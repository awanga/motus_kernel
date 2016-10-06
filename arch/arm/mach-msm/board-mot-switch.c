/* arch/arm/mach-msm/board-mot-switch.c
 *
 * Copyright (C) 2008 Motorola, Inc.
 * Author: Vladimir Karpovich <Vladimir.Karpovich@motorola.com>
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
 */

#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/switch.h>
#include <linux/input.h>
#include <linux/debugfs.h>
#include <asm/gpio.h>
#include <asm/atomic.h>
#include <mach/board.h>
#include <mach/vreg.h>

#include <asm/mach-types.h>

#include "board-mot.h"

#define DRIVER_NAME "hs"
#define DRIVER_NAME_H2W "h2w"
#define HEADSET_DETECT_GPIO   	    39
#define HEADSET_SEND_END_0_GPIO     37
#define HEADSET_SEND_END_1_GPIO     38

#define HEADSET_DETECT_DEBOUNCE_TIMER   300000000	/*  500 ms */
#define HEADSET_BTN_DEBOUNCE_TIMER      100000000	/* 100 ms */
#define HEADSET_BTN_DETECT_REPEATS      20
#define HEADSET_BTN_HOLD_TIMER          3	/* 3 sec */

#define HEADSET_MIC_BIAS_DELAY          50	/* 50 msec */

#include <mach/msm_rpcrouter.h>

struct hs_dev {
	struct switch_dev sdev, sdev_h2w;
	struct input_dev *input;
	uint8_t state, detect_state;
	uint8_t mic;
	uint8_t tv_out;
	uint8_t btn_state;
	uint8_t btn_in_test;
	uint8_t btn_counter;
	uint8_t btn_off_counter;
	unsigned int irq;
	unsigned int irq_btn_0;
	unsigned int irq_btn_1;
	struct hrtimer timer;
	ktime_t debounce_time;
	struct hrtimer btn_timer;
	ktime_t btn_debounce_time;
	struct work_struct work;
};

static struct hs_dev *hsdev;

/************************************/
/* modified rpc server handset code */
/************************************/

#define HS_SERVER_PROG 0x30000062
#define HS_SERVER_VERS 0x00010001

#define HS_END_K		0x51	/* End key or Power key */
#define HS_HEADSET_SWITCH_K	0x84
#define HS_REL_K		0xFF	/* key release */

#define KEY(hs_key, input_key) ((hs_key << 24) | input_key)

static const uint32_t hs_key_map[] = {
	KEY(HS_END_K, KEY_POWER),
	KEY(HS_HEADSET_SWITCH_K, KEY_MEDIA),
	0
};

static int hs_find_key(uint32_t hscode)
{
	int i, key;

	key = KEY(hscode, 0);

	for (i = 0; hs_key_map[i] != 0; i++) {
		if ((hs_key_map[i] & 0xff000000) == key)
			return hs_key_map[i] & 0x00ffffff;
	}
	return -1;
}

/*
 * tuple format: (key_code, key_param)
 *
 * old-architecture:
 * key-press = (key_code, 0)
 * key-release = (0xff, key_code)
 *
 * new-architecutre:
 * key-press = (key_code, 0)
 * key-release = (key_code, 0xff)
 */
static void report_hs_key(uint32_t key_code, uint32_t key_parm)
{
	int key, temp_key_code;

	if (key_code == HS_REL_K)
		key = hs_find_key(key_parm);
	else
		key = hs_find_key(key_code);

	temp_key_code = key_code;

	if (key_parm == HS_REL_K)
		key_code = key_parm;

	switch (key) {
	case KEY_POWER:
	case KEY_END:
		input_report_key(hsdev->input, key, (key_code != HS_REL_K));
		break;

	case SW_HEADPHONE_INSERT:
		break;
	case KEY_MEDIA:
		break;
	default:
		printk(KERN_ERR "%s: Unhandled handset key %d\n", __func__,
				key);
	case -1:
		printk(KERN_ERR "%s: No mapping for remote handset event %d\n",
				 __func__, temp_key_code);
		return;
	}
    input_sync(hsdev->input);
}

