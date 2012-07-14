/*
 * Copyright (C) 2012 Juergen Beisert
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
#include <gpio.h>
#include <io.h>
#include <asm/armlinux.h>
#include <mach/s3c-iomap.h>
#include <mach/s3c-generic.h>

static const unsigned tiny6410_pin_usage[] = {
};

static int tiny6410_mem_init(void)
{
	arm_add_mem_device("ram0", S3C_SDRAM_BASE, s3c6410_get_memory_size());

	return 0;
}
mem_initcall(tiny6410_mem_init);

/* init CPU card specific devices */
void tiny6410_init(const char *bb_name)
{
	int i;

	/* ----------- configure the access to the outer space ---------- */
	for (i = 0; i < ARRAY_SIZE(tiny6410_pin_usage); i++)
		s3c_gpio_mode(tiny6410_pin_usage[i]);

	armlinux_set_bootparams((void *)S3C_SDRAM_BASE + 0x100);
	armlinux_set_architecture(MACH_TYPE_TINY6410);
}
