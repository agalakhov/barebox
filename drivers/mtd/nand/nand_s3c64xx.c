/*
 * Copyright (C) 2012 Juergen Beisert
 *
 * This driver is partially based on code with:
 *  Copyright (c) 2007 Samsung Electronics
 *  with this additionally note:
 *   Based on nand driver from Ben Dooks <ben@simtec.co.uk>
 *   modified by scsuh. based on au1550nd.c
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
 * ----------------------------------------------------------------------------
 * Some background information:
 *
 * This driver uses the extended checksum capabilities of the S3C64xx SoC.
 * It generates 13 bytes of checksums per 512 bytes of data. This is much more
 * than SLC (single level cell) NANDs require, but the more error can be
 * corrected the saver the data is. Even for MLC (multi level cell) NANDs.
 *
 * The driver forces a specific checksum layout in the OOB data of each page.
 * This is due to the fact the built-in ROM routines which are required to boot
 * the SoC from NAND expect this layout.
 * The checksum layout collides with the factory bad block markers which
 * are also part of the OOB area. So, this driver requires a bad block table.
 * Also the kernel running later on must use this bad block table, otherwise
 * it gets confused about the bad block markers in the OOB.
 *
 * The checksum layout the ROM routines expect:
 * - the iROM *always* uses 8 bit correction sums (=13 byte ECC per 512 byte data)
 * - it expects the checksum at the beginning of the OOB area
 *
 * Expected layout:
 *
 * 512 byte page (16 byte OOB):
 * ----------------------------
 *
 * OOB offset      0 ................... 12
 *                ECC0                 ECC12
 *
 * Free bytes in OOB: 13 ... 15
 * Note: This layout keeps no space for the BadBlockTable in the NAND. So, we
 * *can't* support this type of NAND any more!
 *
 * 2048 byte page (64 byte OOB):
 * -----------------------------
 *
 * OOB offset      0 ...... 12    13 .... 25     26 .... 38     39 .... 51
 *                ECC0/1 ECC12/1 ECC0/2 ECC12/2 ECC0/3 ECC12/3 ECC0/4 ECC12/4
 *
 * Free bytes in OOB: 52 ... 63
 *
 * 4096 byte page (128 byte OOB):
 * ------------------------------
 *
 * OOB offset      0 ...... 12    13 ... 25      26 .... 38     39 ... 51
 *                ECC0/1 ECC12/1 ECC0/2 ECC12/2 ECC0/3 ECC12/3 ECC0/4 ECC12/4
 * OOB offset      52 ..... 64    65 ... 77      78 .... 90     91 ... 103
 *                ECC0/5 ECC12/5 ECC0/6 ECC12/6 ECC0/7 ECC12/7 ECC0/8 ECC12/8
 *
 * Free bytes in OOB: 104 ... 127
 *
 * 8192 byte page (436 byte OOB):
 * ------------------------------
 *
 * 0 ... 4095 page data
 * 4096 ... 4199 ECC data
 * OOB data not used in this case, also not the page data from 4200 ... 8191
 *
 * What's bad with this layout:
 *  This type of layout destroys the manufacturer's bad block information!
 *
 * If we do not intend to boot from NAND, we are free with the OOB layout
 *
 * Note: For the 8192 byte page NAND we are in trouble, if we want to boot from it.
 * The maximum size the iROM can handle is 4096 per page. Maybe we can boot from
 * NAND in this case, if we only use the first 4096 bytes per 8192 page. So,
 * the bootloader uses the double count of pages and does not use the second half
 * of each block. What a nightmare to handle...
 *
 */

#include <common.h>
#include <driver.h>
#include <malloc.h>
#include <init.h>

