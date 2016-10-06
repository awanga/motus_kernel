/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2008-2012, The Linux Foundation. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * The MSM peripherals are spread all over across 768MB of physical
 * space, which makes just having a simple IO_ADDRESS macro to slide
 * them into the right virtual location rough.  Instead, we will
 * provide a master phys->virt mapping for peripherals here.
 *
 */

#ifndef __ASM_ARCH_MSM_IOMAP_7XXX_H
#define __ASM_ARCH_MSM_IOMAP_7XXX_H

/* Physical base address and size of peripherals.
 * Ordered by the virtual base addresses they will be mapped at.
 *
 * MSM_VIC_BASE must be an value that can be loaded via a "mov"
 * instruction, otherwise entry-macro.S will not compile.
 *
 * If you add or remove entries here, you'll want to edit the
 * msm_io_desc array in arch/arm/mach-msm/io.c to reflect your
 * changes.
 *
 */

#if 0 /*def CONFIG_ARCH_MSM7X01A*/

/* disable unified iomap */
#undef MSM_TMR_BASE
#undef MSM_TMR0_BASE
#undef MSM_QGIC_DIST_BASE
#undef MSM_QGIC_CPU_BASE
#undef MSM_TCSR_BASE
#undef MSM_APCS_GCC_BASE
#undef MSM_SAW_L2_BASE
#undef MSM_SAW0_BASE
#undef MSM_SAW1_BASE
#undef MSM_IMEM_BASE
#undef MSM_ACC0_BASE
#undef MSM_ACC1_BASE
#undef MSM_ACC2_BASE
#undef MSM_ACC3_BASE
#undef MSM_CLK_CTL_BASE
#undef MSM_MMSS_CLK_CTL_BASE
#undef MSM_LPASS_CLK_CTL_BASE
#undef MSM_HFPLL_BASE
#undef MSM_TLMM_BASE
#undef MSM_SHARED_RAM_BASE
#undef MSM_SIC_NON_SECURE_BASE
#undef MSM_HDMI_BASE
#undef MSM_RPM_BASE
#undef MSM_RPM_MPM_BASE
#undef MSM_QFPROM_BASE
#undef MSM_L2CC_BASE
#undef MSM_APCS_GLB_BASE
#undef MSM_SAW2_BASE
#undef MSM_SAW3_BASE
#undef MSM_VIC_BASE
#undef MSM_CSR_BASE
#undef MSM_GPIO1_BASE
#undef MSM_GPIO2_BASE
#undef MSM_SCU_BASE
#undef MSM_CFG_CTL_BASE
#undef MSM_CLK_CTL_SH2_BASE
#undef MSM_MPM2_PSHOLD_BASE
#undef MSM_MDC_BASE
#undef MSM_AD5_BASE

/* msm7x00 compatible memory map */
#define MSM_VIC_BASE          IOMEM(0xF8000000)
#define MSM_CSR_BASE          IOMEM(0xF8001000)
#define MSM_GPIO1_BASE        IOMEM(0xF8003000)
#define MSM_GPIO2_BASE        IOMEM(0xF8004000)
#define MSM_CLK_CTL_BASE      IOMEM(0xF8005000)
#define MSM_SHARED_RAM_BASE   IOMEM(0xF8100000)
#define MSM_MDC_BASE	      IOMEM(0xF8200000)
#define MSM_AD5_BASE          IOMEM(0xF8300000)

#define MSM_TMR_BASE          MSM_CSR_BASE

#endif /* CONFIG_ARCH_MSM7X01A */

#define MSM7XXX_VIC_PHYS          0xC0000000
#define MSM7XXX_VIC_SIZE          SZ_4K

#define MSM7XXX_CSR_PHYS          0xC0100000
#define MSM7XXX_CSR_SIZE          SZ_4K

#define MSM7XXX_TMR_PHYS          MSM7XXX_CSR_PHYS
#define MSM7XXX_TMR_SIZE          SZ_4K

#define MSM7XXX_GPIO1_PHYS        0xA9200000
#define MSM7XXX_GPIO1_SIZE        SZ_4K

#define MSM7XXX_GPIO2_PHYS        0xA9300000
#define MSM7XXX_GPIO2_SIZE        SZ_4K

#define MSM7XXX_CLK_CTL_PHYS      0xA8600000
#define MSM7XXX_CLK_CTL_SIZE      SZ_4K

#define MSM7XXX_L2CC_PHYS         0xC0400000
#define MSM7XXX_L2CC_SIZE         SZ_4K

#define MSM7XXX_UART1_PHYS        0xA9A00000
#define MSM7XXX_UART1_SIZE        SZ_4K

#define MSM7XXX_UART2_PHYS        0xA9B00000
#define MSM7XXX_UART2_SIZE        SZ_4K

#define MSM7XXX_UART3_PHYS        0xA9C00000
#define MSM7XXX_UART3_SIZE        SZ_4K

#define MSM7XXX_MDC_PHYS          0xAA500000
#define MSM7XXX_MDC_SIZE          SZ_1M

#define MSM7XXX_AD5_PHYS          0xAC000000
#define MSM7XXX_AD5_SIZE          (SZ_1M*13)


#define MSM_UART1_PHYS        0xA9A00000
#define MSM_UART1_SIZE        SZ_4K

#define MSM_UART2_PHYS        0xA9B00000
#define MSM_UART2_SIZE        SZ_4K

#define MSM_UART3_PHYS        0xA9C00000
#define MSM_UART3_SIZE        SZ_4K

#define MSM_SDC1_PHYS         0xA0400000
#define MSM_SDC1_SIZE         SZ_4K

#define MSM_SDC2_PHYS         0xA0500000
#define MSM_SDC2_SIZE         SZ_4K

#define MSM_SDC3_PHYS         0xA0600000
#define MSM_SDC3_SIZE         SZ_4K

#define MSM_SDC4_PHYS         0xA0700000
#define MSM_SDC4_SIZE         SZ_4K

#define MSM_NAND_PHYS         0xA0A00000
#define MSM_NAND_SIZE         SZ_4K

#define MSM_I2C_PHYS          0xA9900000
#define MSM_I2C_SIZE          SZ_4K

#define MSM_HSUSB_PHYS        0xA0800000
#define MSM_HSUSB_SIZE        SZ_4K

#define MSM_PMDH_PHYS         0xAA600000
#define MSM_PMDH_SIZE         SZ_4K

#define MSM_EMDH_PHYS         0xAA700000
#define MSM_EMDH_SIZE         SZ_4K

#define MSM_MDP_PHYS          0xAA200000
#define MSM_MDP_SIZE          0x000F0000

#define MSM_VFE_PHYS          0xA0F00000
#define MSM_VFE_SIZE          SZ_1M

#define MSM_UART1DM_PHYS      0xA0200000
#define MSM_UART2DM_PHYS      0xA0300000

#define MSM_SSBI_PHYS         0xA8100000
#define MSM_SSBI_SIZE         SZ_4K

#define MSM_TSSC_PHYS         0xAA300000
#define MSM_TSSC_SIZE         SZ_4K

#define MSM_TVENC_PHYS        0xAA400000
#define MSM_TVENC_SIZE        SZ_4K

#ifndef __ASSEMBLY__
extern void __iomem *__msm_ioremap_caller(unsigned long phys_addr, size_t size,
					unsigned int mtype, void *caller);
#endif

#endif
