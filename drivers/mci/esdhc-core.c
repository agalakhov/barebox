/*
 * Copyright (C) 2012 Juergen Beisert
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
 */

#define DEBUG

#include <common.h>
#include <init.h>
#include <clock.h>
#include <gpio.h>
#include <mci.h>
#include <mci/esdhc.h>
#include <errno.h>
#include <clock.h>
#include <io.h>

#include "esdhc-core.h"

#define to_esdhc(mci) container_of(mci, struct esdhc_mci, mci)

/* functions from the platform adaption part */

unsigned esdhcp_get_base_clk(struct esdhc_mci *);

/* ------------------------ SDA generic part ------------------------------ */

/* enable the clock to the _external_ SD/MMC card */
static void esdhc_enable_SD_clock(struct esdhc_mci *h)
{
	uint16_t reg = readw(h->base + CLKCON);
	writew(reg | CLKCON_ENSDCLK, h->base + CLKCON);
	pr_debug("SD Clock enabled\n");
	/* TODO check bit 4: it will be set, when the SD clock is active */
	/* note: after setting bit 3 it needs some time to activate the SD clock! But it seems very fast */
}

/* disable the clock to the _external_ SD/MMC card */
static void esdhc_disable_SD_clock(struct esdhc_mci *h)
{
	uint16_t reg = readw(h->base + CLKCON) & ~CLKCON_ENSDCLK;
	writew(reg, h->base + CLKCON);
	pr_debug("SD Clock disabled\n");
	/* TODO check bit 4: it will be cleared, when the SD clock is inactive */
	/* note: after clearing bit 3 it needs some time to disable the SD clock! It seems very slow! 1 second */
}

/* return 0=card present, else: error code */
static int esdhc_card_present(struct esdhc_mci *h)
{
	int rc;

	switch (h->pdata->cd_type) {
	case ESDHC_CD_NONE:
		return -ENOSYS;	/* TODO */

	case ESDHC_CD_PERMANENT:
		/* assume its present */
		break;

	case ESDHC_CD_CONTROLLER:
		rc = wait_on_timeout(1 * SECOND, readl(h->base + PRNSTS) & PRNSTS_STBLCARD);
		if (rc != 0)
			return rc;
		if (!(readl(h->base + PRNSTS) & PRNSTS_INSCARD))
			return -ENODEV;
		break;

	case ESDHC_CD_GPIO:
		rc = gpio_direction_input(h->pdata->cd_gpio);
		if (rc != 0)
			return rc;
		if (gpio_get_value(h->pdata->cd_gpio))
			return -ENODEV;
		break;

	case ESDHC_CD_GPIO_INV:
		rc = gpio_direction_input(h->pdata->cd_gpio);
		if (rc != 0)
			return rc;
		if (!gpio_get_value(h->pdata->cd_gpio))
			return -ENODEV;
		break;
	}

	return 0;
}

/* return 0=card writeable, else: error code */
static int esdhc_card_protected(struct esdhc_mci *h)
{
	int rc;

	switch (h->pdata->wp_type) {
	case ESDHC_WP_CONTROLLER:
	case ESDHC_WP_NONE:
		return -ENOSYS;	/* TODO */

	case ESDHC_WP_PERMANENT:
		break;

	case ESDHC_WP_GPIO:
		rc = gpio_direction_input(h->pdata->wp_gpio);
		if (rc != 0)
			return rc;
		if (!gpio_get_value(h->pdata->wp_gpio))
			return -EROFS;
		break;

	case ESDHC_WP_GPIO_INV:
		rc = gpio_direction_input(h->pdata->wp_gpio);
		if (rc != 0)
			return rc;
		if (gpio_get_value(h->pdata->wp_gpio))
			return -EROFS;
		break;
	}

	return 0;
}

static void esdhc_speed_mode_set(struct esdhc_mci *h, unsigned freq)
{
	u8 reg = readb(h->base + HOSTCTL) & ~HOSTCTL_ENHIGHSPD;
	/*
	 * enable HC's high speed mode, when a card clock
	 * above 25 MHz should be used
	 */
	if (freq > 25000000)
		reg |= HOSTCTL_ENHIGHSPD;

	writeb(reg, h->base + HOSTCTL);
}