#define NFCONF			0x00
# define NFCONF_NANDBOOT	(1 << 31)
# define NFCONF_ECCCLKCON	(1 << 30)
# define NFCONF_ECC_MLC		(1 << 24)
# define NFCONF_ECC_1BIT	(0 << 23)
# define NFCONF_ECC_4BIT	(2 << 23)
# define NFCONF_ECC_8BIT	(1 << 23)
# define NFCONF_TACLS(x)	((x) << 12)
# define NFCONF_TWRPH0(x)	((x) << 8)
# define NFCONF_TWRPH1(x)	((x) << 4)
# define NFCONF_ADVFLASH	(1 << 3)
# define NFCONF_PAGESIZE	(1 << 2)
# define NFCONF_ADDRCYCLE	(1 << 1)
# define NFCONF_BUSWIDTH	(1 << 0)
#define NFCONT			0x04
# define NFCONT_ECC_ENC		(1 << 18)
# define NFCONT_LOCKTGHT	(1 << 17)
# define NFCONT_LOCKSOFT	(1 << 16)
# define NFCONT_MECCLOCK	(1 << 7)
# define NFCONT_SECCLOCK	(1 << 6)
# define NFCONT_INITMECC	(1 << 5)
# define NFCONT_INITSECC	(1 << 4)
# define NFCONT_nFCE1		(1 << 2)
# define NFCONT_nFCE0		(1 << 1)
# define NFCONT_INITECC		(NFCONT_INITSECC | NFCONT_INITMECC)
#define NFCMMD			0x08
#define NFADDR			0x0c
#define NFDATA8			0x10
#define NFDATA			0x10
#define NFMECCDATA0		0x14
#define NFMECCDATA1		0x18
#define NFSECCDATA		0x1c
#define NFSBLK			0x20
#define NFEBLK			0x24
#define NFSTAT			0x28
# define NFSTAT_ECCENCDONE	(1 << 7)
# define NFSTAT_ECCDECDONE	(1 << 6)
# define NFSTAT_ILEGL_ACC	(1 << 5)
# define NFSTAT_RnB_CHANGE	(1 << 4)
# define NFSTAT_nFCE1		(1 << 3)
# define NFSTAT_nFCE0		(1 << 2)
# define NFSTAT_Res1		(1 << 1)
# define NFSTAT_READY		(1 << 0)
# define NFSTAT_CLEAR		(NFSTAT_ECCENCDONE | NFSTAT_ECCDECDONE | NFSTAT_ILEGL_ACC | NFSTAT_RnB_CHANGE)
#define NFMECCERR0		0x2c
# define NFMECCERR0_ECCBUSY	(1 << 31)
#define NFMECCERR1		0x30
#define NFMECC0			0x34
#define NFMECC1			0x38
#define NFSECC			0x3c
#define NFMLCBITPT		0x40
#define NF8ECCERR0		0x44
#define NF8ECCERR1		0x48
#define NF8ECCERR2		0x4C
#define NFM8ECC0		0x50
#define NFM8ECC1		0x54
#define NFM8ECC2		0x58
#define NFM8ECC3		0x5C
#define NFMLC8BITPT0		0x60
#define NFMLC8BITPT1		0x64

#define S3C64XX_CS_PAGE_SZ 512
#define S3C64XX_CS_SZ 13

/* fields will be filled at runtime */
static struct nand_ecclayout s3c64xx_oob_layout;

static uint8_t scan_ff_pattern[] = { 0xff, 0xff };

/*
 * Sizes and facory bad block markers valid for all seen NAND devices on
 * the Mini6410/Tiny6410:
 *
 *  - SLC K9F1G08U0B (128M/2k/64B), 1st byte in spare area, also first or
 *    second page in each block contains != 0xFF
 *    - ECC: 1 bit / 512 Byte
 *    - options: struct nand_chip.options = NAND_BBT_SCAN2NDPAGE (scans the
 *      first two pages of every page)
 *  - SLC K9F2G08 (256M/2k/64B), 1st byte in spare area, also first or second
 *    page in each block contains != 0xFF
 *    - ECC: 1 bit / 512 Byte
 *    - options: struct nand_chip.options = NAND_BBT_SCAN2NDPAGE (scans the
 *      first two pages of every page)
 *  - SLC K9K8G08U0A (1G/2k/64B), 1st byte in spare area, also first or second
 *    page in each block contains != 0xFF
 *    - ECC: 1 bit / 512 Byte
 *    - options: struct nand_chip.options = NAND_BBT_SCAN2NDPAGE (scans the
 *      first two pages of every page)
 *  - MLC K9GAG08U0E (2G/8k/436B), 1st byte in spare area, also first or last
 *    page in each block contains != 0xFF
 *    - ECC: 24 bit / 1024 Byte
 *    - options: struct nand_chip.options = NAND_BBT_SCANLASTPAGE (or
 *      NAND_BBT_SCANALLPAGES)
 *
 * This pattern is used for the very first time this driver gets access to a
 * virgin NAND and must collect all factory bad block markers to generate the
 * bad block table once.
 */