#define RPC_KEYPAD_NULL_PROC 0
#define RPC_KEYPAD_PASS_KEY_CODE_PROC 2
#define RPC_KEYPAD_SET_PWR_KEY_STATE_PROC 3

static int handle_hs_rpc_call(struct msm_rpc_server *server,
			   struct rpc_request_hdr *req, unsigned len)
{
	struct rpc_keypad_pass_key_code_args {
		uint32_t key_code;
		uint32_t key_parm;
	};

	switch (req->procedure) {
	case RPC_KEYPAD_NULL_PROC:
		return 0;

	case RPC_KEYPAD_PASS_KEY_CODE_PROC: {
		struct rpc_keypad_pass_key_code_args *args;

		args = (struct rpc_keypad_pass_key_code_args *)(req + 1);
		args->key_code = be32_to_cpu(args->key_code);
		args->key_parm = be32_to_cpu(args->key_parm);

		report_hs_key(args->key_code, args->key_parm);

		return 0;
	}

	case RPC_KEYPAD_SET_PWR_KEY_STATE_PROC:
		/* This RPC function must be available for the ARM9
		 * to function properly.  This function is redundant
		 * when RPC_KEYPAD_PASS_KEY_CODE_PROC is handled. So
		 * input_report_key is not needed.
		 */
		return 0;
	default:
		return -ENODEV;
	}
}

static struct msm_rpc_server hs_rpc_server = {
	.prog		= HS_SERVER_PROG,
	.vers		= HS_SERVER_VERS,
	.rpc_call	= handle_hs_rpc_call,
};

static int __init hs_rpc_server_init(void)
{
	return msm_rpc_create_server(&hs_rpc_server);
}
module_init(hs_rpc_server_init);

/************************************/

static int convert_to_h2w(int state)
{
	switch (state) {
	case 1:
		return (2);
	case 3:
		return (1);
	}
	return (state);
}

void hs_switch_set_state(struct hs_dev *hs, int state)
{
	switch_set_state(&hs->sdev, state);
	switch_set_state(&hs->sdev_h2w, convert_to_h2w(state));
}

/* rpc call for pm mic on */
int msm_pm_mic_en(unsigned char enable_disable)
{
	struct vreg *vreg;
	int rc = 0;

	vreg = vreg_get(0, "gp4");
	if (enable_disable) {
		if ((rc = vreg_enable(vreg)))
			printk(KERN_ERR "%s: vreg(gp4) enable failed (%d)\n",
			       __func__, rc);
	} else {
		if ((rc = vreg_disable(vreg)))
			printk(KERN_ERR "%s: vreg(gp4) enable failed (%d)\n",
			       __func__, rc);
	}

	if (rc < 0) {
		printk(KERN_ERR "%s: msm_pm_mic_en failed! rc = %d\n",
		       __func__, rc);
	}
/*	else
	{
		printk(KERN_INFO "msm_pm_mic_en %d \n", enable_disable);
	}
*/
	return rc;
}

static ssize_t print_switch_name(struct switch_dev *sdev, char *buf)
{
	switch (switch_get_state(sdev)) {
	case 0:		// NO DEVICE
		return sprintf(buf, "No Device\n");
	case 1:		// Stereo HEADPHONE
		return sprintf(buf, "Stereo HeadPhone\n");
	case 3:		// Stereo HeadSet
		return sprintf(buf, "Stereo Headset\n");
	case 5:		// TV OUT
		return sprintf(buf, "TV OUT\n");
	}
	return -EINVAL;
}

static ssize_t print_h2w_switch_name(struct switch_dev *sdev, char *buf)
{
	switch (switch_get_state(sdev)) {
	case 0:		// NO DEVICE
		return sprintf(buf, "No Device\n");
	case 2:		// Stereo HEADPHONE
		return sprintf(buf, "Stereo HeadPhone\n");
	case 1:		// Stereo HeadSet
		return sprintf(buf, "Stereo Headset\n");
	}
	return -EINVAL;
}

