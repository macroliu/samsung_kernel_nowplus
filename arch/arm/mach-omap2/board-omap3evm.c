/*
 * linux/arch/arm/mach-omap2/board-omap3evm.c
 *
 * Copyright (C) 2008 Texas Instruments
 *
 * Modified from mach-omap2/board-3430sdp.c
 *
 * Initial code: Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/leds.h>
#include <linux/interrupt.h>

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/i2c/twl.h>
#include <linux/usb/otg.h>
#include <linux/smsc911x.h>

#include <linux/regulator/machine.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/board.h>
#include <plat/usb.h>
#include <plat/common.h>
#include <plat/mcspi.h>
#include <plat/clock.h>
#include <plat/omap-pm.h>

#include "mux.h"
#include "sdram-micron-mt46h32m32lf-6.h"
#include "mmc-twl4030.h"
#include "pm.h"
#include "omap3-opp.h"

#define OMAP3_EVM_TS_GPIO	175
#define OMAP3_EVM_EHCI_VBUS	22
#define OMAP3_EVM_EHCI_SELECT	61

#define OMAP3EVM_ETHR_START	0x2c000000
#define OMAP3EVM_ETHR_SIZE	1024
#define OMAP3EVM_ETHR_ID_REV	0x50
#define OMAP3EVM_ETHR_GPIO_IRQ	176
#define OMAP3EVM_SMSC911X_CS	5

static u8 omap3_evm_version;

u8 get_omap3_evm_rev(void)
{
	return omap3_evm_version;
}
EXPORT_SYMBOL(get_omap3_evm_rev);

static void __init omap3_evm_get_revision(void)
{
	void __iomem *ioaddr;
	unsigned int smsc_id;

	/* Ethernet PHY ID is stored at ID_REV register */
	ioaddr = ioremap_nocache(OMAP3EVM_ETHR_START, SZ_1K);
	if (!ioaddr)
		return;
	smsc_id = readl(ioaddr + OMAP3EVM_ETHR_ID_REV) & 0xFFFF0000;
	iounmap(ioaddr);

	switch (smsc_id) {
	/*SMSC9115 chipset*/
	case 0x01150000:
		omap3_evm_version = OMAP3EVM_BOARD_GEN_1;
		break;
	/*SMSC 9220 chipset*/
	case 0x92200000:
	default:
		omap3_evm_version = OMAP3EVM_BOARD_GEN_2;
	}
}

#if defined(CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)
static struct resource omap3evm_smsc911x_resources[] = {
	[0] =	{
		.start	= OMAP3EVM_ETHR_START,
		.end	= (OMAP3EVM_ETHR_START + OMAP3EVM_ETHR_SIZE - 1),
		.flags	= IORESOURCE_MEM,
	},
	[1] =	{
		.start	= OMAP_GPIO_IRQ(OMAP3EVM_ETHR_GPIO_IRQ),
		.end	= OMAP_GPIO_IRQ(OMAP3EVM_ETHR_GPIO_IRQ),
		.flags	= (IORESOURCE_IRQ | IRQF_TRIGGER_LOW),
	},
};

static struct smsc911x_platform_config smsc911x_config = {
	.phy_interface  = PHY_INTERFACE_MODE_MII,
	.irq_polarity   = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type       = SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags          = (SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS),
};

static struct platform_device omap3evm_smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(omap3evm_smsc911x_resources),
	.resource	= &omap3evm_smsc911x_resources[0],
	.dev		= {
		.platform_data = &smsc911x_config,
	},
};

static inline void __init omap3evm_init_smsc911x(void)
{
	int eth_cs;
	struct clk *l3ck;
	unsigned int rate;

	eth_cs = OMAP3EVM_SMSC911X_CS;

	l3ck = clk_get(NULL, "l3_ck");
	if (IS_ERR(l3ck))
		rate = 100000000;
	else
		rate = clk_get_rate(l3ck);

	if (gpio_request(OMAP3EVM_ETHR_GPIO_IRQ, "SMSC911x irq") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smsc911x IRQ\n",
			OMAP3EVM_ETHR_GPIO_IRQ);
		return;
	}

	gpio_direction_input(OMAP3EVM_ETHR_GPIO_IRQ);
	platform_device_register(&omap3evm_smsc911x_device);
}

#else
static inline void __init omap3evm_init_smsc911x(void) { return; }
#endif