static struct nand_bbt_descr samsung_bad_blocks_marker = {
	.options = NAND_BBT_SCAN2NDPAGE,
	.offs = 0,
	.len = 2,
	.pattern = scan_ff_pattern
};

#define BBT_OPTIONS NAND_BBT_LASTBLOCK | /* scan from top to down */ \
	NAND_BBT_CREATE | /* create BBT (in RAM) from NAND's info (BBT or BBM) */ \
	NAND_BBT_WRITE | /* write the BBT FIXME */ \
	NAND_BBT_2BIT | /* number of bits per erase block in the BBT */ \
	NAND_BBT_VERSION | /* FIXME */ \
	NAND_BBT_PERCHIP /* BBT per chip? WTF? */

static uint8_t bbt_pattern[] = {'B', 'b', 't', '0' };

/* still unitialized fields will be filled at runtime */
static struct nand_bbt_descr samsung_bbt_main = {
	.options = BBT_OPTIONS,
	.len = sizeof(bbt_pattern),
	.maxblocks = 4,	/* erase blocks to scan when searching for the in flash BBT */
	.pattern = bbt_pattern
};

static uint8_t mirror_pattern[] = {'1', 't', 'b', 'B' };

/* still unitialized fields will be filled at runtime */
static struct nand_bbt_descr samsung_bbt_mirror = {
	.options = BBT_OPTIONS,
	.len = sizeof(mirror_pattern),
	.maxblocks = 4,
	.pattern = mirror_pattern
};

static void s3c6_init_nand_layout_description(unsigned page_sz)
{
	int i;

	/* all check sum bytes are in a continous area at the beginng of the OOB */
	for (i = 0; i < MTD_MAX_ECCPOS_ENTRIES_LARGE; i++)
		s3c64xx_oob_layout.eccpos[i] = i;

	/* per 512 data bytes a 13 bytes checksum is used */
	s3c64xx_oob_layout.eccbytes = DIV_ROUND_UP(page_sz, S3C64XX_CS_PAGE_SZ);
	s3c64xx_oob_layout.oobfree.offset = s3c64xx_oob_layout.eccbytes * S3C64XX_CS_SZ;

	/* all spare bytes in the OOB are always at the end */
	switch (page_sz) {
	case 512: /* we expect at least 16 bytes OOB */
		s3c64xx_oob_layout.oobfree.length = 16 - s3c64xx_oob_layout.oobfree.offset;
		break;
	case 2048: /* we expect at least 64 bytes OOB */
		s3c64xx_oob_layout.oobfree.length = 64 - s3c64xx_oob_layout.oobfree.offset;
		break;
	case 4096: /* we expect at least 128 bytes OOB */
		s3c64xx_oob_layout.oobfree.length = 128 - s3c64xx_oob_layout.oobfree.offset;
		break;
	}

	/*
	 * init the location for the bad block table marker. First approach:
	 * - locate them always behind the checksum
	 */
	samsung_bbt_main.offs = s3c64xx_oob_layout.oobfree.offset;
	samsung_bbt_main.veroffs = samsung_bbt_main.offs + samsung_bbt_main.len;
	samsung_bbt_mirror.offs = s3c64xx_oob_layout.oobfree.offset;
	samsung_bbt_mirror.veroffs = samsung_bbt_mirror.offs + samsung_bbt_mirror.len;
}

struct s3c6_nand {
	struct mtd_info mtd_info;
	struct nand_chip nand_info;

	struct platform_device *pdev;
	struct clk *clk;
	void __iomem *regs;
	int current_mode;
};

static inline struct s3c6_nand *mtd_to_s3c_nand(struct mtd_info *m)
{
	return (struct s3c_nand *)m;
}

/* @r base address of NFC's register
 * @o register offset
 * @m bits to set
 */
static inline void s3c6_set_bit(void __iomem *r, unsigned o, unsigned m)
{
	writel(readl(r + o) | m, r + o);
}

/* @r base address of NFC's register
 * @o register offset
 * @m bits to set
 */
static inline void s3c6_clr_bit(void __iomem *r, unsigned o, unsigned m)
{
	writel(readl(r + o) & ~m, r + o);
}

/*
 * Hardware specific access to control-lines function
 * Written by jsgood
 */