static int do_check_mic(void)
{
	uint8_t val1, val2;
	msm_pm_mic_en(1);
	msleep_interruptible(HEADSET_MIC_BIAS_DELAY);
	val1 = gpio_get_value(HEADSET_SEND_END_0_GPIO);
	val2 = gpio_get_value(HEADSET_SEND_END_1_GPIO);
	if ((val1 == 1) && (val2 == 0))
		return (1);	/* The mic is detected */

	/* No Mic , turn off mic bias */
	msm_pm_mic_en(0);
	return (0);
}

int get_state_value(uint8_t state, uint8_t mic, uint8_t tv_out)
{
	return (state | (mic << 1) | (tv_out << 2));
}

void enable_disable_btn_irq(struct hs_dev *hs, int state)
{
	unsigned long irq_flags;
	local_irq_save(irq_flags);
	if (state) {
		enable_irq(hs->irq_btn_0);
		enable_irq(hs->irq_btn_1);
	} else {
		disable_irq(hs->irq_btn_0);
		disable_irq(hs->irq_btn_1);
	}
	local_irq_restore(irq_flags);
}

static int do_set_state(struct hs_dev *hs, uint8_t new_state)
{
	/*unsigned long irq_flags; */
	int mic_state;

	hsdev->detect_state = 1;
	hrtimer_start(&hs->timer, ktime_set(HEADSET_BTN_HOLD_TIMER, 0),
		      HRTIMER_MODE_REL);

	if (hsdev->btn_in_test)
		enable_disable_btn_irq(hs, 0);

	hsdev->btn_in_test = 0;
	if (new_state != hs->state) {
		hs->state = new_state;
		if (new_state) {	/* headset is ON. cheak mic */
			hs->mic = do_check_mic();	/* mic and maybe the BTN */
		} else {
			if (hs->mic)
				msm_pm_mic_en(0);
			if (hsdev->btn_in_test)
				enable_disable_btn_irq(hs, 0);
			hs->mic = 0;
			hsdev->btn_in_test = 0;
		}
		hs_switch_set_state(hs, get_state_value(hs->state,
							hs->mic, hs->tv_out));
	} else if (hs->state) {
		/* if state did not changed and ON then retest the mic it
		   can the disconnected if HS is hlf removed ( it is very
		   noisy :) */
		if (hs->mic) {
			mic_state = do_check_mic();	/*mic and maybe the BTN */
			if (hs->mic != mic_state) {
				hs->mic = mic_state;
				hs_switch_set_state(hs,
						    get_state_value(hs->state,
								    hs->mic,
								    hs->
								    tv_out));
			}
		}
	}

	return 0;
}

static void do_button_event(struct hs_dev *hs, uint8_t state)
{
	if (state == hs->btn_state)
		return;

	/*printk(KERN_INFO "HEADSET: HeadSet Button  %s , mic %d \n", state ? "PRESSED":"RELEASED",hs->mic); */

	hs->btn_state = state;
	input_report_key(hs->input, KEY_MEDIA, state);
	input_sync(hs->input);
}

static void hs_work_func(struct work_struct *work)
{
	unsigned long irq_flags;
	struct hs_dev *hs = container_of(work, struct hs_dev, work);

	/* printk(KERN_INFO "HEADSET: hs_work_fun  state %s , mic %d \n", hsdev->detect_state,hs->mic); */

	if (!hsdev->detect_state) {
		/* headset detect */
		do_set_state(hs, !gpio_get_value(HEADSET_DETECT_GPIO));
		local_irq_save(irq_flags);
		enable_irq(hs->irq);
		local_irq_restore(irq_flags);
	} else {
		/* enable button irq */
		if (!hs->mic) {
			hs->mic = do_check_mic();
			if (hs->mic) {	/*retest  mic */
				/* Found the mic . report it */
				hs_switch_set_state(hs,
						    get_state_value(hs->state,
								    hs->mic,
								    hs->
								    tv_out));
			}
		}
		if ((hs->mic)) {	/* there is the mic and may be the BTN */
			/* Enable button detect interrupt if headset is in and btn_int is disabled */
			if (!hsdev->btn_in_test) {
				enable_disable_btn_irq(hs, 1);
				hsdev->btn_in_test = 1;
			}
		}
	}
}

