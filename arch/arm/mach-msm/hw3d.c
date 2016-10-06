/* arch/arm/mach-msm/hw3d.c
 *
 * Register/Interrupt access for userspace 3D library.
 *
 * Copyright (C) 2007 Google, Inc.
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
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/clk.h>
#include <linux/android_pmem.h>
#include <linux/file.h>
#include <linux/earlysuspend.h>
#include <linux/wakelock.h>
#include <mach/board.h>

#define DECLARE_MUTEX(name)	\
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)

#define REGION_PAGE_ID(addr)		\
	((((uint32_t)(addr)) >> (28 - PAGE_SHIFT)) & 0xf)
#define REGION_PAGE_OFFS(addr)		\
	((((uint32_t)(addr)) & ~(0xf << (28 - PAGE_SHIFT))))

enum {
	HW3D_EBI		= 0,
	HW3D_SMI		= 1,
	HW3D_REGS		= 2,

	HW3D_NUM_REGIONS	= HW3D_REGS + 1,
};

struct mem_region {
	unsigned long		pbase;
	unsigned long		size;
	void __iomem		*vbase;
};

struct hw3d_info {
	struct miscdevice	master_dev;
	struct miscdevice	client_dev;

	struct clk		*grp_clk;
	struct clk		*imem_clk;
	int			irq;

	struct mem_region	regions[HW3D_NUM_REGIONS];

	wait_queue_head_t	irq_wq;
	bool			irq_pending;
	bool			irq_en;
	bool			suspending;
	bool			revoking;
	bool			enabled;

	struct timer_list	revoke_timer;
	wait_queue_head_t	revoke_wq;
	wait_queue_head_t	revoke_done_wq;

	spinlock_t		lock;

	struct file		*client_file;
	struct task_struct	*client_task;

	struct early_suspend	early_suspend;
	struct wake_lock	wake_lock;
};
static struct hw3d_info *hw3d_info;

struct hw3d_data {
	struct vm_area_struct	*vmas[HW3D_NUM_REGIONS];
	struct mutex		mutex;
	bool			closing;
};

static DEFINE_SPINLOCK(hw3d_lock);
static DECLARE_WAIT_QUEUE_HEAD(hw3d_queue);
static int hw3d_pending;
static int hw3d_disabled;

static struct clk *grp_clk;
static struct clk *imem_clk;
DECLARE_MUTEX(hw3d_sem);
static unsigned int hw3d_granted;
static struct file *hw3d_granted_file;

static int hw3d_release(struct inode *, struct file *);
static long hw3d_ioctl(struct file *, unsigned int, unsigned long);

static bool is_master(struct hw3d_info *info, struct file *file)
{
	int fmin = MINOR(file->f_dentry->d_inode->i_rdev);
	return fmin == info->master_dev.minor;
}

static bool is_client(struct hw3d_info *info, struct file *file)
{
	int fmin = MINOR(file->f_dentry->d_inode->i_rdev);
	return fmin == info->client_dev.minor;
}

inline static void locked_hw3d_irq_disable(struct hw3d_info *info)
{
	if (info->irq_en) {
		disable_irq_nosync(info->irq);
		info->irq_en = 0;
	}
}

inline static void locked_hw3d_irq_enable(struct hw3d_info *info)
{
	if (!info->irq_en) {
		enable_irq(info->irq);
		info->irq_en = 1;
	}
}

static irqreturn_t hw3d_irq_handler(int irq, void *data)
{
	unsigned long flags;

	spin_lock_irqsave(&hw3d_lock, flags);
	if (!hw3d_disabled) {
		disable_irq(INT_GRAPHICS);
		hw3d_disabled = 1;
	}
	hw3d_pending = 1;
	spin_unlock_irqrestore(&hw3d_lock, flags);

	wake_up(&hw3d_queue);

	return IRQ_HANDLED;
}

static void hw3d_disable_interrupt(void)
{
	unsigned long flags;
	spin_lock_irqsave(&hw3d_lock, flags);
	if (!hw3d_disabled) {
		disable_irq(INT_GRAPHICS);
		hw3d_disabled = 1;
	}
	spin_unlock_irqrestore(&hw3d_lock, flags);
}

static long hw3d_wait_for_interrupt(void)
{
	unsigned long flags;
	int ret;

	for (;;) {
		spin_lock_irqsave(&hw3d_lock, flags);
		if (hw3d_pending) {
			hw3d_pending = 0;
			spin_unlock_irqrestore(&hw3d_lock, flags);
			return 0;
		}
		if (hw3d_disabled) {
			hw3d_disabled = 0;
			enable_irq(INT_GRAPHICS);
		}
		spin_unlock_irqrestore(&hw3d_lock, flags);

		ret = wait_event_interruptible(hw3d_queue, hw3d_pending);
		if (ret < 0) {
			hw3d_disable_interrupt();
			return ret;
		}
	}

	return 0;
}

#define HW3D_REGS_LEN 0x100000

bool is_msm_hw3d_file(struct file *file)
{
	struct hw3d_info *info = hw3d_info;
	if (MAJOR(file->f_dentry->d_inode->i_rdev) == MISC_MAJOR &&
	    (is_master(info, file) || is_client(info, file)))
		return 1;
	return 0;
}

void put_msm_hw3d_file(struct file *file)
{
	if (!is_msm_hw3d_file(file))
		return;
	fput(file);
}

static long hw3d_revoke_gpu(struct file *file)
{
	int ret = 0;
	unsigned long user_start, user_len;
	struct pmem_region region = {.offset = 0x0, .len = HW3D_REGS_LEN};

	down(&hw3d_sem);
	if (!hw3d_granted)
		goto end;
	/* revoke the pmem region completely */
	if ((ret = pmem_remap(&region, file, PMEM_UNMAP)))
		goto end;
	get_pmem_user_addr(file, &user_start, &user_len);
	/* reset the gpu */
	clk_disable(grp_clk);
	clk_disable(imem_clk);
	hw3d_granted = 0;