static void s3c6_nand_hwcontrol(struct mtd_info *mtd, int dat, unsigned int ctrl)
{
	struct s3c_nand *i = mtd_to_s3c_nand(mtd);
	unsigned int cur;

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_NCE) {
			if (dat != NAND_CMD_NONE) {
				s3c_clr_bit(i->regs, NFCONT, NFCONT_nFCE0);
// 				cur = readl(i->regs + NFCONT);
// 				cur &= ~NFCONT_nFCE0;
// 				writel(cur, i->regs + NFCONT);
			}
		} else {
			s3c_set_bit(i->regs, NFCONT, NFCONT_nFCE0);
// 			cur = readl(i->regs + NFCONT);
// 			cur |= NFCONT_nFCE0;
// 			writel(cur, i->regs + NFCONT);
		}
	}

	if (dat != NAND_CMD_NONE) {
		if (ctrl & NAND_CLE)
			writeb(dat, i->regs + NFCMMD);
		else if (ctrl & NAND_ALE)
			writeb(dat, i->regs + NFADDR);
	}
}

/*
 * Function for checking device ready pin
 * Written by jsgood
 */
static int s3c6_nand_device_ready(struct mtd_info *mtd)
{
	struct s3c_nand *i = mtd_to_s3c_nand(mtd);

	/* it's to check the RnB nand signal bit and
	 * return to device ready condition in nand_base.c
	 */
	return ((readl(i->regs + NFSTAT) & NFSTAT_READY));
}

static void s3c6_nand_wait_enc(void __iomem *regs)
{
	unsigned long timeo = jiffies;

	timeo += 16;    /* when Hz=200,  jiffies interval 1/200=5mS, waiting for 80mS  80/5 = 16 */

	/* Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine. */

	while (time_before(jiffies, timeo)) {
		if (readl(regs + NFSTAT) & NFSTAT_ECCENCDONE)
				break;
		cond_resched();
	}
}

/*
 * Function for checking ECCDecDone in NFSTAT
 * Written by jsgood
 */
static void s3c6_nand_wait_dec(void __iomem *regs)
{
	unsigned long timeo = jiffies;

	timeo += 16;    /* when Hz=200,  jiffies interval  1/200=5mS, waiting for 80mS  80/5 = 16 */

	/* Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine. */

	while (time_before(jiffies, timeo)) {
		if (readl(regs + NFSTAT) & NFSTAT_ECCDECDONE)
			break;
		cond_resched();
	}
}

/*
 * Function for checking ECC Busy
 * Written by jsgood
 */
static void s3c6_nand_wait_ecc_busy(void __iomem *regs)
{
	unsigned long timeo = jiffies;

	timeo += 16;    /* when Hz=200,  jiffies interval  1/200=5mS, waiting for 80mS  80/5 = 16 */

	/* Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine. */

	while (time_before(jiffies, timeo)) {
		if (!(readl(regs + NFMECCERR0) & NFMECCERR0_ECCBUSY))
			break;
		cond_resched();
	}
}

static void s3c6_nand_wait_ecc_busy(void __iomem *regs)
{
	/* TODO timeout */
	while (readl(regs + NF8ECCERR0) & NFMECCERR0_ECCBUSY) {}
}

static void s3c6_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{
	u_long nfcont, nfconf;
	void __iomem *regs = s3c_nand.regs;

	cur_ecc_mode = mode;

	/* 8 bit selection */
	nfconf = readl(regs + NFCONF);

	nfconf &= ~(0x3 << 23);
	nfconf |= (0x1 << 23);

	writel(nfconf, regs + NFCONF);

	/* Initialize & unlock */
	nfcont = readl(regs + NFCONT);
	nfcont |= NFCONT_INITECC;
	nfcont &= ~NFCONT_MECCLOCK;

	if (mode == NAND_ECC_WRITE)
		nfcont |= NFCONT_ECC_ENC;
	else if (mode == NAND_ECC_READ)
		nfcont &= ~NFCONT_ECC_ENC;

	writel(nfcont, regs + NFCONT);
}

