/*
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "board-msm7x01a-regulator.h"

#define PCOM_VREG_CONSUMERS(name) \
	static struct regulator_consumer_supply __pcom_vreg_supply_##name[]

#define PCOM_VREG_CONSTRAINT_LVSW(_name, _always_on, _boot_on, _supply_uV) \
{ \
	.name = #_name, \
	.min_uV = 0, \
	.max_uV = 0, \
	.input_uV = _supply_uV, \
	.valid_modes_mask = REGULATOR_MODE_NORMAL, \
	.valid_ops_mask = REGULATOR_CHANGE_STATUS, \
	.apply_uV = 0, \
	.boot_on = _boot_on, \
	.always_on = _always_on \
}

#define PCOM_VREG_CONSTRAINT_DYN(_name, _min_uV, _max_uV, _always_on, \
		_boot_on, _apply_uV, _supply_uV) \
{ \
	.name = #_name, \
	.min_uV = _min_uV, \
	.max_uV = _max_uV, \
	.valid_modes_mask = REGULATOR_MODE_NORMAL, \
	.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS, \
	.input_uV = _supply_uV, \
	.apply_uV = _apply_uV, \
	.boot_on = _boot_on, \
	.always_on = _always_on \
}


#define PCOM_VREG_INIT(_name, _supply, _constraints)\
{ \
	.supply_regulator = _supply, \
	.consumer_supplies = __pcom_vreg_supply_##_name, \
	.num_consumer_supplies = ARRAY_SIZE(__pcom_vreg_supply_##_name), \
	.constraints = _constraints \
}

#define PCOM_VREG_SMP(_name, _id, _supply, _min_uV, _max_uV, _rise_time, \
		_pulldown, _always_on, _boot_on, _apply_uV, _supply_uV) \
{ \
	.init_data = PCOM_VREG_INIT(_name, _supply, \
		PCOM_VREG_CONSTRAINT_DYN(_name, _min_uV, _max_uV, _always_on, \
			_boot_on, _apply_uV, _supply_uV)), \
	.id = _id, \
	.rise_time = _rise_time, \
	.pulldown = _pulldown, \
	.negative = 0, \
}

#define PCOM_VREG_LDO PCOM_VREG_SMP

PCOM_VREG_CONSUMERS(ldo00) = {
	REGULATOR_SUPPLY("ldo00",	NULL),
	REGULATOR_SUPPLY("gp3",		NULL),
};

PCOM_VREG_CONSUMERS(ldo05) = {
	REGULATOR_SUPPLY("ldo05",	NULL),
	REGULATOR_SUPPLY("mmc",		NULL),
};

PCOM_VREG_CONSUMERS(ldo06) = {
	REGULATOR_SUPPLY("ldo06",	NULL),
	REGULATOR_SUPPLY("usb",		NULL),
};

PCOM_VREG_CONSUMERS(ldo09) = {
	REGULATOR_SUPPLY("ldo09",	NULL),
	REGULATOR_SUPPLY("gp1",		NULL),
};

PCOM_VREG_CONSUMERS(ldo11) = {
	REGULATOR_SUPPLY("ldo11",	NULL),
	REGULATOR_SUPPLY("gp2",		NULL),
};

PCOM_VREG_CONSUMERS(ldo13) = {
	REGULATOR_SUPPLY("ldo13",	NULL),
	REGULATOR_SUPPLY("wlan",	NULL),
};

PCOM_VREG_CONSUMERS(boost) = {
	REGULATOR_SUPPLY("boost",	NULL),
};

/**
 * Minimum and Maximum range for the regulators is as per the
 * device Datasheet. Actual value used by consumer is between
 * the provided range.
 */
static struct proccomm_regulator_info msm7x01a_pcom_vreg_info[] = {
	/* Standard regulators (SMPS and LDO)
	 * R = rise time (us)
	 * P = pulldown (1 = pull down, 0 = float, -1 = don't care)
	 * A = always on
	 * B = boot on
	 * V = automatic voltage set (meaningful for single-voltage regs only)
	 * S = supply voltage (uV)
	 *             name  id  supp    min uV    max uV  R   P  A  B  V  S */
	PCOM_VREG_LDO(ldo00,  5, NULL,  2850000,  2850000, 0, -1, 0, 0, 0, 0),
	PCOM_VREG_LDO(ldo05, 18, NULL,  2850000,  2850000, 0, -1, 0, 0, 0, 0),
	PCOM_VREG_LDO(ldo06, 16, NULL,  3300000,  3300000, 0, -1, 0, 0, 0, 0),
	PCOM_VREG_LDO(ldo09,  8, NULL,  2900000,  2900000, 0, -1, 0, 0, 0, 0),
	PCOM_VREG_LDO(ldo11, 21, NULL,  1800000,  1800000, 0, -1, 0, 0, 0, 0),
	PCOM_VREG_LDO(ldo13, 15, NULL,  1800000,  2850000, 0, -1, 0, 0, 0, 0),
	PCOM_VREG_LDO(boost, 17, NULL,  5000000,  5000000, 0, -1, 0, 0, 0, 0),
};

struct proccomm_regulator_platform_data msm7x01a_proccomm_regulator_data = {
	.regs = msm7x01a_pcom_vreg_info,
	.nregs = ARRAY_SIZE(msm7x01a_pcom_vreg_info)
};