static enum hrtimer_restart hs_detect_event_timer_func(struct hrtimer *data)
{
	struct hs_dev *hs = container_of(data, struct hs_dev, timer);
	schedule_work(&hs->work);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart hs_btn_event_timer_func(struct hrtimer *data)
{
	/*unsigned long irq_flags; */
	struct hs_dev *hs = container_of(data, struct hs_dev, btn_timer);
	uint8_t val1, val2, hs_detect;
	val1 = gpio_get_value(HEADSET_SEND_END_0_GPIO);
	val2 = gpio_get_value(HEADSET_SEND_END_1_GPIO);
	hs_detect = gpio_get_value(HEADSET_DETECT_GPIO);
	if (!hs_detect) {	/* HS is still in  check btn */
		if (val1 == val2) {
			/* try  several times  to avoid false triggering */
			if (hs->btn_counter < HEADSET_BTN_DETECT_REPEATS) {
				hs->btn_off_counter = 0;
				hs->btn_counter++;
				return HRTIMER_RESTART;
			} else
				do_button_event(hs, 1);	// Button pressed
		} else {
			hs->btn_counter = 0;
			if (hs->btn_off_counter < HEADSET_BTN_DETECT_REPEATS) {
				hs->btn_off_counter++;
				return HRTIMER_RESTART;
			} else
				do_button_event(hs, 0);	// Button released
		}
	} else {		/* HS was removed during BTN test */
		hsdev->btn_in_test = 0;
		hs->btn_counter = 0;
		hs->btn_off_counter = 0;
		return HRTIMER_NORESTART;
	}
	/* RE ENABLE button detect interrupt  */
	hs->btn_counter = 0;
	hs->btn_off_counter = 0;

	if (!hsdev->btn_in_test) {
		enable_disable_btn_irq(hs, 1);
		hsdev->btn_in_test = 1;
	}

	return HRTIMER_NORESTART;
}

static irqreturn_t hs_irq_handler(int irq, void *dev_id)
{
	unsigned long irq_flags;
	struct hs_dev *hs = (struct hs_dev *)dev_id;
	local_irq_save(irq_flags);
	disable_irq(hs->irq);
	/* DISABLE button detect interrupt  because HeadSet status is changing */
	/*printk(KERN_INFO "HEADSET:hs_irq_handler  state %s , mic %d \n", hs->detect_state,hs->mic); */

	if (hsdev->btn_in_test) {
		enable_disable_btn_irq(hs, 0);
		hsdev->btn_in_test = 0;
	}
	local_irq_restore(irq_flags);
	hsdev->detect_state = 0;
	hrtimer_start(&hs->timer, hs->debounce_time, HRTIMER_MODE_REL);

	return IRQ_HANDLED;
}

static irqreturn_t hs_button_irq_handler(int irq, void *dev_id)
{
	/*unsigned long irq_flags; */
	struct hs_dev *hs = (struct hs_dev *)dev_id;

	/* DISABLE BTN INT  and start debounce timer if headset is in */
	if (hsdev->btn_in_test) {
		enable_disable_btn_irq(hs, 0);
		hsdev->btn_in_test = 0;
	}
	if (hsdev->state)
		hrtimer_start(&hs->btn_timer, hs->btn_debounce_time,
			      HRTIMER_MODE_REL);

	return IRQ_HANDLED;
}

#if defined(CONFIG_DEBUG_FS)
static int hs_debug_set(void *data, u64 val)
{
	switch_set_state(&hsdev->sdev, (int)val);
	return 0;
}

static int hs_debug_get(void *data, u64 * val)
{
	uint8_t val1, val2;
	val1 = gpio_get_value(HEADSET_SEND_END_0_GPIO);
	val2 = gpio_get_value(HEADSET_SEND_END_1_GPIO);
	printk(KERN_INFO "GPIO are  %d   :  %d \n", val1, val2);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(hs_debug_fops, hs_debug_get, hs_debug_set, "%llu\n");

static int __init hs_debug_init(void)
{
	struct dentry *dent;

	dent = debugfs_create_dir("hs", 0);
	if (IS_ERR(dent))
		return PTR_ERR(dent);

	debugfs_create_file("state", 0644, dent, NULL, &hs_debug_fops);

	return 0;
}

device_initcall(hs_debug_init);
#endif

static int __init hs_probe(struct platform_device *pdev)
{
	struct hs_dev *hs;
	unsigned long request_flags =
	    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
	unsigned long irq_flags;
	int rc;

	printk(KERN_INFO "HEADSET: Registering headset driver\n");

	hs = kzalloc(sizeof *hs, GFP_KERNEL);
	if (hs == NULL)
		return -ENOMEM;

	hs->debounce_time = ktime_set(0, HEADSET_DETECT_DEBOUNCE_TIMER);	/* 300 ms */
	hs->btn_debounce_time = ktime_set(0, HEADSET_BTN_DEBOUNCE_TIMER);	/* 10 ms */
	hs->sdev.name = DRIVER_NAME;
	hs->sdev.print_name = print_switch_name;
	hs->sdev_h2w.name = DRIVER_NAME_H2W;
	hs->sdev_h2w.print_name = print_h2w_switch_name;
	hs->btn_in_test = 0;
	hs->btn_state = 0;
	hs->state = 0;

	INIT_WORK(&hs->work, hs_work_func);

	rc = switch_dev_register(&hs->sdev);
	if (rc < 0)
		goto err_switch_dev_register;

	rc = switch_dev_register(&hs->sdev_h2w);
	if (rc < 0)
		goto err_h2w_switch_dev_register;

	rc = gpio_request(HEADSET_DETECT_GPIO, "hs");
	if (rc < 0)
		goto err_request_detect_gpio;

	rc = gpio_request(HEADSET_SEND_END_0_GPIO, "hs_send_end_0");
	if (rc < 0)
		goto err_request_detect_gpio_0;

	rc = gpio_request(HEADSET_SEND_END_1_GPIO, "hs_send_end_1");
	if (rc < 0)
		goto err_request_detect_gpio_1;

	rc = gpio_direction_input(HEADSET_DETECT_GPIO);
	if (rc < 0)
		goto err_set_detect_gpio;

	rc = gpio_direction_input(HEADSET_SEND_END_0_GPIO);
	if (rc < 0)
		goto err_set_detect_gpio;

	rc = gpio_direction_input(HEADSET_SEND_END_1_GPIO);
	if (rc < 0)
		goto err_set_detect_gpio;

	hs->irq = gpio_to_irq(HEADSET_DETECT_GPIO);
	if (hs->irq < 0) {
		rc = hs->irq;
		goto err_get_hs_detect_irq_num_failed;
	}

	hs->irq_btn_0 = gpio_to_irq(HEADSET_SEND_END_0_GPIO);
	if (hs->irq_btn_0 < 0) {
		rc = hs->irq_btn_0;
		goto err_get_hs_detect_irq_num_failed;
	}

	hs->irq_btn_1 = gpio_to_irq(HEADSET_SEND_END_1_GPIO);
	if (hs->irq_btn_1 < 0) {
		rc = hs->irq_btn_1;
		goto err_get_hs_detect_irq_num_failed;
	}

	hrtimer_init(&hs->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hs->timer.function = hs_detect_event_timer_func;

	hrtimer_init(&hs->btn_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hs->btn_timer.function = hs_btn_event_timer_func;

	local_irq_save(irq_flags);

	rc = request_irq(hs->irq, hs_irq_handler, request_flags, "hs_detect",
			 hs);

	if (rc < 0)
		goto err_request_detect_irq;

	rc = irq_set_irq_wake(hs->irq, 1);
	if (rc < 0)
		goto err_request_detect_irq;

	rc = request_irq(hs->irq_btn_0, hs_button_irq_handler, request_flags,
			 "hs_btn_0", hs);
	if (rc < 0)
		goto err_request_detect_irq;

	rc = irq_set_irq_wake(hs->irq_btn_0, 1);
	if (rc < 0)
		goto err_request_detect_irq;

	rc = request_irq(hs->irq_btn_1, hs_button_irq_handler, request_flags,
			 "hs_btn_1", hs);
	if (rc < 0)
		goto err_request_detect_irq;

	rc = irq_set_irq_wake(hs->irq_btn_1, 1);
	if (rc < 0)
		goto err_request_detect_irq;

	/* DISABLE BTN INT if  Headset is not plugged in */
	hs->btn_in_test = 0;
	disable_irq(hs->irq_btn_1);
	disable_irq(hs->irq_btn_0);

	local_irq_restore(irq_flags);

	hs->input = input_allocate_device();
	if (!hs->input) {
		rc = -ENOMEM;
		goto err_request_input_dev;
	}
	input_set_drvdata(hs->input, hs);

	hs->input->name = "headset";
	hs->input->id.vendor	= 0x0001;
	hs->input->id.product	= 1;
	hs->input->id.version	= 1;

	input_set_capability(hs->input, EV_KEY, KEY_MEDIA);
	input_set_capability(hs->input, EV_SW, SW_HEADPHONE_INSERT);
	input_set_capability(hs->input, EV_KEY, KEY_POWER);
	input_set_capability(hs->input, EV_KEY, KEY_END);

	hs->detect_state = 0;

	rc = input_register_device(hs->input);
	if (rc < 0)
		goto err_register_input_dev;

	hsdev = hs;
	return 0;

 err_register_input_dev:
	input_free_device(hs->input);
 err_request_input_dev:
 err_request_detect_irq:
	local_irq_restore(irq_flags);
 err_set_detect_gpio:
 err_get_hs_detect_irq_num_failed:
	gpio_free(HEADSET_SEND_END_1_GPIO);
 err_request_detect_gpio_1:
	gpio_free(HEADSET_SEND_END_0_GPIO);
 err_request_detect_gpio_0:
	gpio_free(HEADSET_DETECT_GPIO);
 err_request_detect_gpio:
	switch_dev_unregister(&hs->sdev_h2w);
 err_h2w_switch_dev_register:
	switch_dev_unregister(&hs->sdev);
 err_switch_dev_register:
	printk(KERN_ERR "HEADSET: Failed to register driver\n");

	return rc;
}

static int hs_remove(struct platform_device *pdev)
{
	input_unregister_device(hsdev->input);
	gpio_free(HEADSET_SEND_END_0_GPIO);
	gpio_free(HEADSET_SEND_END_1_GPIO);
	gpio_free(HEADSET_DETECT_GPIO);
	free_irq(hsdev->irq, 0);
	free_irq(hsdev->irq_btn_0, 0);
	free_irq(hsdev->irq_btn_1, 0);

	switch_dev_unregister(&hsdev->sdev);
	return 0;
}

static struct platform_device hs_device __refdata = {
	.name = DRIVER_NAME,
};

static struct platform_driver hs_driver __refdata = {
	.probe = hs_probe,
	.remove = hs_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   },
};

static int __init hs_init(void)
{
	int ret;

	ret = platform_driver_register(&hs_driver);
	if (ret) {
		printk(KERN_ERR "HEADSET:platform_driver_register error %d \n",
		       ret);
		return ret;
	}
	return platform_device_register(&hs_device);
}
late_initcall(hs_init);

static void __exit hs_exit(void)
{
	platform_device_unregister(&hs_device);
	platform_driver_unregister(&hs_driver);
}
module_exit(hs_exit);

MODULE_DESCRIPTION("3.5 mm headset  driver");
MODULE_AUTHOR("Vladimir Karpovich <Vladimir.Karpovich@motorola.com>");
MODULE_LICENSE("GPL");