static int s3c6_nand_calculate_ecc(struct mtd_info *mtd, const u_char *dat, u_char *ecc_code)
{
	u_long nfcont, nfm8ecc0, nfm8ecc1, nfm8ecc2, nfm8ecc3;
	void __iomem *regs = s3c_nand.regs;

	/* Lock */
	nfcont = readl(regs + NFCONT);
	nfcont |= NFCONT_MECCLOCK;
	writel(nfcont, (regs + NFCONT));

	if (cur_ecc_mode == NAND_ECC_READ)
		s3c_nand_wait_dec(regs);
	else {
		s3c_nand_wait_enc(regs);

		nfm8ecc0 = readl(regs + NFM8ECC0);
		nfm8ecc1 = readl(regs + NFM8ECC1);
		nfm8ecc2 = readl(regs + NFM8ECC2);
		nfm8ecc3 = readl(regs + NFM8ECC3);

#if 0
		ecc_code[0] = nfm8ecc0 & 0xff;
		ecc_code[1] = (nfm8ecc0 >> 8) & 0xff;
		ecc_code[2] = (nfm8ecc0 >> 16) & 0xff;
		ecc_code[3] = (nfm8ecc0 >> 24) & 0xff;
#else
		writel(nfm8ecc0, &ecc_code[0]);
#endif
#if 0
		ecc_code[4] = nfm8ecc1 & 0xff;
		ecc_code[5] = (nfm8ecc1 >> 8) & 0xff;
		ecc_code[6] = (nfm8ecc1 >> 16) & 0xff;
		ecc_code[7] = (nfm8ecc1 >> 24) & 0xff;
#else
		writel(nfm8ecc1, &ecc_code[4]);
#endif
#if 0
		ecc_code[8] = nfm8ecc2 & 0xff;
		ecc_code[9] = (nfm8ecc2 >> 8) & 0xff;
		ecc_code[10] = (nfm8ecc2 >> 16) & 0xff;
		ecc_code[11] = (nfm8ecc2 >> 24) & 0xff;
#else
		writel(nfm8ecc2, &ecc_code[8]);
#endif
		ecc_code[12] = nfm8ecc3 & 0xff;
	}

	return 0;
}

static int s3c6_nand_correct_data(struct mtd_info *mtd, u_char *dat, u_char *read_ecc, u_char *calc_ecc)
{
	int ret = -1;
	u_long nf8eccerr0, nf8eccerr1, nf8eccerr2, nfmlc8bitpt0, nfmlc8bitpt1;
	u_char err_type;
	void __iomem *regs = s3c_nand.regs;

	s3c_nand_wait_ecc_busy_8bit(regs);

	nf8eccerr0 = readl(regs + NF8ECCERR0);
	nf8eccerr1 = readl(regs + NF8ECCERR1);
	nf8eccerr2 = readl(regs + NF8ECCERR2);
	nfmlc8bitpt0 = readl(regs + NFMLC8BITPT0);
	nfmlc8bitpt1 = readl(regs + NFMLC8BITPT1);

	err_type = (nf8eccerr0 >> 25) & 0xf;

	/* No error, If free page (all 0xff) */
        /* While testing, it was found that NFECCERR0[29] bit is set even if
         * the page contents were not zero. So this code is commented */
        /*if ((nf8eccerr0 >> 29) & 0x1)
 	         err_type = 0;*/

	switch (err_type) {
	case 9: /* Uncorrectable */
		printk("s3c-nand: ECC uncorrectable error detected\n");
		ret = -1;
		break;

	case 8: /* 8 bit error (Correctable) */
		dat[(nf8eccerr2 >> 22) & 0x3ff] ^= ((nfmlc8bitpt1 >> 24) & 0xff);

	case 7: /* 7 bit error (Correctable) */
		dat[(nf8eccerr2 >> 11) & 0x3ff] ^= ((nfmlc8bitpt1 >> 16) & 0xff);

	case 6: /* 6 bit error (Correctable) */
		dat[nf8eccerr2 & 0x3ff] ^= ((nfmlc8bitpt1 >> 8) & 0xff);

	case 5: /* 5 bit error (Correctable) */
		dat[(nf8eccerr1 >> 22) & 0x3ff] ^= (nfmlc8bitpt1 & 0xff);

	case 4: /* 4 bit error (Correctable) */
		dat[(nf8eccerr1 >> 11) & 0x3ff] ^= ((nfmlc8bitpt0 >> 24) & 0xff);

	case 3: /* 3 bit error (Correctable) */
		dat[nf8eccerr1 & 0x3ff] ^= ((nfmlc8bitpt0 >> 16) & 0xff);

	case 2: /* 2 bit error (Correctable) */
		dat[(nf8eccerr0 >> 15) & 0x3ff] ^= ((nfmlc8bitpt0 >> 8) & 0xff);

	case 1: /* 1 bit error (Correctable) */
		printk("s3c-nand: %d bit(s) error detected, corrected successfully\n", err_type);
		dat[nf8eccerr0 & 0x3ff] ^= (nfmlc8bitpt0 & 0xff);
		ret = err_type;
		break;

	case 0: /* No error */
		ret = 0;
		break;
	}

	return ret;
}

