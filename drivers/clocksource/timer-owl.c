/*
 * Timer driver for Actions OWL SoC platforms
 *
 * Copyright (C) 2014 Actions Semi Inc.
 * David Liu <liuwei@actions-semi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

/* timer register offset */
#define T0_CTL		0x08
#define T0_VAL		0x0c
#define T0_CMP		0x10
#define T1_CTL		0x14
#define T1_VAL		0x18
#define T1_CMP		0x1c

static void __iomem *owl_timer_base;

static void owl_clkevt_mode(enum clock_event_mode mode,
		struct clock_event_device *clk)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		pr_err("%s: periodic mode not supported\n", __func__);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		writel(0x0, owl_timer_base + T1_CTL);
		writel(0x0, owl_timer_base + T1_VAL);
		writel(0x0, owl_timer_base + T1_CMP);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		/* disable irq */
		writel(0x0, owl_timer_base + T1_CTL);
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int owl_clkevt_next_event(unsigned long evt,
		struct clock_event_device *unused)
{
	/* disable timer */
	writel(0x0, owl_timer_base + T1_CTL);

	/* writing the value has immediate effect */
	writel(0x0, owl_timer_base + T1_VAL);
	writel(evt, owl_timer_base + T1_CMP);

	/* enable timer & IRQ */
	writel(0x6, owl_timer_base + T1_VAL);

	return 0;
}

static struct clock_event_device owl_clockevent = {
	.name = "owl_tick",
	.rating = 300,
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = owl_clkevt_mode,
	.set_next_event = owl_clkevt_next_event,
};

static irqreturn_t owl_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	writel(0x1, owl_timer_base + T1_CTL);
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction owl_timer_irq = {
	.name = "owl_timer1",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = owl_timer_interrupt,
	.dev_id = &owl_clockevent,
};

static void __init owl_timer_init(struct device_node *np)
{
#if 0
	struct clk *timer_clk;
#endif
	unsigned long rate;
	int ret, irq;

	pr_info("[OWL] timer initialization\n");

	owl_timer_base = of_iomap(np, 0);
	if (!owl_timer_base) {
		pr_err("%s: failed to map registers\n", __func__);
		return;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("%s: failed to parse IRQ\n", __func__);
		return;
	}

#if 0
	/* FIXME: of clk is not registered at this time */
	timer_clk = of_clk_get(np, 0);
	if (IS_ERR(timer_clk)) {
		pr_err("%s: failed to get clk\n", __func__);
		return;
	}

	clk_prepare_enable(timer_clk);
	rate = clk_get_rate(timer_clk);
#else
	rate = 24000000;
#endif

	/* Timer 1 is used as clock event device */
	writel(0x0, owl_timer_base + T1_CTL);
	writel(0x0, owl_timer_base + T1_VAL);
	writel(0x0, owl_timer_base + T1_CMP);

	ret = setup_irq(irq, &owl_timer_irq);
	if (ret) {
		pr_err("failed to setup irq %d\n", irq);
		return;
	}

	owl_clockevent.cpumask = cpumask_of(0);

	clockevents_config_and_register(&owl_clockevent, rate,
			0xf, 0xffffffff);
}
CLOCKSOURCE_OF_DECLARE(owl, "actions,owl-timer",
		owl_timer_init);