/*
 * Clock feedback control: FIXME check documentation in the SDA manual
 *
 * Rx: FCSEL0 + FCSEL1
 *  - 00 inverter delay (=10ns @ 50 MHz, FIXME and @ <50 MHz?)
 *  - 01 basic delay
 *  - 10 inverter delay + 2ns (=12ns @ 50 MHz)
 *  - 11 basic delay + 2ns
 *
 * clk source --------o--------------------------------o--------> SD/MMC card
 *                    |                                |
 * feedback <---------O----------O---------------------o
 *                    ^          ^                     |
 *                ENFBCLKTX      O-- 2ns -O------------o
 *                               |        ^            |
 *                               +--------O- inverter -+
 */
static void esdhc_tx_delay_set(struct esdhc_mci *h, unsigned val)
{
	u32 reg = readl(h->base + CONTROL3) & ~(CONTROL3_FCSEL2 | CONTROL3_FCSEL3);

	if (val & 0x01)
		reg |= CONTROL3_FCSEL2;
	if (val & 0x02)
		reg |= CONTROL3_FCSEL3;

	writel(reg, h->base + CONTROL3);
}

/*
 * Clock feedback control: FIXME check documentation in the SDA manual
 *
 * Rx: FCSEL0 + FCSEL1
 *  - 00 inverter delay (=10ns @ 50 MHz, FIXME and @ <50 MHz?)
 *  - 01 basic delay
 *  - 10 inverter delay + 2ns (=12ns @ 50 MHz)
 *  - 11 basic delay + 2ns
 *
 * clk source --------o--------------------------------o--------> SD/MMC card
 *                    |                                |
 * feedback <---------O----------O---------------------o
 *                    ^          ^                     |
 *                ENFBCLKRX      O-- 2ns -O------------o
 *                               |        ^            |
 *                               +--------O- inverter -+
 */
static void esdhc_rx_delay_set(struct esdhc_mci *h, unsigned val)
{
	u32 reg = readl(h->base + CONTROL3) & ~(CONTROL3_FCSEL0 | CONTROL3_FCSEL1);

	if (val & 0x01)
		reg |= CONTROL3_FCSEL0;
	if (val & 0x02)
		reg |= CONTROL3_FCSEL1;

	writel(reg, h->base + CONTROL3);
}

/* setup the physical features of the interface */
static void esdhc_interface_setup(struct esdhc_mci *h, unsigned freq)
{
	u32 reg;

	/* TODO get the values from the platform */
	reg = CONTROL2_ENCLKOUTHOLD |
		CONTROL2_DEFCNT(2) |
		CONTROL2_ENSTAASYNCCLR |
		CONTROL2_ENCMDCNFMSK |
		CONTROL2_SELBASECLK(h->pdata->clk_src);

	if (freq > 25000000) {
		pr_debug("Setting up the delays for frequencies above 25 MHz\n");
		if (h->pdata->rx_delay_en25)
			reg |= CONTROL2_ENFBCLKRX;
		if (h->pdata->tx_delay_en25)
			reg |= CONTROL2_ENFBCLKTX;
		esdhc_tx_delay_set(h, h->pdata->tx_delay25);
		esdhc_rx_delay_set(h, h->pdata->rx_delay25);
	} else {
		pr_debug("Setting up the delays for frequencies below 25 MHz\n");
		if (h->pdata->rx_delay_en)
			reg |= CONTROL2_ENFBCLKRX;
		if (h->pdata->tx_delay_en)
			reg |= CONTROL2_ENFBCLKTX;
		esdhc_tx_delay_set(h, h->pdata->tx_delay);
		esdhc_rx_delay_set(h, h->pdata->rx_delay);
	}

	writel(reg, h->base + CONTROL2);

	/*
	 * Pay attention: at least at the S3C6410 SoC the third channel
	 * cannot be set in this way!
	 */
	writel(CONTROL4_SELCLKPADDS(h->pdata->pin_strength), h->base + CONTROL4);
}

