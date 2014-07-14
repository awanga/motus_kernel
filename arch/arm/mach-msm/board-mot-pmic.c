/*
 * board-mot-pmic.c
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#include <mach/pmic.h>

#include "smd_rpcrouter.h"

/* rpc related */
#define PMIC_RPC_TIMEOUT (5*HZ)

#define SPEAKER_PDEV_NAME	"rs00010001:00000000"
#define SPEAKER_RPC_PROG	0x30000061
#define SPEAKER_RPC_VER	0x00010001

/*MOTOROLA FIX: The following values 
are the correct ones. 
Please refer to 
the following 
files:
1) ../products/76XX/drivers/pmic/pmic3/rpc/lib/sw/pm_lib_rpc.h
2) ../products/76XX/drivers/pmic/pmic3/rpc/lib/sw/pm_lib.xdr
*/

#define RTC_START_PROC 6
#define RTC_STOP_PROC 7
#define RTC_GET_TIME_PROC 8
#define RTC_ENABLE_ALARM_PROC 9
#define RTC_DISABLE_ALARM_PROC 10
#define RTC_GET_ALARM_TIME_PROC 11
#define RTC_GET_ALARM_STATUS_PROC 12
#define RTC_SET_TIME_ADJUST_PROC 13
#define RTC_GET_TIME_ADJUST_PROC 14
#define SET_LED_INTENSITY_PROC 15
#define FLASH_LED_SET_CURRENT_PROC 16
#define FLASH_LED_SET_MODE_PROC 17
#define FLASH_LED_SET_POLARITY_PROC 18
#define SPEAKER_CMD_PROC 19
#define SET_SPEAKER_GAIN_PROC 20
#define VIB_MOT_SET_VOLT_PROC 21
#define VIB_MOT_SET_MODE_PROC 22
#define VIB_MOT_SET_POLARITY_PROC 23
#define VID_EN_PROC 24
#define VID_IS_EN_PROC 25
#define VID_LOAD_DETECT_EN_PROC 26
#define MIC_EN_PROC 27
#define MIC_IS_EN_PROC 28
#define MIC_SET_VOLT_PROC 29
#define MIC_GET_VOLT_PROC 30
#define SPKR_EN_RIGHT_CHAN_PROC 31
#define SPKR_IS_RIGHT_CHAN_EN_PROC 32
#define SPKR_EN_LEFT_CHAN_PROC 33
#define SPKR_IS_LEFT_CHAN_EN_PROC 34
#define SET_SPKR_CONFIGURATION_PROC 35
#define GET_SPKR_CONFIGURATION_PROC 36
#define SPKR_GET_GAIN_PROC 37
#define SPKR_IS_EN_PROC 38

struct std_rpc_req {
	struct rpc_request_hdr req;
	uint32_t value;
};
struct std_rpc_reply {
	struct rpc_reply_hdr hdr;
	uint32_t result;
};
struct get_value_rep {
	struct std_rpc_reply reply_hdr;
	uint32_t MoreData;
	uint32_t value;
};
struct std_rpc_req2 {
	struct rpc_request_hdr hdr;
	uint32_t value1;
	uint32_t value2;
};

/* pmic.c */
extern int modem_to_linux_err(uint err);

static struct msm_rpc_endpoint *endpoint;

static int check_and_connect(void)
{
	if (endpoint != NULL)
		return 0;

	endpoint = msm_rpc_connect(SPEAKER_RPC_PROG, SPEAKER_RPC_VER, 0);
	if (endpoint == NULL) {
		return -ENODEV;
	} else if (IS_ERR(endpoint)) {
		int rc = PTR_ERR(endpoint);
		printk(KERN_ERR "%s: init rpc failed! rc = %d\n",
		       __func__, rc);
		endpoint = NULL;
		return rc;
	}
	return 0;
}

static int do_remote_value(const uint32_t set_value,
			   uint32_t * const get_value,
			   const uint32_t proc)
{
	struct std_rpc_req req;
	struct std_rpc_reply std_rep;
	struct get_value_rep rep;
	void *rep_ptr;
	int rep_size, rc = check_and_connect();

	if (rc) /* connect problem */
		return rc;

	if (get_value != NULL) { /* get value */
		req.value = cpu_to_be32(1); /* output_pointer_not_null */
		rep_size = sizeof(rep);
		rep_ptr = &rep;
	} else { /* set value */
		req.value = cpu_to_be32(set_value);
		rep_size = sizeof(std_rep);
		rep_ptr = &std_rep;
	}
	rc = msm_rpc_call_reply(endpoint, proc,
				&req, sizeof(req),
				rep_ptr, rep_size,
				PMIC_RPC_TIMEOUT);
	if (rc < 0)
		return rc;

	if (get_value != NULL) { /* get value */
		if (!rep.reply_hdr.result) {
			if (!rep.MoreData)
				return -ENOMSG;

			*get_value = be32_to_cpu(rep.value);
		}
		rc = modem_to_linux_err(rep.reply_hdr.result);
	} else {
		rc = modem_to_linux_err(std_rep.result);
	}
	return rc;
}

static int do_std_rpc_req2(struct get_value_rep *rep, uint32_t proc,
				uint32_t value1, uint32_t value2)
{
	struct std_rpc_req2 req;
	int rc;

	rc = check_and_connect();
	if (rc) {
		printk(KERN_ERR "%s: can't make rpc connection!\n", __func__);
		return rc;
	}

	req.value1 = cpu_to_be32(value1);
	req.value2 = cpu_to_be32(value2);

	rc = msm_rpc_call_reply(endpoint, proc,
				    &req, sizeof(req),
				    rep, sizeof(*rep),
				    PMIC_RPC_TIMEOUT);
	if (rc < 0) {
		printk(KERN_ERR
			"%s: msm_rpc_call_reply failed! proc=%d rc=%d\n",
			__func__, proc, rc);
		return rc;
	}

	return modem_to_linux_err(rep->reply_hdr.result);
}

/* Cannot use 'current' as the parameter name because 'current' is defined as
 * a macro to get a pointer to the current task.
 */
int mot_flash_led_set_current(const uint16_t milliamps)
{
	return do_remote_value(milliamps, NULL, FLASH_LED_SET_CURRENT_PROC);
}
EXPORT_SYMBOL(mot_flash_led_set_current);

int mot_set_led_intensity(const enum ledtype type, int level)
{
	struct get_value_rep rep;

	if (type >= LED_TYPE_OUT_OF_RANGE)
		return -EINVAL;

	return do_std_rpc_req2(&rep, SET_LED_INTENSITY_PROC,
				(uint32_t)type, level);
}
EXPORT_SYMBOL(mot_set_led_intensity);
