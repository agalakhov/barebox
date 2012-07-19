/*
 * Copyright (C) 2012 Juergen Beisert
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
 */

#define DEBUG

#include <common.h>
#include <init.h>
#include <io.h>
#include <errno.h>
#include <mci.h>
#include <mci/esdhc.h>

#include <mach/s3c-mci.h>
#include <mach/s3c-generic.h>
#include <mach/s3c-iomap.h>

#include "esdhc-core.h"

unsigned esdhcp_get_base_clk(struct esdhc_mci *this)
{
	struct device_d *dev = this->mci.hw_dev;

	switch (this->pdata->clk_src) {
	case 0:
	case 1:
		pr_debug("%s: Using hclk as the input clock\n", __func__);
		return s3c_get_hclk();
	case 2:
		pr_debug("%s: Using HSMMCLK as the input clock\n", __func__);
		return s3c_get_hsmmc_clk(dev->id);
	default:
		dev_err(dev, "External SDHCI clock not supported\n");
		return 0;
	}
}

/* ----------------------------------------------------------------------- */


#define CLK_SRC 0x1C/* 0x7E00F01c */
/* 18/19 + 0 EPLLout, 1 MPLLout, 2 EPLLin, 3 27 MHz*/

#define HCLK_GATE 0x30/* 0x7E00F030 */
/* 17 + "gating HCLK for HSMMCx" */

#define SCLK_GATE 0x38 /* 0x7E00F038 */
/* 27 + und 24 + ??????? "gating special clock for MMCx" */

#define CLK_DIV1 0x24 /* 0x7E00F024 */
/* 0 + 4 + 8 */

/* HCLK versorgt den Teil, der am internen Bus hängt */
/* dann gibt es noch auf Seite 953 ein BaseCLK für die SD-Karten-Seite */

/* die Bits im CONTROL2-Register nennen sich "Base Clock Source Select" */
/* 00 or 01 =HCLK, 10=HSMMCx (from SYSCON), 11=External
   Clock source (XTI or XEXTCLK) */

/*
 * There are many MMC related clocks mentionend in the datasheet.
 * By trial and error I found:
 *
 *         MMCx_SEL
 * EPLLout ---0-\
 * MPLLout ---1--\-----SCLK_MMCx----DIV_MMCx--+---->HSMMCx
 * EPLLin  ---2--/       on/off      1..16    |
 * 27 MHz  ---3-/                             |
 *                                            |  SELBASECLK
 * HCLK --HCLK_GATE---o-----------------------|--o---0--\
 *                    |                       |  +---1---\---ESDHCDIV--> SD card clock
 *                    |                       +------2---/
 *                    |      EXTCLK -----------------3--/
 *                    |
 *                    |
 *                    |
 *                    +--------------------------------->| MMC unit clock
 *
 * HCLK_HSMMC0 switches off the access to the ESDHC unit and the SD card clock
 * SCLK_MMC0 and SCLK_MMC0_48 does not change anything.
 * Changing the divider value does not change at least anything at the SD card clock
 * Changing the clock source settings, doesn't also change anything at the SD card clock
 *
 * There are some other bits and dividers.
 *
 * Versuche mit
 * - EPLLout, am SD Clock kam raus: 93.757 kHz -> EPLLout = 24 MHz
 * - MPLLout, am SD Clock kam raus: 1.03914 MHz -> MPLLout = 266 MHz
 * - EPLLin, am SD Clock kam raus: 46.8785 kHz -> EPLLin = 12 MHz
 * - 27 MHz, am SD Clock kam raus: 105.469 kHz -> 27 MHz
 *
 *  CLK_SRC 1 from 4 ------------- SCLK_MMCx -----DIV_MMCx-----> 2nd SELBASECLK
 *  ??? ----------- SCLK_MMCx_48 ---------> ?????
 *
 * Beim S5PV210 sind die Takte im CONTROL2 0/1 HCLK, 2 = SCLK_MMCx, 3 nix
 * -> damit sind sie beim S3C6410 genauso
 *
 * CONTROL2 0x7c200080
 */
static int esdhc_s3c64xx_setup_input_clock(struct esdhc_mci *h, int id)
{
	struct esdhc_platform_data *pd = h->pdata;

	if (pd->clk_src == 2)	/* HSMMCx? */
		/* use the MPLL output as our source for the highest available clock */
		s3c_set_hsmmc_clk(id, 1, 1);

	return 0;
}

static int esdhc_s3c64xx_probe(struct device_d *dev)
{
	struct esdhc_platform_data *pd = dev->platform_data;
	struct esdhc_mci *this;

	if (pd == NULL) {
		dev_err(dev, "Missing platform data\n");
		return -EINVAL;
	}

	this = xzalloc(sizeof(struct esdhc_mci));

	printf("New esdhc is at %p\n", this);
	dev->priv = this;
	this->pdata = pd;

	this->base = dev_request_mem_region(dev, 0);
	this->mci.hw_dev = dev;
	printf("New esdhc's hw base is at %p\n", this->base);

	/* enable the defined clock source */
	esdhc_s3c64xx_setup_input_clock(this, dev->id);

	/* feed forward the platform specific values */
	this->mci.voltages = pd->voltages;
	this->mci.host_caps = pd->host_caps;
	this->mci.f_min = pd->f_min == 0 ? esdhcp_get_base_clk(this) / 256 : pd->f_min;
	this->mci.f_max = pd->f_max == 0 ? esdhcp_get_base_clk(this) / 2 : pd->f_max;
	pr_debug("%s: using f_min = %u Hz, f_max = %u Hz\n", __func__, this->mci.f_min, this->mci.f_max);

	return esdhc_register(this);
}

static struct driver_d esdhc_s3c64xx_driver = {
        .name  = "esdhc_s3c64xx",
        .probe = esdhc_s3c64xx_probe,
#ifdef CONFIG_MCI_INFO
	.info = esdhc_info,
#endif
};

static int esdhc_s3c64xx_init_driver(void)
{
        register_driver(&esdhc_s3c64xx_driver);
        return 0;
}

device_initcall(esdhc_s3c64xx_init_driver);