/* setup HC internal and external clock */
static void esdhc_clock_setup(struct esdhc_mci *h, unsigned div)
{
	struct device_d *dev = h->mci.hw_dev;
	int rc;

	/* stop the clock prior changing its frequency */
	esdhc_disable_SD_clock(h);

	/* setup HC local clock settings and enable engine's clock */
	writew(CLKCON_SELFREQ(div) | CLKCON_ENINTCLK, h->base + CLKCON);

	rc = wait_on_timeout(1 * SECOND, readw(h->base + CLKCON) & CLKCON_STBLINTCLK);
	if (rc == 0)
		esdhc_enable_SD_clock(h);
	else
		dev_warn(dev, "internal clock stabilization failed\n");
}

/* define a new clock frequency in [Hz] */
static unsigned esdhc_clock_setup_speed(struct esdhc_mci *h, unsigned freq)
{
	struct device_d *dev = h->mci.hw_dev;
	unsigned u, bclk, tclk;

	if (freq == 0)
		return 0;

	dev_dbg(dev, "%s: Should set %u Hz.\n", __func__, freq);

	bclk = esdhcp_get_base_clk(h);
	dev_dbg(dev, "%s: Starting with %d Hz.\n", __func__, bclk);

	if (freq > bclk) {
		esdhc_clock_setup(h, 0);
		return bclk;
	}

	/* 0x80 wird zu 519 kHz
	 * 0x40 wird zu 1 MHz
	 * 0x20 wird zu 2 MHz
	 * 0x10 wird zu 4 MHz
	 * 0x08 wird zu 8 MHz
	 * 0x04 wird zu 16 MHz
	 * 0x02 wird zu 33 MHz
	 * 0x01 wird zu 66 MHz
	 *
	 * es macht keinen Unterschied zwischen SELBASECLK 0 und 1
	 * 2 wird zu 93,7 kHz bei 0x80 was zu 24 MHz EPLL korrespondiert (zuvor gemessen an DOUT)
	 * 3 -> SD Takt ist aus!
	 */
	/* calculate a divider according to manual page 993 (SELFREQ) */
	for (u = 1; u <= 0x80; u <<= 1) {
		tclk = bclk / (u << 1);
// 		pr_debug("%s: trying divider %u which results into %u Hz\n", __func__, u << 1, tclk);
		if (freq >= tclk) {
			esdhc_speed_mode_set(h, tclk);
			esdhc_interface_setup(h, tclk);
			esdhc_clock_setup(h, u);
			dev_dbg(dev, "Using %d Hz instead of %d\n", tclk, freq);
			return tclk;
		}
	}

	dev_err(dev, "Used physical clock is too high. Trying to continue\n");
	return tclk;
}

static int esdhc_reset_wait_done(struct esdhc_mci *h)
{
	struct device_d *dev = h->mci.hw_dev;
	int rc;

	rc = wait_on_timeout(1 * SECOND, !(readl(h->base + CONTROL2) & CONTROL2_HWINITFIN));
	if (rc != 0)
		dev_err(dev, "Cannot reset the ESDHC\n");

	return rc;
}

static int esdhc_initialize(struct esdhc_mci *h)
{
	int rc;

	/* reset the engine */
	writeb(SWRST_RSTCMD | SWRST_RSTALL, h->base + SWRST);
	rc = esdhc_reset_wait_done(h);
	if (rc != 0)
		return rc;

	/* TODO right? */
	writeb(0xe, h->base + PWRCON);	/* Set 3.3 V */
	writeb(0xf, h->base + PWRCON);	/* enable power */

	return 0;
}

/* setup data transfer related register */
static int esdhc_prepare_data_transfer(struct esdhc_mci *h, struct mci_data *data)
{
	u16 trnmode = TRNMOD_ENBLKCNT;

	writeb(0x0e, h->base + TIMEOUTCON); /* TODO 2^27? */

	if (data->blocks > 1)
		trnmode | TRNMOD_MUL1SIN0;
	if (data->flags == MMC_DATA_READ)
		trnmode |= TRNMOD_RD1WT0;

	/* sdhci_writew(host, SDHCI_MAKE_BLKSZ(SDHCI_DEFAULT_BOUNDARY_ARG, data->blocksize), SDHCI_BLOCK_SIZE); */
	writew(0x200, h->base + BLKSIZE); /* TODO */

	/* sdhci_writew(host, data->blocks, SDHCI_BLOCK_COUNT); */
	writew(data->blocks, h->base + BLKCNT);

	/* sdhci_writew(host, mode, SDHCI_TRANSFER_MODE); */
	writew(trnmode, h->base + TRNMOD);

	return 0;
}

