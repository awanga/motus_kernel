/* Copyright (c) 2008-2009, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* Qualcomm PMIC Multi-Purpose Pin Configurations */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/module.h>

#include <mach/mpp.h>
#include <mach/proc_comm.h>

#define MPP(_name, _id, _status) { .name = _name, .id = _id, .status = _status}

struct mpp {
        const char *name;
        unsigned id;
        int status;
};

static struct mpp mpps[] = {
        MPP("mpp1", 0, 0),
        MPP("mpp2", 1, 0),
        MPP("mpp3", 2, 0),
        MPP("mpp4", 3, 0),
        MPP("mpp5", 4, 0),
        MPP("mpp6", 5, 0),
        MPP("mpp7", 6, 0),
        MPP("mpp8", 7, 0),
        MPP("mpp9", 8, 0),
        MPP("mpp10", 9, 0),
        MPP("mpp11", 10, 0),
        MPP("mpp12", 11, 0),
        MPP("mpp13", 12, 0),
        MPP("mpp14", 13, 0),
        MPP("mpp15", 14, 0),
        MPP("mpp16", 15, 0),
        MPP("mpp17", 16, 0),
        MPP("mpp18", 17, 0),
        MPP("mpp19", 18, 0),
        MPP("mpp20", 19, 0),
        MPP("mpp21", 20, 0),
        MPP("mpp22", 21, 0),
};

unsigned mpp_get(struct device *dev, const char *id)
{
        int n;
        for (n = 0; n < ARRAY_SIZE(mpps); n++) {
                if (!strcmp(mpps[n].name, id))
                        return (mpps + n)->id;
        }
        return 0;
}
EXPORT_SYMBOL(mpp_get);

#if defined(CONFIG_MACH_MOT)
int mpp_config_analog_sink(unsigned mpp, unsigned config)
{
        unsigned id = mpp;
        int err;
        err = msm_proc_comm(PCOM_PM_MPP_CONFIG_I_SINK, &id, &config);
        return err;
}
EXPORT_SYMBOL(mpp_config_analog_sink);
#endif

int mpp_config_digital_out(unsigned mpp, unsigned config)
{
	int err;
	err = msm_proc_comm(PCOM_PM_MPP_CONFIG, &mpp, &config);
	if (err)
		pr_err("%s: msm_proc_comm(PCOM_PM_MPP_CONFIG) failed\n",
		       __func__);
	return err;
}
EXPORT_SYMBOL(mpp_config_digital_out);

int mpp_config_digital_in(unsigned mpp, unsigned config)
{
	int err;
	err = msm_proc_comm(PCOM_PM_MPP_CONFIG_DIGITAL_INPUT, &mpp, &config);
	if (err)
		pr_err("%s: msm_proc_comm(PCOM_PM_MPP_CONFIG) failed\n",
		       __func__);
	return err;
}
EXPORT_SYMBOL(mpp_config_digital_in);

#if defined(CONFIG_DEBUG_FS)
static int test_result;

static int mpp_debug_set(void *data, u64 val)
{
	unsigned mpp = (unsigned) data;

	test_result = mpp_config_digital_out(mpp, (unsigned)val);
	if (test_result) {
		printk(KERN_ERR
			   "%s: mpp_config_digital_out \
			   [mpp(%d) = 0x%x] failed (err=%d)\n",
			   __func__, mpp, (unsigned)val, test_result);
	}
	return 0;
}

static int mpp_debug_get(void *data, u64 *val)
{
	if (!test_result)
		*val = 0;
	else
		*val = 1;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mpp_fops, mpp_debug_get, mpp_debug_set, "%llu\n");

static int __init mpp_debug_init(void)
{
	struct dentry *dent;
	int n;
	char	file_name[16];

	dent = debugfs_create_dir("mpp", 0);
	if (IS_ERR(dent))
		return 0;

	for (n = 0; n < MPPS; n++) {
		snprintf(file_name, sizeof(file_name), "mpp%d", n + 1);
		debugfs_create_file(file_name, 0644, dent,
				    (void *)n, &mpp_fops);
	}

	return 0;
}

device_initcall(mpp_debug_init);
#endif