static void s3c6_nand_write_page(struct mtd_info *mtd, struct nand_chip *chip, const uint8_t *buf)
{
	int i, eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	const uint8_t *p = buf;
	int badoffs = mtd->writesize == 512 ? NAND_SMALL_BADBLOCK_POS : NAND_LARGE_BADBLOCK_POS;
	uint8_t *ecc_calc = chip->buffers->ecccalc;
	uint32_t *mecc_pos = chip->ecc.layout->eccpos;

	for (i = 0; eccsteps; eccsteps--, i += eccbytes, p += eccsize) {
		chip->ecc.hwctl(mtd, NAND_ECC_WRITE);
		chip->write_buf(mtd, p, eccsize);
		chip->ecc.calculate(mtd, p, &ecc_calc[i]);
	}

	chip->oob_poi[badoffs] = 0xff;
	for (i = 0; i < chip->ecc.total; i++) {
		chip->oob_poi[mecc_pos[i]] = ecc_calc[i];
	}

	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
}

static int s3c6_nand_read_page(struct mtd_info *mtd, struct nand_chip *chip, uint8_t *buf, int page) /* TODO page */
{
	int i, stat, eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	int col = 0;
	uint8_t *p = buf;
	uint32_t *mecc_pos = chip->ecc.layout->eccpos;

	/* Step1: read whole oob */
	col = mtd->writesize;	/* = page size */
	chip->cmdfunc(mtd, NAND_CMD_RNDOUT, col, -1);
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);

	/*
	 * The checksum unit expects always 512 bytes of data and
	 * immediately 13 bytes of the checksum to check the data against.
	 * This could be archieved by using the "random read" feature of the
	 * NAND. But this approach is *very* slow.
	 * This approach reads the 512 bytes of data and then writes 13 bytes
	 * of the checksum. For the checksum engine its the same, but it saves
	 * us the random read usage and speeds up reading a page including the
	 * error check.
	 */
	col = 0;
	for (i = 0; eccsteps; eccsteps--, i += eccbytes, p += eccsize) {
		chip->cmdfunc(mtd, NAND_CMD_RNDOUT, col, -1);
		s3c_nand_enable_hwecc_8bit(mtd, NAND_ECC_READ);
		chip->read_buf(mtd, p, eccsize);
		chip->write_buf(mtd, chip->oob_poi + mecc_pos[0] + ((chip->ecc.steps - eccsteps) * eccbytes), eccbytes);
		chip->ecc.calculate(mtd, 0, 0);
		stat = chip->ecc.correct(mtd, p, NULL, NULL);

		if (stat == -1)
			mtd->ecc_stats.failed++;

		col = eccsize * ((mtd->writesize / eccsize) + 1 - eccsteps);
	}

	return 0;
}

static int s3c6_nand_read_oob(struct mtd_info *mtd, struct nand_chip *chip, int page, int sndcmd)
{
	int eccbytes = chip->ecc.bytes;
	int secc_start = mtd->oobsize - eccbytes;

	if (sndcmd) {
		chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
		sndcmd = 0;
	}

	chip->read_buf(mtd, chip->oob_poi, secc_start);
	return sndcmd;
}

static int s3c6_nand_write_oob(struct mtd_info *mtd, struct nand_chip *chip, int page)
{
	int status = 0;
	int eccbytes = chip->ecc.bytes;
	int secc_start = mtd->oobsize - eccbytes;

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, mtd->writesize, page);

	chip->write_buf(mtd, chip->oob_poi, secc_start);
	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = chip->waitfunc(mtd, chip);
	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