static struct regulator_consumer_supply omap3evm_vmmc1_supply = {
	.supply			= "vmmc",
};

static struct regulator_consumer_supply omap3evm_vsim_supply = {
	.supply			= "vmmc_aux",
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data omap3evm_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &omap3evm_vmmc1_supply,
};

/* VSIM for MMC1 pins DAT4..DAT7 (2 mA, plus card == max 50 mA) */
static struct regulator_init_data omap3evm_vsim = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 3000000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &omap3evm_vsim_supply,
};

static struct twl4030_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= 63,
	},
	{}	/* Terminator */
};

static struct gpio_led gpio_leds[] = {
	{
		.name			= "omap3evm::ledb",
		/* normally not visible (board underside) */
		.default_trigger	= "default-on",
		.gpio			= -EINVAL,	/* gets replaced */
		.active_low		= true,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	},
};


static int omap3evm_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	omap_mux_init_gpio(63, OMAP_PIN_INPUT);
	mmc[0].gpio_cd = gpio + 0;
	twl4030_mmc_init(mmc);

	/* link regulators to MMC adapters */
	omap3evm_vmmc1_supply.dev = mmc[0].dev;
	omap3evm_vsim_supply.dev = mmc[0].dev;

	/*
	 * Most GPIOs are for USB OTG.  Some are mostly sent to
	 * the P2 connector; notably LEDA for the LCD backlight.
	 */

	/* TWL4030_GPIO_MAX + 1 == ledB (out, active low LED) */
	gpio_leds[2].gpio = gpio + TWL4030_GPIO_MAX + 1;

	platform_device_register(&leds_gpio);

	return 0;
}

static struct twl4030_gpio_platform_data omap3evm_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.use_leds	= true,
	.setup		= omap3evm_twl_gpio_setup,
};

static struct twl4030_usb_data omap3evm_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static int board_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_A),
	KEY(0, 3, KEY_B),
	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(1, 2, KEY_E),
	KEY(1, 3, KEY_F),
	KEY(2, 0, KEY_ENTER),
	KEY(2, 1, KEY_I),
	KEY(2, 2, KEY_J),
	KEY(2, 3, KEY_K),
	KEY(3, 0, KEY_M),
	KEY(3, 1, KEY_N),
	KEY(3, 2, KEY_O),
	KEY(3, 3, KEY_P)
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data omap3evm_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 4,
	.cols		= 4,
	.rep		= 1,
};

static struct twl4030_madc_platform_data omap3evm_madc_data = {
	.irq_line	= 1,
};

static struct twl4030_codec_audio_data omap3evm_audio_data = {
	.audio_mclk = 26000000,
};

static struct twl4030_codec_data omap3evm_codec_data = {
	.audio_mclk = 26000000,
	.audio = &omap3evm_audio_data,
};

static struct twl4030_platform_data omap3evm_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.keypad		= &omap3evm_kp_data,
	.madc		= &omap3evm_madc_data,
	.usb		= &omap3evm_usb_data,
	.gpio		= &omap3evm_gpio_data,
	.codec		= &omap3evm_codec_data,
};

static struct i2c_board_info __initdata omap3evm_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &omap3evm_twldata,
	},
};

static int __init omap3_evm_i2c_init(void)
{
	/*
	 * REVISIT: These entries can be set in omap3evm_twl_data
	 * after a merge with MFD tree
	 */
	omap3evm_twldata.vmmc1 = &omap3evm_vmmc1;
	omap3evm_twldata.vsim = &omap3evm_vsim;

	omap_register_i2c_bus(1, 2600, omap3evm_i2c_boardinfo,
			ARRAY_SIZE(omap3evm_i2c_boardinfo));
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static struct platform_device omap3_evm_lcd_device = {
	.name		= "omap3evm_lcd",
	.id		= -1,
};

static struct omap_lcd_config omap3_evm_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static void ads7846_dev_init(void)
{
	if (gpio_request(OMAP3_EVM_TS_GPIO, "ADS7846 pendown") < 0)
		printk(KERN_ERR "can't get ads7846 pen down GPIO\n");

	gpio_direction_input(OMAP3_EVM_TS_GPIO);

	omap_set_gpio_debounce(OMAP3_EVM_TS_GPIO, 1);
	omap_set_gpio_debounce_time(OMAP3_EVM_TS_GPIO, 0xa);
}

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(OMAP3_EVM_TS_GPIO);
}

