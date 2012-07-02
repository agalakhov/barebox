/*
 * Copyright (C) 2012 Alexey Galakhov
 *
 * Based on the original driver by Jinsung Yang <jsgood.yang@samsung.com>
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *
 * Uses some code from older S3C24x0 driver
 * Copyright (C) 2009 Juergen Beisert, Pengutronix
 * Copyright © 2004-2008 Simtec Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <config.h>
#include <common.h>
#include <driver.h>
#include <malloc.h>
#include <io.h>
#include <init.h>
#include <asm-generic/errno.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <mach/nand-samsung.h>

#define __nand_boot_init


#define NFCONF	0x00
#define NFCONT	0x04
#define NFCMMD	0x08
#define NFADDR	0x0C
#define NFDATA	0x10
#define NFSTAT	0x28
#define NFMECC	0x34

#define NFCONT_EN	(1)
#define NFCONT_nCE0	(1 << 1)
#define NFCONT_nCE1	(1 << 1)
#define NFCONT_nCE2	(1 << 22)
#define NFCONT_nCE3	(1 << 23)
#define NFCONT_MECCINIT	(1 << 5)
#define NFCONT_MECCLOCK	(1 << 7)
#define NFCONT_MECCDIRWR (1 << 18)

#define NFSTAT_BUSY	(1)

#define NF8BASE		0x20000
#define NF8CONF		(NF8BASE + 0x00)
#define NF8CONT		(NF8BASE + 0x20)
#define NF8STAT		(NF8BASE + 0x30)
#define NF8SECSTAT	(NF8BASE + 0x40)
#define NF8PRGECC	(NF8BASE + 0x90)
#define NF8ECCERL	(NF8BASE + 0xC0)
#define NF8ECCERP	(NF8BASE + 0xF0)

#define NF8CONT_MECCRESET	(1 << 0)
#define NF8CONT_MECCINIT	(1 << 2)
#define NF8CONT_ECCDIRWR	(1 << 16)

#define NF8STAT_ECCBUSY		(1 << 31)

struct s3c_nand_host {
	struct mtd_info		mtd;
	struct nand_chip	nand;
	struct mtd_partition	*parts;
	struct device_d		*dev;

	void __iomem		*base;
};

/*
 * ECC layout for 8-bit ECC (13 bytes/page)
 * Compatible with bl0 bootloader, see iROM appnote
 */
static struct nand_ecclayout nand_oob_s3c_64 = {
	.eccbytes = 52,
	.eccpos = {
		   12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
		   25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
		   38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
		   51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63
		  },
	.oobfree = { { .offset = 2, .length = 10 } }
};

/*
 * ECC layout for 16-bit ECC (26 bytes/page)
 * Compatible with bl0 bootloader, see iROM appnote
 */
static struct nand_ecclayout nand_oob_s3c_128 = {
	.eccbytes = 104,
	.eccpos = {
		   12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
		   25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
		   38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
		   51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
		   64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
		   77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
		   90, 91, 92, 93, 94, 95, 96, 97, 98, 99,100,101,102,
		  103,104,105,106,107,108,109,110,111,112,113,114,115
		  },
	.oobfree = {
		     { .offset = 2, .length = 10 },
		     { .offset = 116, .length = 12 }
		    }
};


/**
 * Enable the NAND flash controller
 * @param[in] host Base address of the NAND controller
 * @param[in] timing Timing to access the NAND memory
 */
static void __nand_boot_init enable_nand_controller(void __iomem *host, uint32_t timing)
{
	writel(NFCONT_EN | NFCONT_nCE0 | NFCONT_nCE1 | NFCONT_nCE2 | NFCONT_nCE3, host + NFCONT);
	writel(timing, host + NFCONF);
}

/**
 * Diable the NAND flash controller
 * @param[in] host Base address of the NAND controller
 */
static void __nand_boot_init disable_nand_controller(void __iomem *host)
{
	writel(NFCONT_nCE0 | NFCONT_nCE1 | NFCONT_nCE2 | NFCONT_nCE3, host + NFCONT);
}

