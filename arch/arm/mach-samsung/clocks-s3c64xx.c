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
 */

#include <config.h>
#include <common.h>
#include <init.h>
#include <clock.h>
#include <io.h>
#include <mach/s3c-iomap.h>
#include <mach/s3c-generic.h>
#include <mach/s3c-clocks.h>

static unsigned s3c_get_apllclk(void)
{
	uint32_t m, p, s, reg_val;

	reg_val = readl(S3C_APLLCON);
	m = S3C_APLLCON_GET_MDIV(reg_val);
	p = S3C_APLLCON_GET_PDIV(reg_val);
	s = S3C_APLLCON_GET_SDIV(reg_val);

	return (S3C64XX_CLOCK_REFERENCE * m) / (p << s);
}

static unsigned s3c_get_apll_out(void)
{
	if (readl(S3C_CLK_SRC) & S3C_CLK_SRC_FOUTAPLL)
		return s3c_get_apllclk();

	return S3C64XX_CLOCK_REFERENCE;
}

uint32_t s3c_get_mpllclk(void)
{
	uint32_t m, p, s, reg_val;

	reg_val = readl(S3C_MPLLCON);
	m = S3C_MPLLCON_GET_MDIV(reg_val);
	p = S3C_MPLLCON_GET_PDIV(reg_val);
	s = S3C_MPLLCON_GET_SDIV(reg_val);

	return (S3C64XX_CLOCK_REFERENCE * m) / (p << s);
}

unsigned s3c_get_epllclk(void)
{
	return 0;	 /* TODO */
}

static unsigned s3c_get_mpll_out(void)
{
	if (readl(S3C_CLK_SRC) & S3C_CLK_SRC_FOUTMPLL)
		return s3c_get_mpllclk();

	return S3C64XX_CLOCK_REFERENCE;
}

uint32_t s3c_get_fclk(void)
{
	unsigned clk;

	clk = s3c_get_apll_out();
	if (readl(S3C_MISC_CON) & S3C_MISC_CON_SYN667)
		clk /= 2;

	return clk / (S3C_CLK_DIV0_GET_ADIV(readl(S3C_CLK_DIV0)) + 1);
}

static unsigned s3c_get_hclk_in(void)
{
	unsigned clk;

	if (readl(S3C_OTHERS) & S3C_OTHERS_CLK_SELECT)
		clk = s3c_get_apll_out();
	else
		clk = s3c_get_mpll_out();

	if (readl(S3C_MISC_CON) & S3C_MISC_CON_SYN667)
		clk /= 5;

	return clk;
}

static unsigned s3c_get_hclkx2(void)
{
	return s3c_get_hclk_in() /
			(S3C_CLK_DIV0_GET_HCLK2(readl(S3C_CLK_DIV0)) + 1);
}

uint32_t s3c_get_hclk(void)
{
	return s3c_get_hclkx2() /
			(S3C_CLK_DIV0_GET_HCLK(readl(S3C_CLK_DIV0)) + 1);
}

uint32_t s3c_get_pclk(void)
{
	return s3c_get_hclkx2() /
			(S3C_CLK_DIV0_GET_PCLK(readl(S3C_CLK_DIV0)) + 1);
}

static void s3c_init_mpll_dout(void)
{
	unsigned reg;

	/* keep it at the same frequency as HCLKx2 */
	reg = readl(S3C_CLK_DIV0) | S3C_CLK_DIV0_SET_MPLL_DIV(1); /* e.g. / 2 */
	writel(reg, S3C_CLK_DIV0);
}

/* configure and enable UCLK1 */
static int s3c_init_uart_clock(void)
{
	unsigned reg;

	s3c_init_mpll_dout();	/* to have a reliable clock source */

	/* source the UART clock from the MPLL, currently *not* from EPLL */
	reg = readl(S3C_CLK_SRC) | S3C_CLK_SRC_UARTMPLL;
	writel(reg, S3C_CLK_SRC);

	/* keep UART clock at the same frequency than the PCLK */
	reg = readl(S3C_CLK_DIV2) & ~S3C_CLK_DIV2_UART_MASK;
	reg |= S3C_CLK_DIV2_SET_UART(0x3);	/* / 4 */
	writel(reg, S3C_CLK_DIV2);

	/* ensure this very special clock is running */
	reg = readl(S3C_SCLK_GATE) | S3C_SCLK_GATE_UART;
	writel(reg, S3C_SCLK_GATE);

	return 0;
}
core_initcall(s3c_init_uart_clock);

unsigned s3c_get_uart_clk(unsigned source)
{
	u32 reg;
	unsigned clk, pdiv, uartpdiv;

	switch (source) {
	default: /* PCLK */
		clk = s3c_get_pclk();
		pdiv = uartpdiv = 1;
		break;
	case 1: /* UCLK0 */
		clk = 0;
		pdiv = uartpdiv = 1;	/* TODO */
		break;
	case 3: /* UCLK1 */
		reg = readl(S3C_CLK_SRC);
		if (reg & S3C_CLK_SRC_UARTMPLL)
			clk = s3c_get_mpllclk();
		else
			clk = s3c_get_epllclk();
		pdiv = S3C_CLK_DIV0_GET_MPLL_DIV(readl(S3C_CLK_DIV0)) + 1;
		uartpdiv = S3C_CLK_DIV2_GET_UART(readl(S3C_CLK_DIV2)) + 1;
		break;
	}

	return clk / pdiv / uartpdiv;
}

int s3c64xx_dump_clocks(void)
{
	printf("refclk:  %7d kHz\n", S3C64XX_CLOCK_REFERENCE / 1000);
	printf("apll:    %7d kHz\n", s3c_get_apllclk() / 1000);
	printf("mpll:    %7d kHz\n", s3c_get_mpllclk() / 1000);
	printf("CPU:     %7d kHz\n", s3c_get_fclk() / 1000);
	printf("hclkx2:  %7d kHz\n", s3c_get_hclkx2() / 1000);
	printf("hclk:    %7d kHz\n", s3c_get_hclk() / 1000);
	printf("pclk:    %7d kHz\n", s3c_get_pclk() / 1000);
	return 0;
}

late_initcall(s3c64xx_dump_clocks);
