/**
 * NAND flash driver for Broadcom Secondary Memory Interface
 *
 * Written by Luke Wren <luke@raspberrypi.org>
 * Copyright (c) 2015, Raspberry Pi (Trading) Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>

#include <linux/broadcom/bcm2835_smi.h>

#define DEVICE_NAME "bcm2835-smi-nand"
#define DRIVER_NAME "smi-nand-bcm2835"

struct bcm2835_smi_nand_host {
	struct bcm2835_smi_instance *smi_inst;
	struct nand_chip nand_chip;
	struct mtd_info mtd;
	struct device *dev;
};

/****************************************************************************
*
*   NAND functionality implementation
*
****************************************************************************/

#define SMI_NAND_CLE_PIN 0x01
#define SMI_NAND_ALE_PIN 0x02

static inline void bcm2835_smi_nand_cmd_ctrl(struct mtd_info *mtd, int cmd,
					     unsigned int ctrl)
{
	uint32_t cmd32 = cmd;
	uint32_t addr = ~(SMI_NAND_CLE_PIN | SMI_NAND_ALE_PIN);
	struct bcm2835_smi_nand_host *host = dev_get_drvdata(mtd->dev.parent);
	struct bcm2835_smi_instance *inst = host->smi_inst;

	if (ctrl & NAND_CLE)
		addr |= SMI_NAND_CLE_PIN;
	if (ctrl & NAND_ALE)
		addr |= SMI_NAND_ALE_PIN;
	/* Lower ALL the CS pins! */
	if (ctrl & NAND_NCE)
		addr &= (SMI_NAND_CLE_PIN | SMI_NAND_ALE_PIN);

	bcm2835_smi_set_address(inst, addr);

	if (cmd != NAND_CMD_NONE)
		bcm2835_smi_write_buf(inst, &cmd32, 1);
}

static inline uint8_t bcm2835_smi_nand_read_byte(struct mtd_info *mtd)
{
	uint8_t byte;
	struct bcm2835_smi_nand_host *host = dev_get_drvdata(mtd->dev.parent);
	struct bcm2835_smi_instance *inst = host->smi_inst;

	bcm2835_smi_read_buf(inst, &byte, 1);
	return byte;
}

static inline void bcm2835_smi_nand_write_byte(struct mtd_info *mtd,
					       uint8_t byte)
{
	struct bcm2835_smi_nand_host *host = dev_get_drvdata(mtd->dev.parent);
	struct bcm2835_smi_instance *inst = host->smi_inst;

	bcm2835_smi_write_buf(inst, &byte, 1);
}

static inline void bcm2835_smi_nand_write_buf(struct mtd_info *mtd,
					      const uint8_t *buf, int len)
{
	struct bcm2835_smi_nand_host *host = dev_get_drvdata(mtd->dev.parent);
	struct bcm2835_smi_instance *inst = host->smi_inst;

	bcm2835_smi_write_buf(inst, buf, len);
}

static inline void bcm2835_smi_nand_read_buf(struct mtd_info *mtd,
					     uint8_t *buf, int len)
{
	struct bcm2835_smi_nand_host *host = dev_get_drvdata(mtd->dev.parent);
	struct bcm2835_smi_instance *inst = host->smi_inst;

	bcm2835_smi_read_buf(inst, buf, len);
}

/****************************************************************************
*
*   Probe and remove functions
*
***************************************************************************/

static int bcm2835_smi_nand_probe(struct platform_device *pdev)
{
	struct bcm2835_smi_nand_host *host;
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node, *smi_node;
	struct smi_settings *smi_settings;
	struct bcm2835_smi_instance *smi_inst;

	if (!node) {
		dev_err(dev, "No device tree node supplied!");
		return -EINVAL;
	}

	smi_node = of_parse_phandle(node, "smi_handle", 0);

	/* Request use of SMI peripheral: */
	smi_inst = bcm2835_smi_get(smi_node);

	if (!smi_inst) {
		dev_err(dev, "Could not register with SMI.");
		return -EPROBE_DEFER;
	}

	/* Set SMI timing and bus width */

	smi_settings = bcm2835_smi_get_settings_from_regs(smi_inst);

	smi_settings->data_width = SMI_WIDTH_8BIT;
	smi_settings->read_setup_time = 2;
	smi_settings->read_hold_time = 1;
	smi_settings->read_pace_time = 1;
	smi_settings->read_strobe_time = 3;

	smi_settings->write_setup_time = 2;
	smi_settings->write_hold_time = 1;
	smi_settings->write_pace_time = 1;
	smi_settings->write_strobe_time = 3;

	bcm2835_smi_set_regs_from_settings(smi_inst);

	host = devm_kzalloc(dev, sizeof(struct bcm2835_smi_nand_host),
		GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->dev = dev;
	host->smi_inst = smi_inst;

	platform_set_drvdata(pdev, host);

	/* Link the structures together */

	this = &host->nand_chip;
	mtd = &host->mtd;
	mtd->priv = this;
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = dev;
	mtd->name = DRIVER_NAME;

	/* 20 us command delay time... */
	this->chip_delay = 20;

	this->priv = host;
	this->cmd_ctrl = bcm2835_smi_nand_cmd_ctrl;
	this->read_byte = bcm2835_smi_nand_read_byte;
	this->write_byte = bcm2835_smi_nand_write_byte;
	this->write_buf = bcm2835_smi_nand_write_buf;
	this->read_buf = bcm2835_smi_nand_read_buf;

	this->ecc.mode = NAND_ECC_SOFT;

	/* Should never be accessed directly: */

	this->IO_ADDR_R = (void *)0xdeadbeef;
	this->IO_ADDR_W = (void *)0xdeadbeef;

	/* Scan to find the device and get the page size */

	if (nand_scan(mtd, 1))
		return -ENXIO;

	nand_release(mtd);
	return -EINVAL;
}

static int bcm2835_smi_nand_remove(struct platform_device *pdev)
{
	struct bcm2835_smi_nand_host *host = platform_get_drvdata(pdev);

	nand_release(&host->mtd);

	return 0;
}

/****************************************************************************
*
*   Register the driver with device tree
*
***************************************************************************/

static const struct of_device_id bcm2835_smi_nand_of_match[] = {
	{.compatible = "brcm,bcm2835-smi-nand",},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, bcm2835_smi_nand_of_match);

static struct platform_driver bcm2835_smi_nand_driver = {
	.probe = bcm2835_smi_nand_probe,
	.remove = bcm2835_smi_nand_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = bcm2835_smi_nand_of_match,
	},
};

module_platform_driver(bcm2835_smi_nand_driver);

MODULE_ALIAS("platform:smi-nand-bcm2835");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION
	("Driver for NAND chips using Broadcom Secondary Memory Interface");
MODULE_AUTHOR("Luke Wren <luke@raspberrypi.org>");
