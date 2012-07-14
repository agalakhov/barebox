/*
 * Copyright (C) 2012 Juergen Beisert <jbe@pengutronix.de>
 *
 * This code is partially based on code from:
 *
 *  the FriendlyARM developers Jinsung Yang and Jongpill Lee. They did not add
 *  their copyright note, so I extracted their names from a logfile.
 *
 *  Copyright 2000-2006 Samsung Electronics sc.suh@samsung.com
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
 *
 * The SD/MMC host controller on the S3C64xx SoC is compatible with the
 * "SD Association's (SDA) Host Standard Specification".
 *
 * Due to this, this driver might work on other SoCs witch are using such a
 * host. Check this first to avoid inventing the wheel again and again.
 */

#define DEBUG

#include <common.h>
#include <init.h>
#include <mci.h>
#include <errno.h>
#include <clock.h>
#include <io.h>
#include <mach/s3c-mci.h>
#include <mach/s3c-generic.h>
#include <mach/s3c-iomap.h>

#define SDMASYSAD 0x00	/* u32 */
#define BLKSIZE 0x04	/* u16 */
#define BLKCNT 0x06	/* u16 */
#define ARGMNT 0x08	/* u32 */
#define TRNMOD 0x0c	/* u16 */

#define CMDREG 0x0e	/* u16 */
# define CMDREG_RSPTYP_NONE 0
# define CMDREG_RSPTYP_136B 1
# define CMDREG_RSPTYP_48B 2
# define CMDREG_RSPTYP_48B_BUSY 3
# define CMDREG_ENCMDCRC (1 << 3)
# define CMDREG_ENCMDIDC (1 << 4)
# define CMDREG_DATAPRNT (1 << 5)
# define CMDREG_CMDTYP_OTHER (0 << 6)
# define CMDREG_CMDTYP_SUS_CMD52 (1 << 6)
# define CMDREG_CMDTYP_RES_CMD52 (2 << 6)
# define CMDREG_CMDTYP_IOABRT (3 << 6)
# define CMDREG_CMDIDX(x) (((x) & 0x3f) << 8)

#define RSPREG0 0x10	/* u32 */
#define RSPREG1 0x14	/* u32 */
#define RSPREG2 0x18	/* u32 */
#define RSPREG3 0x1c	/* u32 */
#define BDATA 0x20	/* u32 */

#define PRNSTS 0x24	/* u32 */
# define PRNSTS_TCMD (1 << 24)
# define PRNSTS_GET_TDAT(x) (((x) >> 20) & 0xf)
# define PRNSTS_TCD (1 << 18)
# define PRNSTS_STBLCARD (1 << 17)
# define PRNSTS_INSCARD (1 << 16)
# define PRNSTS_BUFRDRDY (1 << 11)
# define PRNSTS_BUFWTRDY (1 << 10)

#define HOSTCTL 0x28	/* u8 */
# define HOSTCTL_WIDE4 (1 << 1)
# define HOSTCTL_ENHIGHSPD (1 << 2)
# define HOSTCTL_DMASEL (1 << 3) /* 2 bits */
# define HOSTCTL_WIDE8 (1 << 5) /* 2 bits */

#define PWRCON 0x29	/* u8 */
#define BLKGAP 0x2A	/* u8 */
#define WAKCON 0x2B	/* u8 */
#define CLKCON 0x2c	/* u16 */
# define CLKCON_SELFREQ(x) ((x) << 8)
# define CLKCON_ENSDCLK (1 << 2)
# define CLKCON_STBLINTCLK (1 << 1)
# define CLKCON_ENINTCLK (1 << 0)
#define TIMEOUTCON 0x2e	/* u8 */
#define SWRST 0x2f	/* u8 */
# define SWRST_RSTDAT (1 << 2)
# define SWRST_RSTCMD (1 << 1)
# define SWRST_RSTALL (1 << 0)
#define NORINTSTS 0x30	/* u16 */
#define ERRINTSTS 0x32	/* u16 */
#define CAPAREG 0x40	/* u32 */
#define CONTROL2 0x80	/* u32 */
# define CONTROL2_ENSTAASYNCCLR (1 << 31)
# define CONTROL2_ENCMDCNFMSK (1 << 30)
# define CONTROL2_CDINVRXD3 (1 << 29)
# define CONTROL2_ENFBCLKRX (1 << 14)
# define CONTROL2_ENCLKOUTHOLD (1 << 8)
# define CONTROL2_SELBASECLK(x) ((x) << 4)
# define CONTROL2_ENCLKOUTMSKCON (1 << 1)
# define CONTROL2_HWINITFIN (1 << 0)
#define CONTROL3 0x84	/* u32 */
# define CONTROL3_FCSEL3 (1 << 31)
# define CONTROL3_FIA3(x) ((x) << 24)
# define CONTROL3_FCSEL2 (1 << 23)
# define CONTROL3_FIA2(x) ((x) << 16)
# define CONTROL3_FCSEL1 (1 << 15)
# define CONTROL3_FIA1(x) ((x) << 8)
# define CONTROL3_FCSEL0 (1 << 7)
# define CONTROL3_FIA0(x) (x)
#define CONTROL4 0x8C	/* u32 */
# define CONTROL4_SELCLKPADDS(x) ((x) << 16)
# define CONTROL4_STABUSY (1 << 0)
#define HCVER 0xfe	/* u16 */
# define HCVER_GET_VENVER(x) ((x) >> 8)
# define HCVER_GET_SPECVER(x) ((x) & 0xff)