end:
	up(&hw3d_sem);
	return ret;
}

static long hw3d_grant_gpu(struct file *file)
{
	int ret = 0;
	struct pmem_region region = {.offset = 0x0, .len = HW3D_REGS_LEN};

	down(&hw3d_sem);
	if (hw3d_granted) {
		ret = -1;
		goto end;
	}
	/* map the registers */
	if ((ret = pmem_remap(&region, file, PMEM_MAP)))
		goto end;
	clk_enable(grp_clk);
	clk_enable(imem_clk);
	hw3d_granted = 1;
	hw3d_granted_file = file;
end:
	up(&hw3d_sem);
	return ret;
}

static int hw3d_release(struct inode *inode, struct file *file)
{
	down(&hw3d_sem);
	/* if the gpu is in use, and its inuse by the file that was released */
	if (hw3d_granted && (file == hw3d_granted_file)) {
		clk_disable(grp_clk);
		clk_disable(imem_clk);
		hw3d_granted = 0;
		hw3d_granted_file = NULL;
	}
	up(&hw3d_sem);
	return 0;
}

static long hw3d_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		case HW3D_REVOKE_GPU:
			return hw3d_revoke_gpu(file);
			break;
		case HW3D_GRANT_GPU:
			return hw3d_grant_gpu(file);
			break;
		case HW3D_WAIT_FOR_INTERRUPT:
			return hw3d_wait_for_interrupt();
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static struct android_pmem_platform_data pmem_data = {
	.name = "hw3d",
	.start = 0xA0000000,
	.size = 0x100000,
	.allocator_type = PMEM_ALLOCATORTYPE_ALLORNOTHING,
	.cached = 0,
};

static int __init hw3d_init(void)
{
	int ret;

	grp_clk = clk_get(NULL, "grp_clk");
	if (IS_ERR(grp_clk))
		return PTR_ERR(grp_clk);

	imem_clk = clk_get(NULL, "imem_clk");
	if (IS_ERR(imem_clk)) {
		clk_put(grp_clk);
		return PTR_ERR(imem_clk);
	}
	ret = request_irq(INT_GRAPHICS, hw3d_irq_handler,
			  IRQF_TRIGGER_HIGH, "hw3d", 0);
	if (ret) {
		clk_put(grp_clk);
		clk_put(imem_clk);
		return ret;
	}
	hw3d_disable_interrupt();
	hw3d_granted = 0;

	return pmem_setup(&pmem_data, hw3d_ioctl, hw3d_release);
}

device_initcall(hw3d_init);
