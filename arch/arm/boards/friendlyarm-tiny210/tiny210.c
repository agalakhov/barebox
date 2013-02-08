/*
 * Copyright (C) 2012 Alexey Galakhov
 * Based on Mini6410 code by Juergen Beisert
 *
 * Copyright (C) 2012 Juergen Beisert, Pengutronix
 *
 * In some ways inspired by code
 *
 * (C) Copyright 2002
 * Sysgo Real-Time Solutions, GmbH <www.elinos.com>
 * Marius Groeger <mgroeger@sysgo.de>
 *
 * (C) Copyright 2002
 * David Mueller, ELSOFT AG, <d.mueller@elsoft.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <driver.h>
#include <init.h>
#include <sizes.h>
#include <generated/mach-types.h>
#include <dm9000.h>
#include <gpio.h>
#include <led.h>
#include <io.h>
#include <nand.h>
#include <asm/armlinux.h>
#include <mach/s3c-iomap.h>
#include <mach/s3c-clocks.h>
#include <mach/s3c-generic.h>
#include <mach/s3c-esdhc.h>

static struct s3c_sdhc_platform_data tiny210_esdhc0 = {
	/* socket is connected to 3.3 V */
	.esdhc_pd.voltages = MMC_VDD_32_33 | MMC_VDD_33_34,
	/* only four data lines are connected */
	.esdhc_pd.host_caps = MMC_MODE_4BIT | MMC_MODE_HS | MMC_MODE_HS_52MHz,
	.esdhc_pd.cd_type = ESDHC_CD_CONTROLLER,
//	.esdhc_pd.wp_type = ESDHC_WP_GPIO,
//	.esdhc_pd.wp_gpio = GPL13,
	.esdhc_pd.f_max = 52000000,

	.clk_src = 2, /* use HSMMCx as the main clock card's clock */
	.pin_strength = 0, /* [2mA], 4mA, 7mA or 9mA below 25 MHz */
	.pin_strength25 = 3, /* 2mA, 4mA, 7mA or [9mA] above 25 MHz */

/* don't ask me, how these delay settings where calculated. Just copy & paste ;) */
	.rx_delay_en = 0 /*1*/, /* use the delay setting for RX */
	.rx_delay = 2 /*3*/, /* basic delay + 2ns */
	.rx_delay_en25 = 0, /* TODO */
	.rx_delay25 = 0, /* TODO */

	.tx_delay_en = 0, /* ignore the delay settings for TX (copy & paste) */
	.tx_delay = 2 /*0*/,
	.tx_delay_en25 = 0, /* TODO */
	.tx_delay25 = 0, /* TODO */
};



static const unsigned pin_usage[] = {
	/* TODO */
};

static struct gpio_led leds[] = {
	{
		.gpio = GPJ20,
		.led = {
			.name = "led1",
		}
	}, {
		.gpio = GPJ21,
		.led = {
			.name = "led2",
		}
	}, {
		.gpio = GPJ22,
		.led = {
			.name = "led3",
		}
	}, {
		.gpio = GPJ23,
		.led = {
			.name = "led4",
		}
	}
};

static int tiny210_mem_init(void)
{
	arm_add_mem_device("ram0", S3C_SDRAM_BASE, s5p_get_memory_size());
	return 0;
}
mem_initcall(tiny210_mem_init);

static int tiny210_console_init(void)
{
	/*
	 * configure the UART1 right now, as barebox will
	 * start to send data immediately
	 */
	s3c_gpio_mode(GPA00_RXD0 | ENABLE_PU);
	s3c_gpio_mode(GPA01_TXD0);
	s3c_gpio_mode(GPA02_NCTS0 | ENABLE_PU);
	s3c_gpio_mode(GPA03_NRTS0);

	add_generic_device("s3c_serial", DEVICE_ID_DYNAMIC, NULL,
			   S3C_UART1_BASE, S3C_UART1_SIZE,
			   IORESOURCE_MEM, NULL);
	return 0;
}
console_initcall(tiny210_console_init);

static int tiny210_devices_init(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(pin_usage); i++)
		s3c_gpio_mode(pin_usage[i]);

	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		leds[i].active_low = 1;
		gpio_direction_output(leds[i].gpio, leds[i].active_low);
		led_gpio_register(&leds[i]);
	}

	add_generic_device("esdhc_s3c64xx", 0, NULL, 0xEB000000,
			0x100, IORESOURCE_MEM, &tiny210_esdhc0);

	armlinux_set_bootparams((void*)S3C_SDRAM_BASE + 0x100);
	armlinux_set_architecture(MACH_TYPE_MINI210);

	return 0;
}
device_initcall(tiny210_devices_init);