struct sda_mci {
	struct mci_host mci;
	void __iomem *base;
	int bus_width:2; /* 0 = 1 bit, 1 = 4 bit, 2 = 8 bit */
	unsigned clock;	/* current clock in Hz */
	/* from platform data */
	unsigned base_clk_sel:2;
	unsigned pin_strength:2;
};

#define to_sda_mci(mci) container_of(mci, struct sda_mci, mci)

/*
 * Clock path of this unit:
 * - base clock is the SoC's HCLK
 * - this clock is delivered to the HC, can be switched with ENINTCLK/CLOCKCON
 * - the clock to the attached SD card can be switched with ENSDCLK/CLOCKON
 * - the clock frequency to the attached SD card can be selected with SELFREQ/CLOCKON
 */
/* ------------------------ SoC specific part ----------------------------- */

static void s3c6_setup_pin_strength(struct sda_mci *this)
{
	struct device_d *dev = this->mci.hw_dev;
	/*
	 * drive strength can only be set for channel 0 and 1. Channel 2 is
	 * special
	 */
	if (this->base == (void __iomem *)S3C_SDI2_BASE)
		dev_err(dev, "Cannot setup pin drive strength for channel 2 yet\n");
	else
		writel(CONTROL4_SELCLKPADDS(this->pin_strength), this->base + CONTROL4);
}

static unsigned s3c6_get_base_clk(struct sda_mci *this)
{
	struct device_d *dev = this->mci.hw_dev;

	switch (this->base_clk_sel) {
	case 0:
	case 1:
		return s3c_get_hclk();
	case 2:
		return s3c_get_epllclk();
	default:
		dev_err(dev, "External SDHCI clock not supported\n");
		return 0;
	}
}

/* ------------------------ SDA generic part ------------------------------ */

/* enable the clock to the external SD/MMC card */
static void sda_enable_SD_clock(struct sda_mci *this)
{
	uint16_t reg;

	reg = readw(this->base + CLKCON);
	writew(reg | CLKCON_ENSDCLK, this->base + CLKCON);
}

/* disable the clock to the external SD/MMC card */
static void sda_disable_SD_clock(struct sda_mci *this)
{
	uint16_t reg;

	reg = readw(this->base + CLKCON) & ~CLKCON_ENSDCLK;
	writew(reg, this->base + CLKCON);
}

static void sda_setup_interface(struct sda_mci *this, unsigned clksrc)
{
	u32 reg32;

	this->base_clk_sel = clksrc;

	/* FIXME "basic delay + 2ns" (???????) sounds platform specific! */
	reg32 = CONTROL2_ENSTAASYNCCLR |
		CONTROL2_ENCMDCNFMSK |
		CONTROL2_ENFBCLKRX |
		CONTROL2_ENCLKOUTHOLD |
		CONTROL2_SELBASECLK(clksrc);
	writel(reg32, this->base + CONTROL2);

	s3c6_setup_pin_strength(this);
}

/* setup HC internal and external clock */
static void sda_setup_clock(struct sda_mci *this, unsigned div)
{
	struct device_d *dev = this->mci.hw_dev;
	uint16_t reg;
	uint64_t start;
	static const uint64_t toffs = 1 * SECOND;

	/* setup HC local clock settings and enable engine's clock */
	writew(CLKCON_SELFREQ(div) | CLKCON_ENINTCLK, this->base + CLKCON);

	start = get_time_ns();
	do {
		reg = readw(this->base + CLKCON);
		if (reg & CLKCON_STBLINTCLK) {
			sda_enable_SD_clock(this);
			return;
		}
	}  while (!is_timeout(start, toffs));

	dev_warn(dev, "internal clock stabilization failed\n");
}

