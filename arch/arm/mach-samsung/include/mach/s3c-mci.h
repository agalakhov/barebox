/*
 * (C) Copyright 2010 Juergen Beisert, Pengutronix
 *
 * This code is partially based on u-boot code:
 *
 * Copyright 2008, Freescale Semiconductor, Inc
 * Andy Fleming
 *
 * Based (loosely) on the Linux code
 *
 * See file CREDITS for list of people who contributed to this
 * project.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __MACH_MMC_H_
#define __MACH_MMC_H_

struct s3c_mci_platform_data {
	unsigned caps;	/**< supported operating modes (MMC_MODE_*) */
	unsigned voltages; /**< supported voltage range (MMC_VDD_*) */
	unsigned f_min;	/**< min operating frequency in Hz (0 -> no limit) */
	unsigned f_max;	/**< max operating frequency in Hz (0 -> no limit) */
	/* TODO */
	/* function to modify the voltage */
	/* function to switch the voltage */
	/* function to detect the presence of a SD card in the socket */
	unsigned gpio_detect;
	unsigned detect_invert;
	unsigned clk_src:2; /* 0/1 HCLK, 2 EPLL, 3 external */
	unsigned pin_strength:2; /* 2mA, 4mA, 7mA or 9mA */
};

#endif /* __MACH_MMC_H_ */