static int esdhc_terminate_transfer(struct esdhc_mci *h)
{
	return -EINVAL;
}

static unsigned esdhc_get_cmd_flags(unsigned cmd_flags, unsigned data_flags)
{
	unsigned reg = CMDREG_RSPTYP_NONE;

	if (cmd_flags & MMC_RSP_136) {
		reg |= CMDREG_RSPTYP_136B;
		pr_debug("Command with long response\n");
	}

	if (cmd_flags & MMC_RSP_PRESENT) {
		reg |= CMDREG_RSPTYP_48B;
		pr_debug("Command with response\n");
	}

	if (cmd_flags & MMC_RSP_BUSY) {
		reg |= CMDREG_RSPTYP_48B_BUSY;
		pr_debug("Command with response & busy\n");
	}

	if (cmd_flags & MMC_RSP_CRC) {
		reg |= CMDREG_ENCMDCRC;
		pr_debug("Command with CRC response\n");
	}

	if (cmd_flags & MMC_RSP_OPCODE) {
		reg |= CMDREG_ENCMDIDC;
		pr_debug("Command with FIXME\n");
	}

	if (data_flags != 0) {
		reg |= CMDREG_DATAPRNT;
		pr_debug("Command with data\n");
	}

	return reg;
}

/* wait until the card accepts the next command */
static int esdhc_wait_for_command_ready(struct esdhc_mci *h, u32 mask)
{
	int rc = wait_on_timeout(1 * SECOND, !(readl(h->base + PRNSTS) & mask));
	if (rc != 0)
		pr_debug("Waiting for SDHCI timed out\n");
	return rc;
}

/* wait until the current commend is done */
static int esdhc_cmd_wait_done(struct esdhc_mci *h)
{
	u16 reg, err;
	uint64_t start;
	static const uint64_t toffs = 1 * SECOND;

	start = get_time_ns();
	do {
		reg = readw(h->base + NORINTSTS);
		if (reg & NORINTSTS_STAERR)
			break;
		if (reg & NORINTSTS_STACMDCMPLT)
			return 0;
	} while (!is_timeout(start, toffs));

	err = readw(h->base + ERRINTSTS);
	/* clear the error flags */
	writew(err, h->base + ERRINTSTS);
	writew(reg, h->base + NORINTSTS);
	printf("%s command error: 0x%04hx -> 0x%04hx\n", __func__, reg, err);
	return -ETIMEDOUT;
}

static int esdhc_transfer_wait_done(struct esdhc_mci *h)
{
	return wait_on_timeout(1 * SECOND, readw(h->base + NORINTSTS) & NORINTSTS_STATRANCMPLT);
}

/* issue a command, wait untils it's done and read the response */
static int esdhc_cmd_send(struct esdhc_mci *h, struct mci_cmd *cmd, struct mci_data *data)
{
	int rc;
	u32 mask = PRNSTS_CMDINHCMD, resp[4];
	u16 reg;

	pr_debug("  %s called\n", __func__);

	if (cmd->cmdidx != MMC_CMD_STOP_TRANSMISSION)
		mask |= PRNSTS_CMDINHDAT;

	rc = esdhc_wait_for_command_ready(h, mask);
	if (rc != 0)
		return rc;

	reg = esdhc_get_cmd_flags(cmd->resp_type, data != NULL ? data->flags : 0);

	if (cmd->cmdidx == MMC_CMD_STOP_TRANSMISSION)
		reg |= CMDREG_CMDTYP_IOABRT;

	reg |= CMDREG_CMDIDX(cmd->cmdidx);

	pr_debug("  %s writing argument: %X\n", __func__, cmd->cmdarg);
	writel(cmd->cmdarg, h->base + ARGMNT);
	pr_debug("  %s writing command: %X\n", __func__, reg);
	writew(reg, h->base + CMDREG); /* issue the command after all data */

	rc = esdhc_cmd_wait_done(h);
	if (rc != 0)
		return rc;

	cmd->response[0] = resp[0] = readl(h->base + RSPREG0);
	if (cmd->resp_type & MMC_RSP_136) {
		resp[1] = readl(h->base + RSPREG1);
		resp[2] = readl(h->base + RSPREG2);
		resp[3] = readl(h->base + RSPREG3);
		/* shift this data a little bit around */
		cmd->response[0] = (resp[3] << 8) | (resp[2] >> 24);
		cmd->response[1] = (resp[2] << 8) | (resp[1] >> 24);
		cmd->response[2] = (resp[1] << 8) | (resp[0] >> 24);
		cmd->response[3] = (resp[0] << 8);
	}

	pr_debug("  %s left regular\n", __func__);
	return 0;
}

