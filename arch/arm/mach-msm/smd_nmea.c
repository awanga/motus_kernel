/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora Forum nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * Alternatively, provided that this notice is retained in full, this software
 * may be relicensed by the recipient under the terms of the GNU General Public
 * License version 2 ("GPL") and only version 2, in which case the provisions of
 * the GPL apply INSTEAD OF those given above.  If the recipient relicenses the
 * software under the GPL, then the identification text in the MODULE_LICENSE
 * macro must be changed to reflect "GPLv2" instead of "Dual BSD/GPL".  Once a
 * recipient changes the license terms to the GPL, subsequent recipients shall
 * not relicense under alternate licensing terms, including the BSD or dual
 * BSD/GPL terms.  In addition, the following license statement immediately
 * below and between the words START and END shall also then apply when this
 * software is relicensed under the GPL:
 *
 * START
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 and only version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * END
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * SMD NMEA Driver -- Provides GPS NMEA device to SMD port interface.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>

#include <mach/msm_smd.h>

#define MAX_BUF_SIZE 200

static DEFINE_MUTEX(nmea_ch_lock);
static DEFINE_MUTEX(nmea_rx_buf_lock);

static DECLARE_WAIT_QUEUE_HEAD(nmea_wait_queue);

struct nmea_device_t {
	struct miscdevice misc;

	struct smd_channel *ch;

	unsigned char rx_buf[MAX_BUF_SIZE];
	unsigned int bytes_read;
};

struct nmea_device_t *nmea_devp;

static void nmea_work_func(struct work_struct *ws)
{
	int sz;

	for (;;) {
		sz = smd_cur_packet_size(nmea_devp->ch);
		if (sz == 0)
			break;
		if (sz > smd_read_avail(nmea_devp->ch))
			break;
		if (sz > MAX_BUF_SIZE) {
			smd_read(nmea_devp->ch, 0, sz);
			continue;
		}

		mutex_lock(&nmea_rx_buf_lock);
		if (smd_read(nmea_devp->ch, nmea_devp->rx_buf, sz) != sz) {
			mutex_unlock(&nmea_rx_buf_lock);
			printk(KERN_ERR "nmea: not enough data?!\n");
			continue;
		}
		nmea_devp->bytes_read = sz;
		mutex_unlock(&nmea_rx_buf_lock);
		wake_up_interruptible(&nmea_wait_queue);
	}
}

struct workqueue_struct *nmea_wq;
static DECLARE_WORK(nmea_work, nmea_work_func);

static void nmea_notify(void *priv, unsigned event)
{
	switch (event) {
	case SMD_EVENT_DATA: {
		int sz;
		sz = smd_cur_packet_size(nmea_devp->ch);
		if ((sz > 0) && (sz <= smd_read_avail(nmea_devp->ch)))
			queue_work(nmea_wq, &nmea_work);
		break;
	}
	case SMD_EVENT_OPEN:
		printk(KERN_INFO "nmea: smd opened\n");
		break;
	case SMD_EVENT_CLOSE:
		printk(KERN_INFO "nmea: smd closed\n");
		break;
	}
}

static ssize_t nmea_read(struct file *fp, char __user *buf,
			 size_t count, loff_t *pos)
{
	int r;
	int bytes_read;

	r = wait_event_interruptible(nmea_wait_queue,
				nmea_devp->bytes_read);
	if (r < 0) {
		/* qualify error message */
		if (r != -ERESTARTSYS) {
			/* we get this anytime a signal comes in */
			printk(KERN_ERR "ERROR:%s:%i:%s: "
				"wait_event_interruptible ret %i\n",
				__FILE__,
				__LINE__,
				__func__,
				r
				);
		}
		return r;
	}

	mutex_lock(&nmea_rx_buf_lock);
	bytes_read = nmea_devp->bytes_read;
	nmea_devp->bytes_read = 0;
#ifdef CONFIG_MACH_MOT
	if (bytes_read > count)
		bytes_read = count;
#endif
	r = copy_to_user(buf, nmea_devp->rx_buf, bytes_read);
	mutex_unlock(&nmea_rx_buf_lock);

	if (r > 0) {
		printk(KERN_ERR "ERROR:%s:%i:%s: "
			"copy_to_user could not copy %i bytes.\n",
			__FILE__,
			__LINE__,
			__func__,
			r);
		return r;
	}

	return bytes_read;
}

static int nmea_open(struct inode *ip, struct file *fp)
{
	int r = 0;

	mutex_lock(&nmea_ch_lock);
	if (nmea_devp->ch == 0)
		r = smd_open("GPSNMEA", &nmea_devp->ch, nmea_devp, nmea_notify);
	mutex_unlock(&nmea_ch_lock);

	return r;
}

static int nmea_release(struct inode *ip, struct file *fp)
{
	int r = 0;

	mutex_lock(&nmea_ch_lock);
	if (nmea_devp->ch != 0) {
		r = smd_close(nmea_devp->ch);
		nmea_devp->ch = 0;
	}
	mutex_unlock(&nmea_ch_lock);

	return r;
}

static const struct file_operations nmea_fops = {
	.owner = THIS_MODULE,
	.read = nmea_read,
	.open = nmea_open,
	.release = nmea_release,
};

static struct nmea_device_t nmea_device = {
	.misc = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "nmea",
		.fops = &nmea_fops,
	}
};

static void __exit nmea_exit(void)
{
	destroy_workqueue(nmea_wq);
	misc_deregister(&nmea_device.misc);
}

static int __init nmea_init(void)
{
	int ret;

	nmea_device.bytes_read = 0;
	nmea_devp = &nmea_device;

	nmea_wq = create_singlethread_workqueue("nmea");
	if (nmea_wq == 0)
		return -ENOMEM;

	ret = misc_register(&nmea_device.misc);
	return ret;
}

module_init(nmea_init);
module_exit(nmea_exit);

MODULE_DESCRIPTION("MSM Shared Memory NMEA Driver");
MODULE_LICENSE("GPL v2");
