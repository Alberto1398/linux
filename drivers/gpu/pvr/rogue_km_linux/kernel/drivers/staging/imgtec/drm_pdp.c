/*************************************************************************/ /*!
@File
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/version.h>

#include <drm/drmP.h>
#include <pvr_drm_display.h>

#include "drm_pdp.h"

#define DRM_PDP_DEBUGFS_DISPLAY_ENABLED "display_enabled"
#define DRM_PDP_IRQ 1

struct pvr_drm_display_buffer {
	struct pdp_mm_allocation *allocation;
	struct kref refcount;
};

static bool display_enable = true;

module_param(display_enable, bool, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(display_enable, "Enable all displays (default: Y)");


static void display_buffer_destroy(struct kref *kref)
{
	struct pvr_drm_display_buffer *buffer =
		container_of(kref, struct pvr_drm_display_buffer, refcount);

	DRM_DEBUG_DRIVER("buffer %p\n", buffer);

	pdp_mm_free(buffer->allocation);
	kfree(buffer);
}

static inline void display_buffer_ref(struct pvr_drm_display_buffer *buffer)
{
	DRM_DEBUG_DRIVER("buffer %p\n", buffer);
	kref_get(&buffer->refcount);
}

static inline void display_buffer_unref(struct pvr_drm_display_buffer *buffer)
{
	DRM_DEBUG_DRIVER("buffer %p\n", buffer);
	kref_put(&buffer->refcount, display_buffer_destroy);
}


static int display_enabled_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t display_enabled_read(struct file *file,
				    char __user *user_buffer,
				    size_t count,
				    loff_t *position_ptr)
{
	struct pdp_display_device *display_dev = file->private_data;
	loff_t position = *position_ptr;
	char buffer[] = "N\n";
	size_t buffer_size = ARRAY_SIZE(buffer);
	int err;

	if (position < 0)
		return -EINVAL;
	else if (position >= buffer_size || count == 0)
		return 0;

	if (display_dev->enabled)
		buffer[0] = 'Y';

	if (count > buffer_size - position)
		count = buffer_size - position;

	err = copy_to_user(user_buffer, &buffer[position], count);
	if (err)
		return -EFAULT;

	*position_ptr = position + count;

	return count;
}

static ssize_t display_enabled_write(struct file *file,
				     const char __user *user_buffer,
				     size_t count,
				     loff_t *position)
{
	struct pdp_display_device *display_dev = file->private_data;
	char buffer[3];
	int err;

	count = min(count, ARRAY_SIZE(buffer) - 1);

	err = copy_from_user(buffer, user_buffer, count);
	if (err)
		return -EFAULT;
	buffer[count] = '\0';

	if (strtobool(buffer, &display_dev->enabled) == 0 &&
	    display_dev->crtc)
		pdp_crtc_set_plane_enabled(display_dev->crtc,
					   display_dev->enabled);

	return count;
}

static irqreturn_t pdp_irq_handler(void *data)
{
	struct pdp_display_device *display_dev =
		(struct pdp_display_device *)data;

	return pdp_crtc_irq_handler(display_dev);
}

static const struct file_operations pdp_display_enabled_fops = {
	.owner = THIS_MODULE,
	.open = display_enabled_open,
	.read = display_enabled_read,
	.write = display_enabled_write,
	.llseek = default_llseek,
};


int drm_pdp_init(struct drm_device *dev, void **display_priv_out)
{
	struct pdp_display_device *display_dev;
	int err;

	DRM_DEBUG_DRIVER("begin initialisation\n");

	if (!dev || !display_priv_out)
		return -EINVAL;

	display_dev = kzalloc(sizeof(*display_dev), GFP_KERNEL);
	if (!display_dev)
		return -ENOMEM;

	display_dev->dev = dev;
	display_dev->enabled = display_enable;

	display_dev->debugfs_dir_entry = debugfs_create_dir(DRVNAME, NULL);
	if (IS_ERR_OR_NULL(display_dev->debugfs_dir_entry)) {
		DRM_INFO("failed to create debugfs root directory\n");
		display_dev->debugfs_dir_entry = NULL;
	}

	err = pdp_mm_init(display_dev);
	if (err)
		goto err_remove_debugfs_dir;

	*display_priv_out = display_dev;

	DRM_DEBUG_DRIVER("finished initialisation\n");

	return 0;

err_remove_debugfs_dir:
	debugfs_remove(display_dev->debugfs_dir_entry);
	kfree(display_dev);
	return err;
}
EXPORT_SYMBOL(drm_pdp_init);

int drm_pdp_configure(void *display_priv)
{
	struct pdp_display_device *display_dev =
		(struct pdp_display_device *)display_priv;
	int err;

	DRM_DEBUG_DRIVER("begin configure\n");

	if (!display_dev)
		return -EINVAL;

	err = pdp_modeset_init(display_dev);
	if (err) {
		DRM_ERROR("modeset initialisation failed (err=%d)\n", err);
		return err;
	}

	err = drm_vblank_init(display_dev->dev, 1);
	if (err) {
		DRM_ERROR("failed to complete vblank init (err=%d)\n", err);
		goto err_modeset_deinit;
	}

	err = pvr_drm_irq_install(display_dev->dev,
				  DRM_PDP_IRQ,
				  pdp_irq_handler,
				  &display_dev->irq_handle);
	if (err) {
		DRM_ERROR("failed to install display irq handler (err=%d)\n",
			  err);
		goto err_vblank_cleanup;
	}

	if (display_dev->debugfs_dir_entry) {
		display_dev->display_enabled_entry =
			debugfs_create_file(DRM_PDP_DEBUGFS_DISPLAY_ENABLED,
					    S_IFREG | S_IRUGO | S_IWUSR,
					    display_dev->debugfs_dir_entry,
					    display_dev,
					    &pdp_display_enabled_fops);
		if (IS_ERR_OR_NULL(display_dev->display_enabled_entry)) {
			DRM_INFO("failed to create '%s' debugfs entry\n",
				 DRM_PDP_DEBUGFS_DISPLAY_ENABLED);
			display_dev->display_enabled_entry = NULL;
		}
	}

	DRM_DEBUG_DRIVER("finished configure\n");

	return 0;

err_vblank_cleanup:
	drm_vblank_cleanup(display_dev->dev);
err_modeset_deinit:
	pdp_modeset_cleanup(display_dev);
	return err;
}
EXPORT_SYMBOL(drm_pdp_configure);

void drm_pdp_cleanup(void *display_priv)
{
	struct pdp_display_device *display_dev =
		(struct pdp_display_device *)display_priv;
	int err;

	DRM_DEBUG_DRIVER("begin cleanup\n");

	if (!display_dev)
		return;

	drm_vblank_cleanup(display_dev->dev);

	err = pvr_drm_irq_uninstall(display_dev->dev,
				    display_dev->irq_handle);
	if (err)
		DRM_ERROR("failed to uninstall display irq handler (err=%d)\n",
			  err);

	debugfs_remove(display_dev->display_enabled_entry);

	pdp_modeset_cleanup(display_dev);
	pdp_mm_cleanup(display_dev);

	debugfs_remove(display_dev->debugfs_dir_entry);

	kfree(display_dev);

	DRM_DEBUG_DRIVER("finished cleanup\n");
}
EXPORT_SYMBOL(drm_pdp_cleanup);

int drm_pdp_buffer_alloc(void *display_priv,
			 size_t size,
			 struct pvr_drm_display_buffer **buffer_out)
{
	struct pvr_drm_display_buffer *buffer;
	int err;

	if (!display_priv || size == 0 || !buffer_out)
		return -EINVAL;

	buffer = kmalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	err = pdp_mm_alloc(display_priv, size, &buffer->allocation);
	if (err) {
		kfree(buffer);
		return 0;
	}

	kref_init(&buffer->refcount);

	*buffer_out = buffer;

	DRM_DEBUG_DRIVER("buffer %p\n", buffer);

	return 0;
}
EXPORT_SYMBOL(drm_pdp_buffer_alloc);

int drm_pdp_buffer_free(struct pvr_drm_display_buffer *buffer)
{
	DRM_DEBUG_DRIVER("buffer %p\n", buffer);

	if (!buffer)
		return -EINVAL;

	display_buffer_unref(buffer);

	return 0;
}
EXPORT_SYMBOL(drm_pdp_buffer_free);

uint64_t *drm_pdp_buffer_acquire(struct pvr_drm_display_buffer *buffer)
{
	uint64_t *addr_array;
	uint64_t addr;
	size_t page_count;
	size_t size;
	size_t i;
	int err;

	DRM_DEBUG_DRIVER("buffer %p\n", buffer);

	if (!buffer)
		return ERR_PTR(-EINVAL);

	err = pdp_mm_dev_paddr(buffer->allocation, &addr);
	if (err)
		return ERR_PTR(err);

	err = pdp_mm_size(buffer->allocation, &size);
	if (err)
		return ERR_PTR(err);

	page_count = size >> PAGE_SHIFT;

	addr_array = kmalloc_array(page_count, sizeof(*addr_array), GFP_KERNEL);
	if (!addr_array)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < page_count; i++) {
		addr_array[i] = addr;
		addr += PAGE_SIZE;
	}

	display_buffer_ref(buffer);

	return addr_array;
}
EXPORT_SYMBOL(drm_pdp_buffer_acquire);

int drm_pdp_buffer_release(struct pvr_drm_display_buffer *buffer,
			   uint64_t *dev_paddr_array)
{
	DRM_DEBUG_DRIVER("buffer %p\n", buffer);

	if (!buffer || !dev_paddr_array)
		return -EINVAL;

	display_buffer_unref(buffer);

	kfree(dev_paddr_array);

	return 0;
}
EXPORT_SYMBOL(drm_pdp_buffer_release);

void *drm_pdp_buffer_vmap(struct pvr_drm_display_buffer *buffer)
{
	uint64_t addr;
	size_t size;
	int err;

	DRM_DEBUG_DRIVER("buffer %p\n", buffer);

	if (!buffer)
		return NULL;

	err = pdp_mm_cpu_paddr(buffer->allocation, &addr);
	if (err) {
		DRM_ERROR("failed to get allocation CPU physical address\n");
		return NULL;
	}

	err = pdp_mm_size(buffer->allocation, &size);
	if (err) {
		DRM_ERROR("failed to get allocation size\n");
		return NULL;
	}

	display_buffer_ref(buffer);

	return ioremap_wc(addr, size);
}
EXPORT_SYMBOL(drm_pdp_buffer_vmap);

void drm_pdp_buffer_vunmap(struct pvr_drm_display_buffer *buffer, void *vaddr)
{
	DRM_DEBUG_DRIVER("buffer %p\n", buffer);
	display_buffer_unref(buffer);
	iounmap(vaddr);
}
EXPORT_SYMBOL(drm_pdp_buffer_vunmap);

u32 drm_pdp_get_vblank_counter(void *display_priv, int crtc)
{
	struct pdp_display_device *display_dev =
		(struct pdp_display_device *)display_priv;

	if (!display_dev)
		return 0;

	return drm_vblank_count(display_dev->dev, crtc);
}
EXPORT_SYMBOL(drm_pdp_get_vblank_counter);

int drm_pdp_enable_vblank(void *display_priv, int crtc)
{
	struct pdp_display_device *display_dev =
		(struct pdp_display_device *)display_priv;

	if (!display_dev)
		return -EINVAL;

	switch (crtc) {
	case 0:
		pdp_crtc_set_vblank_enabled(display_dev->crtc, true);
		break;
	default:
		DRM_ERROR("invalid crtc %d\n", crtc);
		return -EINVAL;
	}
	DRM_DEBUG_DRIVER("vblank interrupts enabled for crtc %d\n", crtc);

	return 0;
}
EXPORT_SYMBOL(drm_pdp_enable_vblank);

int drm_pdp_disable_vblank(void *display_priv, int crtc)
{
	struct pdp_display_device *display_dev =
		(struct pdp_display_device *)display_priv;

	if (display_dev == NULL)
		return -EINVAL;

	switch (crtc) {
	case 0:
		pdp_crtc_set_vblank_enabled(display_dev->crtc, false);
		break;
	default:
		DRM_ERROR("invalid crtc %d\n", crtc);
		return -EINVAL;
	}
	DRM_DEBUG_DRIVER("vblank interrupts disabled for crtc %d\n", crtc);

	return 0;
}
EXPORT_SYMBOL(drm_pdp_disable_vblank);


static int __init pdp_init(void)
{
	return 0;
}

static void __exit pdp_exit(void)
{
}

module_init(pdp_init);
module_exit(pdp_exit);

MODULE_AUTHOR("Imagination Technologies Ltd. <gpl-support@imgtec.com>");
MODULE_LICENSE("Dual MIT/GPL");