/* wait until the data is written from the internal FIFO to the card */
static int esdhc_wait_buffer_write_ready(struct esdhc_mci *h)
{
	return wait_on_timeout(1 * SECOND, !(readl(h->base + PRNSTS) & PRNSTS_BUFWTRDY));
}

/* transfer data from host to card */
static int esdhc_write_pio(struct esdhc_mci *h, struct mci_data *data)
{
	const u32 *p;
	unsigned cnt, data_size;
	int rc;

	p = (const u32*)data->src;
	data_size = data->blocksize * data->blocks;

	while (data_size > 0) {
		/* wait for free entries in the FIFO */
		rc = esdhc_wait_buffer_write_ready(h);
		if (rc != 0)
			return rc;

		cnt = 512; /* FIXME must we always write a full block? */
		while (cnt--) {
			writel(*p++, h->base + BDATA);
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

/* wait until the data is read in from the card into the internal FIFO */
static int esdhc_wait_buffer_read_ready(struct esdhc_mci *h)
{
	return wait_on_timeout(1 * SECOND, !(readl(h->base + PRNSTS) & PRNSTS_BUFRDRDY));
}

/* transfer data from card to host */
static int esdhc_read_pio(struct esdhc_mci *h, struct mci_data *data)
{
	u32 *p;
	unsigned cnt, data_size;
	int rc;

	p = (u32*)data->dest;
	data_size = data->blocksize * data->blocks;

	while (data_size > 0) {
		/* wait for the data of one block */
		rc = esdhc_wait_buffer_read_ready(h);
		if (rc != 0)
			return rc;

		cnt = 512; /* FIXME can we also read blocks partially??? */
		while (cnt--) {
			*p++ = readl(h->base + BDATA);
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

static int esdhcp_data_cmds(struct esdhc_mci *h, struct mci_cmd *cmd, struct mci_data *data)
{
	int rc;

	rc = esdhc_prepare_data_transfer(h, data);
	if (rc != 0)
		return rc;

	if (data->flags & MMC_DATA_READ) {
		/* command phase */
		rc = esdhc_cmd_send(h, cmd, data);
		if (rc != 0)
			return rc;

		/* data phase */
		rc = esdhc_read_pio(h, data);
		if (rc == 0)
			/* wait until it's done */
			rc = esdhc_transfer_wait_done(h);
		else
			esdhc_terminate_transfer(h);
	}

	if (data->flags & MMC_DATA_WRITE) {
		rc = esdhc_card_protected(h);
		if (rc != 0)
			return rc;

		/* command phase */
		rc = esdhc_cmd_send(h, cmd, data);
		if (rc != 0)
			return rc;

		/* data phase */
		rc = esdhc_write_pio(h, data);
		if (rc == 0)
			/* wait until it's done */
			rc = esdhc_transfer_wait_done(h);
		else
			esdhc_terminate_transfer(h);
	}

	return rc;
}

/* ------------------------- MCI API -------------------------------------- */

static int esdhc_reset(struct mci_host *mci, struct device_d *mci_dev)
{
	struct esdhc_mci *h = to_esdhc(mci);

	return esdhc_initialize(h);
}

static int esdhc_request(struct mci_host *mci, struct mci_cmd *cmd, struct mci_data *data)
{
	struct esdhc_mci *h = to_esdhc(mci);
	int rc;

	pr_debug("%s called\n", __func__);
// 	esdhc_enable_SD_clock(h);

	rc = esdhc_card_present(h);
	if (rc == 0) {
		if ((cmd->resp_type == 0) || (data == NULL)) {
			pr_debug(" %s: Send command only\n", __func__);
			rc = esdhc_cmd_send(h, cmd, NULL);
		} else {
			pr_debug(" %s: Send command with data\n", __func__);
			rc = esdhcp_data_cmds(h, cmd, data);	/* with response and data */
		}
	}

// 	esdhc_disable_SD_clock(h);
	pr_debug("%s left\n", __func__);
	return rc;
}

/* change transfer clock speed and/or data bus width */
static void esdhc_set_ios(struct mci_host *mci, struct mci_ios *ios)
{
	struct esdhc_mci *h = to_esdhc(mci);
	u8 reg;

	pr_debug("%s called\n", __func__);
	/* data bus width */
	reg = readb(h->base + HOSTCTL) & ~(HOSTCTL_WIDE4 | HOSTCTL_WIDE8);
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
	writeb(reg, h->base + HOSTCTL);
	h->bus_width = ios->bus_width;

	if (ios->clock == 0)
		printf("Should switch off the clock\n");
	else
		h->clock = esdhc_clock_setup_speed(h, ios->clock);
	pr_debug("%s left\n", __func__);
}

/* ----------------------------------------------------------------------- */

#ifdef CONFIG_MCI_INFO
static void esdhc_print_ver(struct esdhc_mci *h)
{
	u16 reg;

	reg = readw(h->base + HCVER);
	printf("ESDHC %u.0 HC, spec %u.0\n", HCVER_GET_VENVER(reg), HCVER_GET_SPECVER(reg));
}

static void esdhc_print_caps(struct esdhc_mci *h)
{
	u32 reg;

	reg = readl(h->base + CAPAREG);
	printf("  Bus voltages:\n");
	if (reg & CAPAREG_CAPV18)
		printf("   1.8V\n");
	if (reg & CAPAREG_CAPV30)
		printf("   3.0V\n");
	if (reg & CAPAREG_CAPV33)
		printf("   3.3V\n");
	printf("  DMA capability: %s\n", reg & CAPAREG_CAPDMA ? "yes" : "no");
	printf("  High speed capability: %s\n", reg & CAPAREG_CAPAHSPD ? "yes" : "no");
	printf("  Max block length: ");
	switch (CAPAREG_GET_CAPMAXBLKLEN(reg)) {
	case 0:
		printf("512 bytes\n");
		break;
	case 1:
		printf("1 kiBytes\n");
		break;
	case 2:
		printf("2 kiBytes\n");
		break;
	default:
		printf("(unknown)\n");
		break;
	}
}

void esdhc_info(struct device_d *dev)
{
	struct esdhc_mci *h = dev->priv;
	struct esdhc_platform_data *pd = h->pdata;

	printf("%s: I'm here (%p, IO: %p)!\n", __func__, h, h->base);

	esdhc_print_ver(h);
	esdhc_print_caps(h);

	printf("  Bus data width: %d bit\n", h->bus_width == 1 ? 4 : 1);
	printf("  Bus frequency: %u Hz\n", h->clock);
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
		pd->cd_type == ESDHC_CD_NONE ||
		pd->cd_type == ESDHC_CD_PERMANENT ? "no" : "yes");

	printf("  Card write protection support: %s\n",
		pd->wp_type == ESDHC_WP_NONE ||
		pd->wp_type == ESDHC_WP_CONTROLLER ? "no" : "yes");
}
#endif

int esdhc_register(struct esdhc_mci *h)
{
	h->mci.send_cmd = esdhc_request;
	h->mci.set_ios = esdhc_set_ios;
	h->mci.init = esdhc_reset;

	/*
	 * Start the clock to let the engine and the card finishes its startup
	 */
	h->clock = esdhc_clock_setup_speed(h, h->mci.f_min);

	return mci_register(&h->mci);
}
