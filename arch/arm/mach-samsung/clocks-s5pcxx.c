/*
 * Copyright (C) 2012 Alexey Galakhov
 * Copyright (C) 2012 Juergen Beisert, Pengutronix
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
#include <io.h>
#include <malloc.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <mach/s3c-iomap.h>
#include <mach/s3c-generic.h>
#include <mach/s3c-clocks.h>


enum s5p_clks {
	xxti, xusbxti, sclk_hdmi27m,
	fin_pll, fin_vpll,
	fout_apll, fout_mpll, fout_epll, fout_vpll,
	sclk_apll, sclk_mpll, sclk_epll, sclk_vpll,
	sclk_a2m,
	mout_hpm, mout_msys, mout_dsys, mout_psys,

	sclk_hpm,
	armclk, hclk_msys, pclk_msys, hclk_imem, sclk_dmc0,
	hclk_dsys, pclk_dsys,
	sclk_onenand, hclk_psys, pclk_psys, clk_dpm, clk_dvsem,

	mux_mmc0, mux_mmc1, mux_mmc2, mux_mmc3,
	mux_uart0, mux_uart1, mux_uart2, mux_uart3,
	sclk_mmc0, sclk_mmc1, sclk_mmc2, sclk_mmc3,
	sclk_mmc0_b, sclk_mmc1_b, sclk_mmc2_b, sclk_mmc3_b,
	sclk_uart0, sclk_uart1, sclk_uart2, sclk_uart3,
	out_mmc0, out_mmc1, out_mmc2, out_mmc3,
	out_uart0, out_uart1, out_uart2, out_uart3,

	clk_max
};

static struct clk *clks[clk_max];

static const char *mux_vpllsrc[] = {"fin_pll", "sclk_hdmi27m" };

static const char *mux_apll[] = { "fin_pll", "fout_apll" };
static const char *mux_mpll[] = { "fin_pll", "fout_mpll" };
static const char *mux_epll[] = { "fin_pll", "fout_epll" };
static const char *mux_vpll[] = { "fin_vpll", "fout_vpll" };

static const char *mux_msys[] = { "sclk_apll", "sclk_mpll" };
static const char *mux_dpsys[] = { "sclk_mpll", "sclk_a2m" };

static const char *mux_std[] = {
	"xxti", "xusbxti", "sclk_hdmi27m",
	"sclk_usbphy0", "sclk_usbphy1", "sclk_hdmiphy",
	"sclk_mpll", "sclk_epll", "sclk_vpll"
};

static struct clk *s3c_clk_pll(const char *name, const char *parent, void __iomem *base, bool onebased);

static int s5p_clk_probe(struct device_d *dev)
{
	void __iomem *base;

	base = NULL; //dev_request_mem_region(dev, 0);

	clks[xxti] = clk_fixed("xxti", S5PCXX_CLOCK_REFERENCE);
	clks[xusbxti] = clk_fixed("xusbxti", 24000000);
	clks[sclk_hdmi27m] = clk_fixed("sclk_hdmi27m", 27000000);

	clks[fin_pll] = clk_fixed("fin_pll", S5PCXX_CLOCK_REFERENCE);

	clks[fin_vpll] = clk_mux("fin_vpll", base + S5P_CLK_SRC1, 28, 1, mux_vpllsrc, ARRAY_SIZE(mux_vpllsrc));

	clks[fout_apll] = s3c_clk_pll("fout_apll", "fin_pll", base + S5P_xPLL_CON + S5P_APLL, 1);
	clks[fout_mpll] = s3c_clk_pll("fout_mpll", "fin_pll", base + S5P_xPLL_CON + S5P_MPLL, 0);
	clks[fout_epll] = s3c_clk_pll("fout_epll", "fin_pll", base + S5P_xPLL_CON + S5P_EPLL, 0);
	clks[fout_vpll] = s3c_clk_pll("fout_vpll", "fin_vpll", base + S5P_xPLL_CON + S5P_VPLL, 0);

	clks[sclk_apll] = clk_mux("sclk_apll", base + S5P_CLK_SRC0,  0, 1, mux_apll, ARRAY_SIZE(mux_apll));
	clks[sclk_mpll] = clk_mux("sclk_mpll", base + S5P_CLK_SRC0,  4, 1, mux_mpll, ARRAY_SIZE(mux_mpll));
	clks[sclk_epll] = clk_mux("sclk_epll", base + S5P_CLK_SRC0,  8, 1, mux_epll, ARRAY_SIZE(mux_epll));
	clks[sclk_vpll] = clk_mux("sclk_vpll", base + S5P_CLK_SRC0, 12, 1, mux_vpll, ARRAY_SIZE(mux_vpll));

	clks[sclk_a2m] = clk_divider("a2m", "fout_apll", base + S5P_CLK_DIV0, 4, 3);

	//clks[mout_hpm] = clk_mux("mout_hpm", base + , , );
	clks[mout_msys] = clk_mux("mout_msys", base + S5P_CLK_SRC0, 16, 1, mux_msys, ARRAY_SIZE(mux_msys));
	clks[mout_dsys] = clk_mux("mout_dsys", base + S5P_CLK_SRC0, 20, 1, mux_dpsys, ARRAY_SIZE(mux_dpsys));
	clks[mout_psys] = clk_mux("mout_psys", base + S5P_CLK_SRC0, 24, 1, mux_dpsys, ARRAY_SIZE(mux_dpsys));

	//clks[sclk_hpm] = 

	clks[armclk] = clk_divider("armclk", "mout_msys", base + S5P_CLK_DIV0, 0, 3);
	clks[hclk_msys] = clk_divider("hclk_msys", "armclk", base + S5P_CLK_DIV0, 8, 3);
	clks[pclk_msys] = clk_divider("pclk_msys", "hclk_msys", base + S5P_CLK_DIV0, 12, 3);
	//clks[hclk_imem] =
	//clks[sclk_dmc0] =

	clks[hclk_dsys] = clk_divider("hclk_dsys", "mout_dsys", base + S5P_CLK_DIV0, 16, 4);
	clks[pclk_dsys] = clk_divider("pclk_dsys", "hclk_dsys", base + S5P_CLK_DIV0, 20, 3);

	//clks[sclk_onenand] =
	clks[hclk_psys] = clk_divider("hclk_psys", "mout_psys", base + S5P_CLK_DIV0, 24, 4);
	clks[pclk_psys] = clk_divider("pclk_psys", "hclk_psys", base + S5P_CLK_DIV0, 28, 3);
	//clks[clk_dpm] =
	//clks[clk_dvsem] =


	clks[mux_mmc0] = clk_mux("mux_mmc0", base + S5P_CLK_SRC4, 0, 4, mux_std, ARRAY_SIZE(mux_std));
	clks[mux_mmc1] = clk_mux("mux_mmc1", base + S5P_CLK_SRC4, 4, 4, mux_std, ARRAY_SIZE(mux_std));
	clks[mux_mmc2] = clk_mux("mux_mmc2", base + S5P_CLK_SRC4, 8, 4, mux_std, ARRAY_SIZE(mux_std));
	clks[mux_mmc3] = clk_mux("mux_mmc3", base + S5P_CLK_SRC4, 12, 4, mux_std, ARRAY_SIZE(mux_std));
	clks[mux_uart0] = clk_mux("mux_uart0", base + S5P_CLK_SRC4, 16, 4, mux_std, ARRAY_SIZE(mux_std));
	clks[mux_uart1] = clk_mux("mux_uart1", base + S5P_CLK_SRC4, 20, 4, mux_std, ARRAY_SIZE(mux_std));
	clks[mux_uart2] = clk_mux("mux_uart2", base + S5P_CLK_SRC4, 24, 4, mux_std, ARRAY_SIZE(mux_std));
	clks[mux_uart3] = clk_mux("mux_uart3", base + S5P_CLK_SRC4, 28, 4, mux_std, ARRAY_SIZE(mux_std));
	clks[sclk_mmc0] = clk_divider("sclk_mmc0", "mux_mmc0", base + S5P_CLK_DIV4, 0, 4);
	clks[sclk_mmc1] = clk_divider("sclk_mmc1", "mux_mmc1", base + S5P_CLK_DIV4, 4, 4);
	clks[sclk_mmc2] = clk_divider("sclk_mmc2", "mux_mmc2", base + S5P_CLK_DIV4, 8, 4);
	clks[sclk_mmc3] = clk_divider("sclk_mmc3", "mux_mmc3", base + S5P_CLK_DIV4, 12, 4);
	clks[sclk_uart0] = clk_divider("sclk_uart0", "mux_uart0", base + S5P_CLK_DIV4, 16, 4);
	clks[sclk_uart1] = clk_divider("sclk_uart1", "mux_uart1", base + S5P_CLK_DIV4, 20, 4);
	clks[sclk_uart2] = clk_divider("sclk_uart2", "mux_uart2", base + S5P_CLK_DIV4, 24, 4);
	clks[sclk_uart3] = clk_divider("sclk_uart3", "mux_uart3", base + S5P_CLK_DIV4, 28, 4);
	clks[sclk_mmc0_b] = clk_gate("sclk_mmc0_b", "sclk_mmc0", base + S5P_CLK_GATE_BLOCK, 9);
	clks[sclk_mmc1_b] = clk_gate("sclk_mmc1_b", "sclk_mmc1", base + S5P_CLK_GATE_BLOCK, 9);
	clks[sclk_mmc2_b] = clk_gate("sclk_mmc2_b", "sclk_mmc2", base + S5P_CLK_GATE_BLOCK, 9);
	clks[sclk_mmc3_b] = clk_gate("sclk_mmc3_b", "sclk_mmc3", base + S5P_CLK_GATE_BLOCK, 9);
	clks[out_mmc0] = clk_gate("out_mmc0", "sclk_mmc0_b", base + S5P_CLK_GATE_IP2, 16);
	clks[out_mmc1] = clk_gate("out_mmc1", "sclk_mmc1_b", base + S5P_CLK_GATE_IP2, 17);
	clks[out_mmc2] = clk_gate("out_mmc2", "sclk_mmc2_b", base + S5P_CLK_GATE_IP2, 18);
	clks[out_mmc3] = clk_gate("out_mmc3", "sclk_mmc3_b", base + S5P_CLK_GATE_IP2, 19);
	clks[out_uart0] = clk_gate("out_uart0", "sclk_uart0", base + S5P_CLK_GATE_IP3, 17);
	clks[out_uart1] = clk_gate("out_uart1", "sclk_uart1", base + S5P_CLK_GATE_IP3, 18);
	clks[out_uart2] = clk_gate("out_uart2", "sclk_uart2", base + S5P_CLK_GATE_IP3, 19);
	clks[out_uart3] = clk_gate("out_uart3", "sclk_uart3", base + S5P_CLK_GATE_IP3, 20);

	clkdev_add_physbase(clks[out_mmc0], S3C_SDMMC0_BASE, NULL);
	clkdev_add_physbase(clks[out_mmc1], S3C_SDMMC1_BASE, NULL);
	clkdev_add_physbase(clks[out_mmc2], S3C_SDMMC2_BASE, NULL);
	clkdev_add_physbase(clks[out_mmc3], S3C_SDMMC3_BASE, NULL);
	clkdev_add_physbase(clks[out_uart0], S3C_UART1_BASE, NULL);
	clkdev_add_physbase(clks[out_uart1], S3C_UART2_BASE, NULL);
	clkdev_add_physbase(clks[out_uart2], S3C_UART3_BASE, NULL);
	clkdev_add_physbase(clks[out_uart3], S3C_UART4_BASE, NULL);

	return 0;
}


struct clk_s3c_pll {
	struct clk clk;
	void __iomem *reg;
	const char *parent;
	bool onebased;
};

static unsigned long clk_s3c_pll_recalc_rate(struct clk *clk, unsigned long parent_rate)
{
	struct clk_s3c_pll *pll = container_of(clk, struct clk_s3c_pll, clk);
	uint32_t m, p, s;
	uint32_t reg = readl(pll->reg);
	m = (reg >> 16) & 0x3ff;
	p = (reg >> 8) & 0x3f;
	s = (reg >> 0) & 0x7;
	if (pll->onebased)
		s -= 1;
	return m * (parent_rate / (p << s));
}

static struct clk_ops clk_s3c_pll_ops = {
	.recalc_rate = clk_s3c_pll_recalc_rate,
};

static struct clk *s3c_clk_pll(const char *name, const char *parent, void __iomem *reg, bool onebased)
{
	struct clk_s3c_pll *pll = xzalloc(sizeof(*pll));
	int ret;

	pll->parent = parent;
	pll->reg = reg;
	pll->onebased = onebased;
	pll->clk.ops = &clk_s3c_pll_ops;
	pll->clk.name = name;
	pll->clk.parent_names = &pll->parent;
	pll->clk.num_parents = 1;

	ret = clk_register(&pll->clk);
	if (ret) {
		free(pll);
		return ERR_PTR(ret);
	}

	return &pll->clk;
}


static struct driver_d s5p_ccm_driver = {
	.probe	= s5p_clk_probe,
	.name	= "s5p-ccm",
};

static int s5p_ccm_init(void)
{
	int ret = platform_driver_register(&s5p_ccm_driver);
	add_generic_device("s5p-ccm", 0, NULL, S5P_CLOCK_POWER_BASE, 0x10000, IORESOURCE_MEM, NULL);
	return ret;
}
core_initcall(s5p_ccm_init);


/* Legacy interface. TODO: remove.  */

uint32_t s3c_get_mpllclk(void)
{
	return clk_get_rate(clks[fout_mpll]);
}

uint32_t s3c_get_apllclk(void)
{
	return clk_get_rate(clks[fout_apll]);
}

uint32_t s3c_get_hclk(void)
{
	return clk_get_rate(clks[hclk_psys]);
}

uint32_t s3c_get_pclk(void)
{
	//return 66700000;
	return clk_get_rate(clks[pclk_psys]);
}

/* we are using the internal 'uclk1' as the UART source */
static unsigned s3c_get_uart_clk_uclk1(void)
{
	unsigned clk = s3c_get_mpllclk();	/* TODO check for EPLL */
	unsigned uartpdiv = ((readl(S5P_CLK_DIV4) >> 16) & 0x3) + 1; /* TODO this is UART0 only */
	return clk / uartpdiv;
}

unsigned s3c_get_uart_clk(unsigned src) {
	//return 66700000;
	return (src & 1) ? s3c_get_uart_clk_uclk1() : s3c_get_pclk();
}