/* define a new clock frequency in [Hz] */
static unsigned sda_setup_clock_speed(struct sda_mci *this, unsigned freq)
{
	struct device_d *dev = this->mci.hw_dev;
	unsigned u, bclk, tclk;

	bclk = s3c6_get_base_clk(this);
	dev_dbg(dev, "%s: Staring with %d Hz.\n", __func__, bclk);

	if (freq > bclk) {
		sda_setup_clock(this, 0);
		return bclk;
	}

	/* calculate a dividor according to manual page 993 (SELFREQ) */
	for (u = 1; u <= 0x80; u <<= 1) {
		tclk = bclk / (u << 1);
		if (freq > tclk) {
			sda_setup_clock(this, u);
			dev_dbg(dev, "Using %d Hz instead of %d\n", tclk, freq);
			return tclk;
		}
	}

	dev_err(dev, "Used physical clock is too high. Trying to continue\n");
	return tclk;
}

static int sda_mci_initialize(struct sda_mci *this)
{
	struct device_d *dev = this->mci.hw_dev;
	uint32_t reg;
	static const uint64_t toffs = 1 * SECOND;
	uint64_t start = get_time_ns();

	writeb(SWRST_RSTCMD | SWRST_RSTALL, this->base + SWRST);

	/* TODO, we can also check SWRST_RSTALL == 0 for reset finished */
	do {
		reg = readl(this->base + CONTROL2);
		if (reg & CONTROL2_HWINITFIN)
			return 0;
	} while(!is_timeout(start, toffs));

	dev_err(dev, "Cannot reset the SD/MMC controller\n");
	return -ETIMEDOUT;
}

static int sda_wait_buffer_write_ready(struct sda_mci *this)
{
	static const uint64_t toffs = 1 * SECOND;
	uint64_t start = get_time_ns();
	uint32_t reg;

	do {
		reg = readl(this->base + PRNSTS);
		if (reg & PRNSTS_BUFWTRDY)
			return 0;
	} while (!is_timeout(start, toffs));

	return -ETIMEDOUT;
}

static int sda_mci_write_block(struct sda_mci *this, struct mci_data *data)
{
	const uint32_t *p;
	unsigned cnt, data_size;
	int rc;

	p = (const uint32_t*)data->src;
	data_size = data->blocksize * data->blocks;

	while (data_size > 0) {
		/* wait for free entries in the FIFO */
		rc = sda_wait_buffer_write_ready(this);
		if (rc != 0)
			return rc;

		cnt = 512; /* FIXME must we always write a full block? */
		while (cnt--) {
			writel(*p++, this->base + BDATA);
			if (data_size > 4)
				data_size -= 4;
			else {
				data_size = 0;
				break;
			}
		}
	}

	return 0;
}

static int sda_wait_buffer_read_ready(struct sda_mci *this)
{
	static const uint64_t toffs = 1 * SECOND;
	uint64_t start = get_time_ns();
	uint32_t reg;

	do {
		reg = readl(this->base + PRNSTS);
		if (reg & PRNSTS_BUFRDRDY)
			return 0;
	} while (!is_timeout(start, toffs));

	return -ETIMEDOUT;
}

static int sda_mci_read_block(struct sda_mci *this, struct mci_data *data)
{
	uint32_t *p;
	unsigned cnt, data_size;
	int rc;

	p = (uint32_t*)data->dest;
	data_size = data->blocksize * data->blocks;

	while (data_size > 0) {
		/* wait for the data of one block */
		rc = sda_wait_buffer_read_ready(this);
		if (rc != 0)
			return rc;

		cnt = 512; /* FIXME can we also read blocks partially??? */
		while (cnt--) {
			*p++ = readl(this->base + BDATA);
			if (data_size > 4)
				data_size -= 4;
			else {
				data_size = 0;
				break;
			}
		}
	}

	return 0;
}

static int sda_mci_std_cmds(struct sda_mci *this, struct mci_cmd *cmd)
{
	int rc;

	rc = sda_prepare_engine(this);
	if (rc != 0)
		return 0;

	return sda_send_command(this, cmd, NULL);
}

static int sda_mci_adtc(struct sda_mci *this, struct mci_cmd *cmd, struct mci_data *data)
{
	int rc;

	rc = sda_prepare_engine(this);
	if (rc != 0)
		return rc;

	rc = sda_prepare_data_transfer(this, data);
	if (rc != 0)
		return rc;

	if (data->flags & MMC_DATA_READ) {
		sda_send_command(this, cmd, data);
		rc = sda_mci_read_block(this, data);
		if (rc == 0) {
			while (!(readl(this->base + SDIDSTA) & SDIDSTA_XFERFINISH))
				;
		} else
			sda_terminate_transfer(this);
	}

	if (data->flags & MMC_DATA_WRITE) {
		sda_send_command(this, cmd, data);
		rc = sda_mci_write_block(this, data);
		if (rc == 0) {
			while (!(readl(this->base + SDIDSTA) & SDIDSTA_XFERFINISH))
				;
		} else
			sda_terminate_transfer(this);
	}
	writel(0, host_data->base + SDIDCON);

	return rc;
}