static void s3c6_select_chip(struct mtd_info *mtd, int chip)
{
	struct s3c_nand *i = mtd_to_s3c_nand(mtd);
	u32 reg;

	reg = readl(i->regs + NFCONT);

	if (chip == -1) {
		writel(reg | S3C2412_NFSTAT_nFCE1 | S3C2412_NFSTAT_nFCE0,
			i->regs + NFCONT);
		return;
	}
	if (chip == 1) {
		writel((reg | S3C2412_NFSTAT_nFCE1) & ~S3C2412_NFSTAT_nFCE0,
			i->regs + NFCONT);
		return;
	}
	if (chip == 2) {
		writel((reg | S3C2412_NFSTAT_nFCE0) & ~S3C2412_NFSTAT_nFCE1,
			i->regs + NFCONT);
		return;
	}
}

/**
 * Setup the flash based BBT
 *
 * The purpose of this routine is to detect if this driver runs the very first
 * time on this NAND. If it runs the first time, it acts differently than any
 * time later on.
 * The first time run:
 *  - do not allow to write to the NAND
 *  - scan the NAND device for bad block markers in a regular way and the NAND
 *    manufacturer intends to do
 *  - create the BBT in RAM, but nothing else
 * If in this state the driver is started again, the first time behaviour
 * happens again.
 *
 * If the user runs the initial command to prepare the NAND for further work,
 * writing to the NAND is enabled and the BBT is written to the NAND.
 * Now the bad block markers get overwritten by the checksum and there is no
 * way to return.
 * The next time this driver starts, it detects the BBT in flash memory and
 * enables writing to the device and acts as a regular NAND driver.
 *
 * This function is a special variant of the nand_default_bbt() function from
 * the framework.
 *
 *
 * Andere Idee: Direktes Benutzen des NAND für den Fall, dass man von bootet.
 * Bootet man nicht davon, gibt es weitere Kommandos, um das NAND vorzubereiten
 * und einen Schalter, der die Nutzung von der Shell aus freischaltet
 */
static int s3c6_default_bbt(struct mtd_info *mtd)
{
	/* setup our special BBT description to detect the very first call */
	this->bbt_td = &samsung_bbt_main;
	this->bbt_md = &samsung_bbt_mirror;
	/* setup the standard bad block marker pattern for the very first call */
	this->badblock_pattern = &samsung_bad_blocks_marker;

	return nand_scan_bbt(mtd, &samsung_bad_blocks_marker);
}

static int s3c6_nfc_calc_rate(unsigned wanted, unsigned clk, int max)
{
	int result;

	result = DIV_ROUND_UP((wanted * clk), NS_IN_KHZ);

	pr_debug("result %d from %ld, %d\n", result, clk, wanted);

	if (result > max) {
		pr_err("%d ns is too big for current clock rate %ld\n", wanted, clk);
		return 0;
	}

	if (result < 1)
		return 1;

	return result;
}

static int s3c6_nfc_setrate(struct s3c_nand *s3c_nand)
{
	struct s3c2410_platform_nand *plat = s3c_nand->pdev->dev.platform_data
	int tacls, twrph0, twrph1;
	unsigned clk_rate
	unsigned set, cfg, mask;

	/* calculate the timing information for the controller */

	clk_rate = clk_get_rate(s3c_nand->clk);
	if (clk_rate > 66000000)	/* refer datasheet */
		s3c_clr_bit(s3c_nand->regs, NFCONF, NFCONF_ECCCLKCON);
	else
		s3c_set_bit(s3c_nand->regs, NFCONF, NFCONF_ECCCLKCON);

	clkrate /= 1000;	/* turn clock into kHz for ease of use */

	if (plat != NULL && (plat->tacls ||  plat->twrph0 || plat->twrph1)) {
		tacls = s3c_nand_calc_rate(plat->tacls, clkrate, 8);
		twrph0 = s3c_nand_calc_rate(plat->twrph0, clkrate, 8);
		twrph1 = s3c_nand_calc_rate(plat->twrph1, clkrate, 8);
	} else {
		/* slowest possible timings as default */
		dev_info(&s3c_nand->pdev.dev, "Falling back to default timings\n");
		tacls = 8;
		twrph0 = 8;
		twrph1 = 8;
	}

	if (tacls < 0 || twrph0 <0 || twrph1 < 0) {
		dev_err(&s3c_nand->pdev.dev, "cannot get suitable timings\n");
		return -EINVAL;
	}

	dev_info(&s3c_nand->pdev.dev, "Tacls=%d, %dns Twrph0=%d %dns, Twrph1=%d %dns\n",
	       tacls, to_ns(tacls, clkrate), twrph0, to_ns(twrph0, clkrate), twrph1, to_ns(twrph1, clkrate));

	mask = (S3C2440_NFCONF_TACLS(7) |
		S3C2440_NFCONF_TWRPH0(7) |
		S3C2440_NFCONF_TWRPH1(7));

	set = S3C2440_NFCONF_TACLS(tacls - 1);
	set |= S3C2440_NFCONF_TWRPH0(twrph0 - 1);
	set |= S3C2440_NFCONF_TWRPH1(twrph1 - 1);
	set |= (1 << 2);	/* refer datasheet */

	cfg = readl(info->regs + NFCONF);
	cfg &= ~mask;
	cfg |= set;
	writel(cfg, info->regs + NFCONF);

	dev_dbg(&s3c_nand->pdev.dev, "NF_CONF is now 0x%lx\n", cfg);

	return 0;
}