static void s3c_nand_hwcontrol(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct s3c_nand_host *host = nand_chip->priv;

	if (cmd == NAND_CMD_NONE)
		return;
	/* If the CLE should be active, this call is a NAND command */
	if (ctrl & NAND_CLE)
		writeb(cmd, host->base + NFCMMD);
	/* If the ALE should be active, this call is a NAND address */
	if (ctrl & NAND_ALE)
		writeb(cmd, host->base + NFADDR);
}

static void s3c_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct s3c_nand_host *host = nand_chip->priv;

	uint32_t reg = readl(host->base + NFCONT);
	reg |= NFCONT_nCE0 | NFCONT_nCE1 | NFCONT_nCE2 | NFCONT_nCE3;
	switch (chip) {
	case 0:
		reg &= ~NFCONT_nCE0;
		break;
	case 1:
		reg &= ~NFCONT_nCE1;
		break;
	case 2:
		reg &= ~NFCONT_nCE2;
		break;
	case 3:
		reg &= ~NFCONT_nCE3;
		break;
	default:
		break;
	}
	writel(reg, host->base + NFCONT);
}

static int s3c_nand_devready(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct s3c_nand_host *host = nand_chip->priv;

	return readl(host->base + NFSTAT) & NFSTAT_BUSY;
}

/**
 * Read one block of data from the NAND port
 * @param[in] mtd Instance data
 * @param[out] buf buffer to write data to
 * @param[in] len byte count
 */
static void s3c_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct s3c_nand_host *host = nand_chip->priv;

	/* cleanup any unaligned read */
	if (unlikely((uint32_t)buf & 3)) {
		for (; (uint32_t)buf & 3; len--)
			*buf++ = readb(host->base + NFDATA);
	}

	readsl(host->base + NFDATA, buf, len >> 2);

	/* cleanup any fractional read */
	if (unlikely(len & 3)) {
		buf += len & ~3;

		for (; len & 3; len--)
			*buf++ = readb(host->base + NFDATA);
	}
}

/**
 * Write one block of data to the NAND port
 * @param[in] mtd Instance data
 * @param[out] buf buffer to read data from
 * @param[in] len byte count
 */
static void s3c_nand_write_buf(struct mtd_info *mtd, const uint8_t *buf,
					int len)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct s3c_nand_host *host = nand_chip->priv;

	/* cleanup any unaligned write */
	if (unlikely((uint32_t)buf & 3)) {
		for (; (uint32_t)buf & 3; len--, buf++)
			writeb(*buf, host->base + NFDATA);
	}

	writesl(host->base + NFDATA, buf, len >> 2);

	/* cleanup any fractional write */
	if (unlikely(len & 3)) {
		buf += len & ~3;

		for (; len & 3; len--, buf++)
			writeb(*buf, host->base + NFDATA);
	}
}

static inline void rwl(void __iomem *reg, uint32_t rst, uint32_t set)
{
	uint32_t r;
	r = readl(reg);
	r &= ~rst;
	r |= set;
	writel(r, reg);
}

static void s3c_nand_enable_hwecc_new8(struct mtd_info *mtd, int mode)
{
	struct nand_chip *chip = mtd->priv;
	struct s3c_nand_host *host = chip->priv;

	uint32_t reg;

	int wr = (mode == NAND_ECC_WRITE) ? 1 : 0;

	/* Set ECC mode */
	reg = 3; /* 8-bit */
	reg |= (chip->ecc.size - 1) << 16;
	writel(reg, host->base + NF8CONF);

	/* Set ECC direction */
	rwl(host->base + NF8CONT, NF8CONT_ECCDIRWR,
	    (mode == NAND_ECC_WRITE) ? NF8CONT_ECCDIRWR : 0);

	/* Reset status bits */
	rwl(host->base + NF8STAT, 0, (1 << 24) | (1 << 25));

	/* Unlock ECC */
	rwl(host->base + NFCONT, NFCONT_MECCLOCK, 0);

	/* Initialize ECC */
	rwl(host->base + NF8CONT, 0, NF8CONT_MECCINIT);
}