/* ------------------------- MCI API -------------------------------------- */

static int sda_mci_reset(struct mci_host *mci, struct device_d *mci_dev)
{
	struct sda_mci *this = to_sda_mci(mci);

	return sda_mci_initialize(this);
}

static int sda_mci_request(struct mci_host *mci, struct mci_cmd *cmd, struct mci_data *data)
{
	struct sda_mci *this = to_sda_mci(mci);
	int rc;

	sda_enable_SD_clock(this);

	if ((cmd->resp_type == 0) || (data == NULL))
		rc = sda_mci_std_cmds(this, cmd);
	else
		rc = sda_mci_adtc(this, cmd, data);	/* with response and data */

	sda_finish_request(this);

	sda_disable_SD_clock(this);
	return rc;
}

/* change transfer clock speed and/or data bus width */
static void sda_mci_set_ios(struct mci_host *mci, struct mci_ios *ios)
{
	struct sda_mci *this = to_sda_mci(mci);
	u8 reg;

	/* data bus width */
	reg = readb(this->base + HOSTCTL) & ~(HOSTCTL_WIDE4 | HOSTCTL_WIDE8);
	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		break;
	case MMC_BUS_WIDTH_4:
		reg |= HOSTCTL_WIDE4;
		break;
	case MMC_BUS_WIDTH_8:
		reg |= HOSTCTL_WIDE8;
		break;
	}
	writeb(reg, this->base + HOSTCTL);

	/* TODO consider the ENHIGHSPD bit in HOSTCTL for up to 50 MHz */
	this->clock = sda_setup_clock_speed(this, ios->clock);
}

/* ----------------------------------------------------------------------- */

static void __maybe_unused sda_info(struct device_d *dev)
{
	struct sda_mci *this = dev->priv;
	struct s3c_mci_platform_data *pd = dev->platform_data;

	printf("  Bus data width: %d bit\n", this->bus_width == 1 ? 4 : 1);
	printf("  Bus frequency: %u Hz\n", this->clock);
	printf("   Frequency limits: ");
	if (pd->f_min == 0)
		printf("no lower limit ");
	else
		printf("%u Hz lower limit ", pd->f_min);
	if (pd->f_max == 0)
		printf("- no upper limit");
	else
		printf("- %u Hz upper limit", pd->f_max);
	printf("\n  Card detection support: %s\n",
		pd->gpio_detect != 0 ? "yes" : "no");
}

static void sda_print_ver(struct sda_mci *this)
{
	uint32_t reg;

	reg = readl(this->base + HCVER);
	pr_info("SDMMC %u.0 HC, spec %u.0\n", HCVER_GET_VENVER(reg), HCVER_GET_SPECVER(reg));
}

static int sda_mci_probe(struct device_d *dev)
{
	struct s3c_mci_platform_data *pd = dev->platform_data;
	struct sda_mci *this;

	if (pd == NULL) {
		dev_err(dev, "Missing platform data\n");
		return -EINVAL;
	}

	this = xzalloc(sizeof(struct sda_mci));

	dev->priv = &this;

	this->mci.send_cmd = sda_mci_request;
	this->mci.set_ios = sda_mci_set_ios;
	this->mci.init = sda_mci_reset;
	this->base = dev_request_mem_region(dev, 0);
	this->mci.hw_dev = dev;

	/* feed forward the platform specific values */
	this->mci.voltages = pd->voltages;
	this->mci.host_caps = pd->caps;
	this->base_clk_sel = pd->clk_src;
	this->mci.f_min = pd->f_min == 0 ? s3c6_get_base_clk(this) / 256 : pd->f_min;
	this->mci.f_max = pd->f_max == 0 ? s3c6_get_base_clk(this) / 2 : pd->f_max;

	/*
	 * Start the clock to let the engine and the card finishes its startup
	 */
	this->clock = sda_setup_clock_speed(this, this->mci.f_min);
	writel(SDICON_FIFORESET | SDICON_MMCCLOCK, this->base + SDICON);
	sda_setup_interface(this, pd->pin_strength);

	sda_print_ver(this);
	return mci_register(&this->mci);
}

static struct driver_d sda_mci_driver = {
        .name  = "sda_mci",
        .probe = sda_mci_probe,
#ifdef CONFIG_MCI_INFO
	.info = sda_info,
#endif
};

static int sda_mci_init_driver(void)
{
        register_driver(&sda_mci_driver);
        return 0;
}

device_initcall(sda_mci_init_driver);
