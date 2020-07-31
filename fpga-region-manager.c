// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Region Manager - Device Tree support for FPGA programming under Linux
 *
 *  Copyright (C) 2013-2016 Altera Corporation
 *  Copyright (C) 2017 Intel Corporation
 *  Copyright (C) 2020 Ichiro Kawazome
 */
#include <linux/fpga/fpga-mgr.h>
#include <linux/fpga/fpga-region.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "fpga-region-interface.h"

static const struct of_device_id fpga_region_manager_of_match[] = {
	{ .compatible = "ikwzm,fpga-region-manager", },
	{},
};
MODULE_DEVICE_TABLE(of, fpga_region_manager_of_match);

/**
 * fpga_region_manager_get_mgr - get reference for FPGA manager
 * @np: device node of FPGA region
 *
 * Get FPGA Manager from "fpga-mgr" property or from ancestor region.
 *
 * Caller should call fpga_mgr_put() when done with manager.
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
static struct fpga_manager *fpga_region_manager_get_mgr(struct device_node *np)
{
	struct device_node  *mgr_node;
	struct fpga_manager *mgr;

	of_node_get(np);
	while (np) {
		if (of_device_is_compatible(np, "ikwzm,fpga-region-manager")) {
			mgr_node = of_parse_phandle(np, "fpga-mgr", 0);
			if (mgr_node) {
				mgr = of_fpga_mgr_get(mgr_node);
				of_node_put(mgr_node);
				of_node_put(np);
				return mgr;
			}
		}
		np = of_get_next_parent(np);
	}
	of_node_put(np);

	return ERR_PTR(-EINVAL);
}

/**
 * fpga_region_manager_get_interfaces - create a list of bridges
 * @region: FPGA region
 *
 * Create a list of bridges including the parent bridge and the bridges
 * specified by "fpga-bridges" property.  Note that the
 * fpga_bridges_enable/disable/put functions are all fine with an empty list
 * if that happens.
 *
 * Caller should call fpga_bridges_put(&region->bridge_list) when
 * done with the bridges.
 *
 * Return 0 for success (even if there are no bridges specified)
 * or -EBUSY if any of the bridges are in use.
 */
static int fpga_region_manager_get_interfaces(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct device_node *region_np = dev->of_node;
	struct fpga_image_info *info = region->info;
	struct device_node *br, *np, *parent_br = NULL;
	int i, ret;

	/* If parent is a bridge, add to list */
	ret = of_fpga_region_interface_get_to_list(region_np->parent, info, &region->bridge_list);

	/* -EBUSY means parent is a bridge that is under use. Give up. */
	if (ret == -EBUSY)
		return ret;

	/* Zero return code means parent was a bridge and was added to list. */
	if (!ret)
		parent_br = region_np->parent;

	/* If overlay has a list of bridges, use it. */
	br = of_parse_phandle(info->overlay, "fpga-bridges", 0);
	if (br) {
		of_node_put(br);
		np = info->overlay;
	} else {
		np = region_np;
	}

	for (i = 0; ; i++) {
		br = of_parse_phandle(np, "fpga-bridges", i);
		if (!br)
			break;

		/* If parent bridge is in list, skip it. */
		if (br == parent_br) {
			of_node_put(br);
			continue;
		}

		/* If node is a bridge, get it and add to list */
		ret = of_fpga_region_interface_get_to_list(br, info, &region->bridge_list);
		of_node_put(br);

		/* If any of the bridges are in use, give up */
		if (ret == -EBUSY) {
			fpga_region_interfaces_put(&region->bridge_list);
			return -EBUSY;
		}
	}

        ret = fpga_region_interfaces_of_setup(&region->bridge_list, region_np);
        if (ret) {
		fpga_region_interfaces_put(&region->bridge_list);
		return -EBUSY;
	}

        ret = fpga_region_interfaces_of_setup(&region->bridge_list, info->overlay);
        if (ret) {
		fpga_region_interfaces_put(&region->bridge_list);
		return -EBUSY;
	}

	return 0;
}

static int fpga_region_manager_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct fpga_region *region;
	struct fpga_manager *mgr;
	int ret;

	/* Find the FPGA mgr specified by region or parent region. */
	mgr = fpga_region_manager_get_mgr(np);
	if (IS_ERR(mgr))
		return -EPROBE_DEFER;

	region = devm_fpga_region_create(dev, mgr, fpga_region_manager_get_interfaces);
	if (!region) {
		ret = -ENOMEM;
		goto eprobe_mgr_put;
	}

	ret = fpga_region_register(region);
	if (ret)
		goto eprobe_mgr_put;

	of_platform_populate(np, fpga_region_manager_of_match, NULL, &region->dev);
	platform_set_drvdata(pdev, region);

	dev_info(dev, "FPGA Region Manager probed\n");

	return 0;

eprobe_mgr_put:
	fpga_mgr_put(mgr);
	return ret;
}

static int fpga_region_manager_remove(struct platform_device *pdev)
{
	struct fpga_region *region = platform_get_drvdata(pdev);
	struct fpga_manager *mgr = region->mgr;

	fpga_region_unregister(region);
	fpga_mgr_put(mgr);

	return 0;
}

static struct platform_driver fpga_region_manager_platform_driver = {
	.probe  = fpga_region_manager_probe,
	.remove = fpga_region_manager_remove,
	.driver = {
		.name	= "fpga-region-manager",
		.of_match_table = of_match_ptr(fpga_region_manager_of_match),
	},
};

/**
 * fpga_region_init - init function for fpga_region class
 * Creates the fpga_region class and registers a reconfig notifier.
 */
static int __init fpga_region_manager_init(void)
{
	int ret;

	ret = platform_driver_register(&fpga_region_manager_platform_driver);
	if (ret)
		goto err_plat;

	return 0;

err_plat:
	return ret;
}

static void __exit fpga_region_manager_exit(void)
{
	platform_driver_unregister(&fpga_region_manager_platform_driver);
}

subsys_initcall(fpga_region_manager_init);
module_exit(fpga_region_manager_exit);

MODULE_DESCRIPTION("FPGA Region Manager");
MODULE_AUTHOR("Ichiro Kawazome <ichiro_k@ca2.so-net.ne.jp>");
MODULE_LICENSE("GPL v2");