static void wait_for_bit(void __iomem *reg, uint32_t bit)
{
	int i;
	for (i = 0; i < 100000; ++i) {
		if (readl(reg) & bit)
			break;
	}
	if (i >= 100000)
		printf("Timed out!\n");
	writel(readl(reg) | bit, reg);
}

static void readecc(void __iomem *eccbase, uint8_t *ecc_code, unsigned ecc_len)
{
	unsigned i, j;
	for (i = 0; i < ecc_len; i += 4) {
		uint32_t reg = readl(eccbase + i);
		for (j = 0; (j < 4) && (i + j < ecc_len); ++j) {
			ecc_code[i + j] = reg & 0xFF;
			reg >>= 8;
		}
	}
}

static int s3c_nand_calculate_ecc(struct mtd_info *mtd, const uint8_t *dat, uint8_t *ecc_code)
{
	struct nand_chip *chip = mtd->priv;
	struct s3c_nand_host *host = chip->priv;

	int b8 = (chip->ecc.bytes > 8); /* FIXME */

	/* Lock ECC */
	rwl(host->base + NFCONT, 0, NFCONT_MECCLOCK);

	if (b8)
		wait_for_bit(host->base + NF8STAT, (1 << 25));
	else
		wait_for_bit(host->base + NFSTAT, (1 << 7));

	if (b8)
		readecc(host->base + NF8PRGECC, ecc_code, chip->ecc.bytes);
	else
		readecc(host->base + NFMECC, ecc_code, chip->ecc.bytes);

	return 0;
}

static int s3c_nand_correct_data_new8(struct mtd_info *mtd, uint8_t *dat,
				uint8_t *dummy1, uint8_t *dummy2)
{
	struct nand_chip *chip = mtd->priv;
	struct s3c_nand_host *host = chip->priv;
	uint32_t reg;
	int errs, i;

	rwl(host->base + NFCONT, 0, NFCONT_MECCLOCK);
	wait_for_bit(host->base + NF8STAT, (1 << 24));
	while (readl(host->base + NF8STAT) & NF8STAT_ECCBUSY)
		;

	reg = readl(host->base + NF8SECSTAT);
	errs = reg & 0x1F;
	if (likely(errs == 0))
		return 0;
	if (unlikely(errs > 8)) /* FIXME */
		return -1;

	for (i = 0; i < 16; ++i) {
		/* SECSTAT register has bit pattern at bits 8+ */
		if (reg & (1 << (8 + i))) {
			unsigned loc = (readl(host->base + NF8ECCERL + (i >> 1))
                                        >> ((i & 1) << 4)) & 0x3FF;
			uint8_t mask = (readl(host->base + NF8ECCERP + (i >> 2))
                                        >> ((i & 3) << 3)) & 0xFF;
			dat[loc] ^= mask;
		}
	}

	return errs;
}

/*
 * S3C error correction hardware requires reading ECC
 * right after block reading
 */
static int s3c_nand_read_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
			       uint8_t *buf)
{
	int i;
	unsigned pos;
	int eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	uint8_t *p = buf;
	uint8_t *ecc_code = chip->buffers->ecccode;
	uint32_t *eccpos = chip->ecc.layout->eccpos;

	for (i = 0, pos = 0;
	     eccsteps;
	     eccsteps--, i += eccbytes, pos += eccsize, p += eccsize) {
		int stat;
		if (likely(pos > 0))
			chip->cmdfunc(mtd, NAND_CMD_RNDOUT, pos, -1);
		chip->ecc.hwctl(mtd, NAND_ECC_READ);
		chip->read_buf(mtd, p, eccsize);
		chip->cmdfunc(mtd, NAND_CMD_RNDOUT, mtd->writesize + eccpos[i], -1);
		chip->read_buf(mtd, &ecc_code[i], eccbytes);
		stat = chip->ecc.correct(mtd, p, NULL, NULL);
		if (stat < 0)
			mtd->ecc_stats.failed++;
		else
			mtd->ecc_stats.corrected += stat;
	}

	return 0;
}


