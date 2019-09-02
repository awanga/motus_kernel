#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm_qos_params.h>
#ifdef CONFIG_POWER_SUPPLY
#include <linux/power_supply.h>
#include <linux/phone_battery.h>
#endif

#include <mach/board.h>
#include <mach/msm_iomap.h>
#include <mach/gpio.h>

#include <mach/msm_hsusb.h>
#include <mach/msm_hsusb_hw.h>
#include <mach/msm_rpcrouter.h>
#include <mach/rpc_hsusb.h>
#include <mach/rpc_pmapp.h>
#include <mach/socinfo.h>
#include <mach/vreg.h>

#define USB_LINK_RESET_TIMEOUT      (msecs_to_jiffies(10))

#define SOC_ROC_2_0		0x10002 /* ROC 2.0 */

//static unsigned charger_attached_at_boot;
extern unsigned powerup_reason_charger(void);

struct usb_info {
	unsigned int soc_version;

	unsigned phy_info;
	unsigned in_lpm;

#ifdef CONFIG_POWER_SUPPLY
	struct power_supply *psy_usb;
	struct power_supply *psy_ac;
#endif
	unsigned setup_intr;
} ui;

static unsigned ulpi_read(unsigned reg)
{
	unsigned timeout = 100000;

	/* initiate read operation */
	writel_relaxed(ULPI_RUN | ULPI_READ | ULPI_ADDR(reg),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while ((readl_relaxed(USB_ULPI_VIEWPORT) & ULPI_RUN) && (--timeout)) ;

	if (timeout == 0) {
		pr_debug("ulpi_read: timeout %08x\n",
			readl_relaxed(USB_ULPI_VIEWPORT));
		return 0xffffffff;
	}
	return ULPI_DATA_READ(readl_relaxed(USB_ULPI_VIEWPORT));
}

static int ulpi_write(unsigned val, unsigned reg)
{
	unsigned timeout = 10000;

	/* initiate write operation */
	writel_relaxed(ULPI_RUN | ULPI_WRITE |
	       ULPI_ADDR(reg) | ULPI_DATA(val),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while((readl_relaxed(USB_ULPI_VIEWPORT) & ULPI_RUN) && (--timeout)) ;

	if (timeout == 0) {
		pr_debug("ulpi_write: timeout\n");
		return -1;
	}

	return 0;
}

static int usb_wakeup_phy(struct usb_info *ui)
{
	int i;

	writel_relaxed(readl_relaxed(USB_USBCMD) & ~ULPI_STP_CTRL, USB_USBCMD);

	/* some circuits automatically clear PHCD bit */
	for (i = 0; i < 5 && (readl_relaxed(USB_PORTSC) & PORTSC_PHCD); i++) {
		writel_relaxed(readl_relaxed(USB_PORTSC) & ~PORTSC_PHCD, USB_PORTSC);
		msleep(1);
	}

	if ((readl_relaxed(USB_PORTSC) & PORTSC_PHCD)) {
		pr_err("%s: cannot clear phcd bit\n", __func__);
		return -1;
	}

	return 0;
}

/* SW workarounds
Issue#2		- Integrated PHY Calibration
Symptom		- Electrical compliance failure in eye-diagram tests
SW workaround		- Try to raise amplitude to 400mV

Issue#3		- AHB Posted Writes
Symptom		- USB stability
SW workaround		- This programs xtor ON, BURST disabled and
			unspecified length of INCR burst enabled
*/
static int usb_hw_reset(struct usb_info *ui)
{
	unsigned i;
	unsigned long timeout;
	unsigned val = 0;

	/* reset the phy before resetting link */
	if (readl_relaxed(USB_PORTSC) & PORTSC_PHCD)
		usb_wakeup_phy(ui);
	/* rpc call for phy_reset */
	msm_hsusb_phy_reset();
	/* Give some delay to settle phy after reset */
	msleep(100);

	/* RESET */
	writel_relaxed(USBCMD_RESET, USB_USBCMD);
	timeout = jiffies + USB_LINK_RESET_TIMEOUT;
	while (readl_relaxed(USB_USBCMD) & USBCMD_RESET) {
		if (time_after(jiffies, timeout)) {
			pr_err("%s: usb link reset timeout\n", __func__);
			break;
		}
		msleep(1);
	}

	/* select DEVICE mode with SDIS active */
	writel_relaxed((USBMODE_SDIS | USBMODE_DEVICE), USB_USBMODE);
	msleep(1);

	/* select ULPI phy */
	i = (readl_relaxed(USB_PORTSC) & ~PORTSC_PTS);
	writel_relaxed(i | PORTSC_PTS_ULPI, USB_PORTSC);

	writel_relaxed((readl_relaxed(USB_USBCMD) & ~USBCMD_ITC_MASK) | USBCMD_ITC(8),
								USB_USBCMD);

	/* If the target is 7x01 and roc version is > 1.2, set
	 * the AHB mode to 2 for maximum performance, else set
	 * it to 1, to bypass the AHB transactor for stability.
	 */
	if (PHY_TYPE(ui->phy_info) == USB_PHY_EXTERNAL) {
		if (ui->soc_version >= SOC_ROC_2_0)
			writel_relaxed(0x02, USB_ROC_AHB_MODE);
		else
			writel_relaxed(0x01, USB_ROC_AHB_MODE);
	} else {
		ulpi_write(ULPI_AMPLITUDE_MAX, ULPI_CONFIG_REG);

		writel_relaxed(0x0, USB_AHB_BURST);
		writel_relaxed(0x00, USB_AHB_MODE);
	}

	/* TBD: do we have to add DpRise, ChargerRise and
	 * IdFloatRise for 45nm
	 */
	/* Disable VbusValid and SessionEnd comparators */
	val = ULPI_VBUS_VALID | ULPI_SESS_END;

	/* enable id interrupt only when transceiver is available */
	/*if (ui->xceiv)
		writel_relaxed(readl_relaxed(USB_OTGSC) | OTGSC_BSVIE | OTGSC_IDIE, USB_OTGSC);
	else {*/
		writel_relaxed((readl_relaxed(USB_OTGSC) | OTGSC_BSVIE) & ~OTGSC_IDPU,
							USB_OTGSC);
		ulpi_write(ULPI_IDPU, ULPI_OTG_CTRL_CLR);
		val |= ULPI_HOST_DISCONNECT | ULPI_ID_GND;
	/*}*/
	ulpi_write(val, ULPI_INT_RISE_CLR);
	ulpi_write(val, ULPI_INT_FALL_CLR);

	//writel_relaxed(ui->dma, USB_ENDPOINTLISTADDR);

	return 0;
}

static int usb_chg_detect_type(struct usb_info *ui)
{
	int ret = USB_CHG_TYPE__INVALID;

	msleep(10);
	switch (PHY_TYPE(ui->phy_info)) {
	case USB_PHY_EXTERNAL:
		if (ulpi_write(0x10, 0x3A))
			return ret;

		/* 50ms is requried for charging circuit to powerup
		 * and start functioning
		 */
		msleep(50);
		if ((readl_relaxed(USB_PORTSC) & PORTSC_LS) == PORTSC_LS)
			ret = USB_CHG_TYPE__WALLCHARGER;
		else
			ret = USB_CHG_TYPE__SDP;

		ulpi_write(0x10, 0x3B);
		break;
	case USB_PHY_INTEGRATED:
	{
		unsigned int i;
		unsigned int extchgctrl = 0;
		unsigned int chgtype = 0;

		switch (PHY_MODEL(ui->phy_info)) {
		case USB_PHY_MODEL_65NM:
			extchgctrl = ULPI_EXTCHGCTRL_65NM;
			chgtype = ULPI_CHGTYPE_65NM;
			break;
		case USB_PHY_MODEL_180NM:
			extchgctrl = ULPI_EXTCHGCTRL_180NM;
			chgtype = ULPI_CHGTYPE_180NM;
			break;
		default:
			pr_err("%s: undefined phy model\n", __func__);
			break;
		}

		/* control charging detection through ULPI */
		i = ulpi_read(ULPI_CHG_DETECT_REG);
		i &= ~extchgctrl;
		ulpi_write(i, ULPI_CHG_DETECT_REG);

		/* power on charger detection circuit */
		i = ulpi_read(ULPI_CHG_DETECT_REG);
		i &= ~ULPI_CHGDETON;
		ulpi_write(i, ULPI_CHG_DETECT_REG);

		msleep(10);
		/* enable charger detection */
		i = ulpi_read(ULPI_CHG_DETECT_REG);
		i &= ~ULPI_CHGDETEN;
		ulpi_write(i, ULPI_CHG_DETECT_REG);

		msleep(10);
		/* read charger type */
		i = ulpi_read(ULPI_CHG_DETECT_REG);
		if (i & chgtype)
			ret = USB_CHG_TYPE__WALLCHARGER;
		else
			ret = USB_CHG_TYPE__SDP;

		/* disable charger circuit */
		i = ulpi_read(ULPI_CHG_DETECT_REG);
		i |= (ULPI_CHGDETEN | ULPI_CHGDETON);
		ulpi_write(i, ULPI_CHG_DETECT_REG);
		break;
	}
	default:
		pr_err("%s: undefined phy type\n", __func__);
	}

	return ret;
}

static void usb_chg_set_type(enum chg_type chg_type)
{
	unsigned i = 0;

	while ((i < 75) && (B_SESSION_VALID & readl_relaxed(USB_OTGSC))) {
		chg_type = usb_chg_detect_type(&ui);
		if (chg_type == USB_CHG_TYPE__WALLCHARGER) {
			pr_info("\n**** Charger Type: WALL CHARGER\n\n");
			msm_chg_usb_charger_connected(USB_CHG_TYPE__WALLCHARGER);
#ifdef CONFIG_BATTERY_MOT
			(void) set_battery_property(POWER_SUPPLY_PROP_STATUS,
						 POWER_SUPPLY_STATUS_CHARGING);
#endif
			msm_chg_usb_i_is_available(1500);
			break;
		} else if (chg_type == USB_CHG_TYPE__SDP) {
			msleep(25);
			if (ui.setup_intr) {
				pr_info("\n**** Charger Type: HOST PC\n\n");
				msm_chg_usb_charger_connected(USB_CHG_TYPE__SDP);
				break;
			}
			i++;
		} else {
			pr_err("%s:undefned charger type", __func__);
			break;
		}
	}
}

static void usb_vbus_online(struct usb_info *ui)
{
	if (ui->in_lpm) {
		//if (usb_lpm_config_gpio)
		//	usb_lpm_config_gpio(0);
		//usb_vreg_enable(ui);
		//usb_clk_enable(ui);
		usb_wakeup_phy(ui);
		ui->in_lpm = 0;
	}

	usb_hw_reset(ui);
}

static void usb_vbus_offline(struct usb_info *ui)
{
	unsigned long timeout;
	unsigned val = 0;
	int phy_stuck;
	int retries = 5;

	/* reset h/w at cable disconnetion becasuse
	 * of h/w bugs and to flush any resource that
	 * h/w might be holding
	 */
	if (readl_relaxed(USB_PORTSC) & PORTSC_PHCD)
		usb_wakeup_phy(ui);

reset_phy:
	msm_hsusb_phy_reset();
	/* Give some delay to settle phy after reset */
	msleep(100);

	writel_relaxed(USBCMD_RESET, USB_USBCMD);
	timeout = jiffies + USB_LINK_RESET_TIMEOUT;
	while (readl_relaxed(USB_USBCMD) & USBCMD_RESET) {
		if (time_after(jiffies, timeout)) {
			pr_err("%s: usb link reset timeout\n", __func__);
			break;
		}
		msleep(1);
	}

	/* Disable VbusValid and SessionEnd comparators */
	val = ULPI_VBUS_VALID | ULPI_SESS_END;

	/* enable id interrupt only when transceiver is available */
	/*if (ui->xceiv)
		writel_relaxed(readl_relaxed(USB_OTGSC) | OTGSC_BSVIE | OTGSC_IDIE, USB_OTGSC);
	else */{
		writel_relaxed((readl_relaxed(USB_OTGSC) | OTGSC_BSVIE) & ~OTGSC_IDPU,
							USB_OTGSC);
		ulpi_write(ULPI_IDPU, ULPI_OTG_CTRL_CLR);
		val |= ULPI_HOST_DISCONNECT | ULPI_ID_GND;
	}
	ulpi_write(val, ULPI_INT_RISE_CLR);
	phy_stuck = ulpi_write(val, ULPI_INT_FALL_CLR);
	if (phy_stuck && retries--) {
		pr_info("%s: phy could be stuck, reset again\n", __func__);
		goto reset_phy;
	}
}


#ifdef CONFIG_USB_EHCI_MSM_72K
static void hsusb_gpio_init(void)
{
	if (gpio_request(111, "ulpi_data_0"))
		pr_err("failed to request gpio ulpi_data_0\n");
	if (gpio_request(112, "ulpi_data_1"))
		pr_err("failed to request gpio ulpi_data_1\n");
	if (gpio_request(113, "ulpi_data_2"))
		pr_err("failed to request gpio ulpi_data_2\n");
	if (gpio_request(114, "ulpi_data_3"))
		pr_err("failed to request gpio ulpi_data_3\n");
	if (gpio_request(115, "ulpi_data_4"))
		pr_err("failed to request gpio ulpi_data_4\n");
	if (gpio_request(116, "ulpi_data_5"))
		pr_err("failed to request gpio ulpi_data_5\n");
	if (gpio_request(117, "ulpi_data_6"))
		pr_err("failed to request gpio ulpi_data_6\n");
	if (gpio_request(118, "ulpi_data_7"))
		pr_err("failed to request gpio ulpi_data_7\n");
	if (gpio_request(119, "ulpi_dir"))
		pr_err("failed to request gpio ulpi_dir\n");
	if (gpio_request(120, "ulpi_next"))
		pr_err("failed to request gpio ulpi_next\n");
	if (gpio_request(121, "ulpi_stop"))
		pr_err("failed to request gpio ulpi_stop\n");
}

static unsigned usb_gpio_lpm_config[] = {
	GPIO_CFG(111, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* DATA 0 */
	GPIO_CFG(112, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* DATA 1 */
	GPIO_CFG(113, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* DATA 2 */
	GPIO_CFG(114, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* DATA 3 */
	GPIO_CFG(115, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* DATA 4 */
	GPIO_CFG(116, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* DATA 5 */
	GPIO_CFG(117, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* DATA 6 */
	GPIO_CFG(118, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* DATA 7 */
	GPIO_CFG(119, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* DIR */
	GPIO_CFG(120, 1, GPIO_CFG_INPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* NEXT */
	GPIO_CFG(121, 1, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_8MA),	/* STOP */
};

static unsigned usb_gpio_lpm_unconfig[] = {
	GPIO_CFG(111, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* DATA 0 */
	GPIO_CFG(112, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* DATA 1 */
	GPIO_CFG(113, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* DATA 2 */
	GPIO_CFG(114, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* DATA 3 */
	GPIO_CFG(115, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* DATA 4 */
	GPIO_CFG(116, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* DATA 5 */
	GPIO_CFG(117, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* DATA 6 */
	GPIO_CFG(118, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* DATA 7 */
	GPIO_CFG(119, 1, GPIO_CFG_INPUT,  GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* DIR */
	GPIO_CFG(120, 1, GPIO_CFG_INPUT,  GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), /* NEXT */
	GPIO_CFG(121, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP,   GPIO_CFG_8MA), /* STOP */
};

static int usb_config_gpio(int config)
{
	int pin, rc;

	if (config) {
 		for (pin = 0; pin < ARRAY_SIZE(usb_gpio_lpm_config); pin++) {
			rc = gpio_tlmm_config(usb_gpio_lpm_config[pin],
								GPIO_CFG_ENABLE);
			if (rc) {
				printk(KERN_ERR
					"%s: gpio_tlmm_config(%#x)=%d\n",
				__func__, usb_gpio_lpm_config[pin], rc);
				return -EIO;
			}
		}
	} else {
		for (pin = 0; pin < ARRAY_SIZE(usb_gpio_lpm_unconfig); pin++) {
			rc = gpio_tlmm_config(usb_gpio_lpm_unconfig[pin],
								GPIO_CFG_ENABLE);
			if (rc) {
				printk(KERN_ERR
					"%s: gpio_tlmm_config(%#x)=%d\n",
				__func__, usb_gpio_lpm_config[pin], rc);
				return -EIO;
			}
		}
	}

	return 0;
}
#endif

void mot_hsusb_init(void)
{
#ifdef CONFIG_USB_EHCI_MSM_72K
	memset(&ui, sizeof(struct usb_info), 0);
	ui.soc_version = socinfo_get_version();

	hsusb_gpio_init();
#endif
}

#ifdef CONFIG_USB_MSM_OTG_72K
static int hsusb_rpc_connect(int connect)
{
	if (connect)
		return msm_hsusb_rpc_connect();
	else
		return msm_hsusb_rpc_close();
}

static void msm_hsusb_vbus_power(unsigned phy_info, int on)
{
	ui.phy_info = phy_info;

	if (on)
		usb_vbus_online(&ui);
	else
		usb_vbus_offline(&ui);
}

static struct pm_qos_request_list msm_otg_pm_qos;

struct msm_otg_platform_data msm_otg_pdata = {
	.vbus_power		 = msm_hsusb_vbus_power,
	.rpc_connect		 = hsusb_rpc_connect,
	.init_gpio		 = usb_config_gpio,
	.chg_connected		 = usb_chg_set_type,
	.pm_qos_req_dma		 = &msm_otg_pm_qos,
};
#endif

struct msm_hsusb_gadget_platform_data msm_hsusb_peripheral_pdata;
