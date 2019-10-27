/*
 *  arch/arm/include/asm/mach/mmc.h
 */
#ifndef ASMARM_MACH_MMC_H
#define ASMARM_MACH_MMC_H

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>

#define SDC_DAT1_DISABLE 0
#define SDC_DAT1_ENABLE  1
#define SDC_DAT1_ENWAKE  2
#define SDC_DAT1_DISWAKE 3

struct embedded_sdio_data {
	struct sdio_cis cis;
	struct sdio_cccr cccr;
	struct sdio_embedded_func *funcs;
	int num_funcs;
};

struct msm_mmc_gpio {
	unsigned no;
	const char *name;
};

struct msm_mmc_gpio_data {
	struct msm_mmc_gpio *gpio;
	u8 size;
};

struct msm_mmc_platform_data {
	unsigned int ocr_mask;			/* available voltages */
	int built_in;				/* built-in device flag */
	u32 (*translate_vdd)(struct device *, unsigned int);
	unsigned int (*status)(struct device *);
	struct embedded_sdio_data *embedded_sdio;
	int (*register_status_notify)(void (*callback)(int card_present, void *dev_id), void *dev_id);

#if defined(CONFIG_MMC_MSM)
	void (*sdio_lpm_gpio_setup)(struct device *, unsigned int);

        unsigned int status_irq;
        unsigned int sdiowakeup_irq;
        unsigned long irq_flags;
        unsigned long mmc_bus_width;

        int (*wpswitch) (struct device *);
	int dummy52_required;

	unsigned int msmsdcc_fmin;
	unsigned int msmsdcc_fmid;
	unsigned int msmsdcc_fmax;
	bool nonremovable;
	bool pclk_src_dfab;

	int (*cfg_mpm_sdiowakeup)(struct device *, unsigned);
	bool sdcc_v4_sup;

	int is_sdio_al_client;
#endif

	struct msm_mmc_gpio_data *gpio_data;
};

#endif