static int s3c_nand_inithw(struct s3c_nand_host *host)
{
	struct s3c_nand_platform_data *pdata = host->dev->platform_data;
	uint32_t tmp;

	/* reset the NAND controller */
	disable_nand_controller(host->base);

	if (pdata != NULL)
		tmp = pdata->nand_timing;
	else
		/* else slowest possible timing */
		tmp = CALC_NFCONF_TIMING(4, 8, 8);

	/* reenable the NAND controller */
	enable_nand_controller(host->base, tmp);

	return 0;
}

static int s3c_nand_probe(struct device_d *dev)
{
	struct nand_chip *chip;
	struct s3c_nand_platform_data *pdata = dev->platform_data;
	struct mtd_info *mtd;
	struct s3c_nand_host *host;
	int ret;


	/* Allocate memory for MTD device structure and private data */
	host = kzalloc(sizeof(struct s3c_nand_host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->dev = dev;
	host->base = dev_request_mem_region(dev, 0);

	/* structures must be linked */
	chip = &host->nand;
	mtd = &host->mtd;
	mtd->priv = chip;

	/* init the default settings */

	/* 50 us command delay time */
	chip->chip_delay = 50;
	chip->priv = host;

	chip->IO_ADDR_R = chip->IO_ADDR_W = host->base + NFDATA;

	chip->read_buf = s3c_nand_read_buf;
	chip->write_buf = s3c_nand_write_buf;
	chip->cmd_ctrl = s3c_nand_hwcontrol;
	chip->dev_ready = s3c_nand_devready;
	chip->select_chip = s3c_nand_select_chip;

	/* we are using the hardware ECC feature of this device */
	chip->ecc.calculate = s3c_nand_calculate_ecc;
	chip->ecc.correct = s3c_nand_correct_data_new8;
	chip->ecc.hwctl = s3c_nand_enable_hwecc_new8;
	chip->ecc.read_page = s3c_nand_read_page_hwecc;

	chip->ecc.mode = NAND_ECC_HW;
	chip->ecc.size = 512;
	switch (pdata->ecc_bits) {
	case 1:
		chip->ecc.size = 256;
		chip->ecc.bytes = 4;
		break;
	case 4:
		chip->ecc.bytes = 8;
		break;
	case 8:
		chip->ecc.bytes = 13;
		chip->ecc.layout = &nand_oob_s3c_64;
		break;
	case 12:
		chip->ecc.bytes = 20;
		break;
	case 16:
		chip->ecc.bytes = 26;
		break;
	default:
		ret = -EINVAL;
		goto on_error;
	}

	if (pdata->flash_bbt) {
		/* use a flash based bbt */
//		chip->options |= NAND_USE_FLASH_BBT;
	}

	ret = s3c_nand_inithw(host);
	if (ret != 0)
		goto on_error;

	/* Scan to find existence of the device */
	ret = nand_scan(mtd, 1);
	if (ret != 0) {
		ret = -ENXIO;
		goto on_error;
	}

	ret = add_mtd_device(mtd, "nand");
	return ret;

on_error:
	free(host);
	return ret;
}

static struct driver_d s3c_nand_driver = {
	.name  = "s3c_nand",
	.probe = s3c_nand_probe,
};

/*
 * Main initialization routine
 * @return 0 if successful; non-zero otherwise
 */
static int __init s3c_nand_init(void)
{
	return register_driver(&s3c_nand_driver);
}

device_initcall(s3c_nand_init);

/**
 * @file
 * @brief Support for MLC NAND devices
 *
 * See "S5PV210 iROM Application Note" for recommended ECC layout.
 */