static int s3c6_map_nfc_registers(struct platform_device *pdev, struct s3c_nand *s3c_nand)
{
	struct resource *res, *r;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "NFC device without resource!\n");
		return -ENODEV;
	}

	r = devm_request_mem_region(&pdev->dev, res->start, resource_size(res), pdev->name);
	if (r == NULL) {
		dev_err(&pdev->dev, "cannot reserve register region\n");
		return -EBUSY;
	}

	/* use managed devices */
	s3c_nand->regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));

	if (s3c_nand->regs == NULL) {
		dev_err(&pdev->dev, "cannot reserve register region\n");
		return -EIO;
	}

	return 0;
}

static int s3c6_nand_probe(struct device_d *dev)
{
	struct s3c_platform_nand *plat_info = dev->platform_data;
	struct nand_chip *chip;
	int rc = 0;

	if (plat_info == NULL) {
		dev_err(&pdev->dev, "No platform data. Cannot continue\n");
		return -EINVAL;
	}

	/* get the clock source and enable it */
	s3c_nand.clk = clk_get(&pdev->dev, "nand");
	if (IS_ERR(s3c_nand.clk)) {
		dev_err(&pdev->dev, "failed to get clock");
		return -ENOENT;
	}
	clk_enable(s3c_nand.clk);

	rc = s3c_map_nfc_registers(pdev, &s3c_nand);
	if (rc != 0)
		goto exit_registers;

	/* add the run-time links */
	s3c_nand.pdev = pdev;
	s3c_nand.mtd_info.dev.parent = &pdev->dev;
	s3c_nand.mtd_info.priv = &s3c_nand.nand_info;
	s3c_nand.nand_info.IO_ADDR_R = (void *)(s3c_nand.regs + NFDATA);
	s3c_nand.nand_info.IO_ADDR_W = (void *)(s3c_nand.regs + NFDATA);

	/* first scan to identify the attached NAND device(s) */
	rc = nand_scan_ident(&s3c_nand.mtd_info, plat_info->chip_count, NULL);

	/* Spezial-Behandlung der ganzen BBT/OOB/ECC-Problematik */
#ifdef CONFIG_NAND_BBT
	if (!chip->scan_bbt)
		chip->scan_bbt = s3c_default_bbt;
#endif

#if 0
	s3c_setup_nand_type(&s3c_nand);
	nr_parts =
	    parse_mtd_partitions(mtd, part_probes, &host->parts, 0);
	if (nr_parts > 0)
		mtd_device_register(mtd, host->parts, nr_parts);
	else if (pdata->parts)
		mtd_device_register(mtd, pdata->parts, pdata->nr_parts);
	else {
		pr_info("Registering %s as whole device\n", mtd->name);
		mtd_device_register(mtd, NULL, 0);
	}
//	add_mtd_partitions(s3c_mtd, partition_info, plat_info->mtd_part_nr); TODO
#endif

	pr_debug("initialized ok\n");
	return 0;

exit_registers:
	clk_disable(s3c_nand.clk);
	clk_put(s3c_nand.clk);
	return rc;
}

static struct driver_d s3c64xx_nand_driver = {
	.name  = "s3c64xx_nand",
	.probe = s3c6_nand_probe,
};

static int s3c6_nand_init(void)
{
	return register_driver(&s3c64xx_nand_driver);
}
device_initcall(s3c6_nand_init);