struct ads7846_platform_data ads7846_config = {
	.x_max			= 0x0fff,
	.y_max			= 0x0fff,
	.x_plate_ohms		= 180,
	.pressure_max		= 255,
	.debounce_max		= 10,
	.debounce_tol		= 3,
	.debounce_rep		= 1,
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
	.settle_delay_usecs	= 150,
};

static struct omap2_mcspi_device_config ads7846_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,	/* 0: slave, 1: master */
};

struct spi_board_info omap3evm_spi_board_info[] = {
	[0] = {
		.modalias		= "ads7846",
		.bus_num		= 1,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &ads7846_mcspi_config,
		.irq			= OMAP_GPIO_IRQ(OMAP3_EVM_TS_GPIO),
		.platform_data		= &ads7846_config,
	},
};

static struct omap_board_config_kernel omap3_evm_config[] __initdata = {
	{ OMAP_TAG_LCD,		&omap3_evm_lcd_config },
};

static void __init omap3_evm_init_irq(void)
{
	omap_board_config = omap3_evm_config;
	omap_board_config_size = ARRAY_SIZE(omap3_evm_config);
	omap_init_irq();
	omap2_init_common_hw(mt46h32m32lf6_sdrc_params, NULL, omap3_mpu_rate_table,
	                     omap3_dsp_rate_table, omap3_l3_rate_table);
}

static struct platform_device *omap3_evm_devices[] __initdata = {
	&omap3_evm_lcd_device,
};

static struct ehci_hcd_omap_platform_data ehci_pdata __initconst = {

	.port_mode[0] = EHCI_HCD_OMAP_MODE_UNKNOWN,
	.port_mode[1] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[2] = EHCI_HCD_OMAP_MODE_UNKNOWN,

	.phy_reset  = true,
	/* PHY reset GPIO will be runtime programmed based on EVM version */
	.reset_gpio_port[0]  = -EINVAL,
	.reset_gpio_port[1]  = -EINVAL,
	.reset_gpio_port[2]  = -EINVAL
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#else
#define board_mux	NULL
#endif

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

static void __init omap3_evm_init(void)
{
	omap3_evm_get_revision();
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);

	omap3_evm_i2c_init();

	platform_add_devices(omap3_evm_devices, ARRAY_SIZE(omap3_evm_devices));

	spi_register_board_info(omap3evm_spi_board_info,
				ARRAY_SIZE(omap3evm_spi_board_info));

	omap_serial_init();

	/* OMAP3EVM uses ISP1504 phy and so register nop transceiver */
	usb_nop_xceiv_register();

	if (get_omap3_evm_rev() >= OMAP3EVM_BOARD_GEN_2) {
		/* enable EHCI VBUS using GPIO22 */
		omap_mux_init_gpio(22, OMAP_PIN_INPUT_PULLUP);
		gpio_request(OMAP3_EVM_EHCI_VBUS, "enable EHCI VBUS");
		gpio_direction_output(OMAP3_EVM_EHCI_VBUS, 0);
		gpio_set_value(OMAP3_EVM_EHCI_VBUS, 1);

		/* Select EHCI port on main board */
		omap_mux_init_gpio(61, OMAP_PIN_INPUT_PULLUP);
		gpio_request(OMAP3_EVM_EHCI_SELECT, "select EHCI port");
		gpio_direction_output(OMAP3_EVM_EHCI_SELECT, 0);
		gpio_set_value(OMAP3_EVM_EHCI_SELECT, 0);

		/* setup EHCI phy reset config */
		omap_mux_init_gpio(21, OMAP_PIN_INPUT_PULLUP);
		ehci_pdata.reset_gpio_port[1] = 21;

	} else {
		/* setup EHCI phy reset on MDC */
		omap_mux_init_gpio(135, OMAP_PIN_OUTPUT);
		ehci_pdata.reset_gpio_port[1] = 135;
	}
	usb_musb_init(&musb_board_data);
	usb_ehci_init(&ehci_pdata);
	ads7846_dev_init();
	omap3evm_init_smsc911x();
}

static void __init omap3_evm_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OMAP3EVM, "OMAP3 EVM")
	/* Maintainer: Syed Mohammed Khasim - Texas Instruments */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xfa000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap3_evm_map_io,
	.init_irq	= omap3_evm_init_irq,
	.init_machine	= omap3_evm_init,
	.timer		= &omap_timer,
MACHINE_END