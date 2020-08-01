// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Region - Device Tree support for FPGA programming under Linux
 *
 *  Copyright (C) 2013-2016 Altera Corporation
 *  Copyright (C) 2017 Intel Corporation
 */
#include <linux/fpga/fpga-bridge.h>
#include <linux/fpga/fpga-mgr.h>
#include "fpga-region-core.h"
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

static DEFINE_IDA(fpga_region_core_ida);
static struct class *fpga_region_core_class;

struct fpga_region_core *fpga_region_core_class_find(
	struct device *start, const void *data,
	int (*match)(struct device *, const void *))
{
	struct device *dev;

	dev = class_find_device(fpga_region_core_class, start, data, match);
	if (!dev)
		return NULL;

	return to_fpga_region_core(dev);
}
EXPORT_SYMBOL_GPL(fpga_region_core_class_find);

/**
 * fpga_region_core_get - get an exclusive reference to a fpga region core
 * @region: FPGA Region struct
 *
 * Caller should call fpga_region_core_put() when done with region.
 *
 * Return fpga_region struct if successful.
 * Return -EBUSY if someone already has a reference to the region.
 * Return -ENODEV if @np is not a FPGA Region.
 */
static struct fpga_region_core *fpga_region_core_get(struct fpga_region_core *region)
{
	struct device *dev = &region->dev;

	if (!mutex_trylock(&region->mutex)) {
		dev_dbg(dev, "%s: FPGA Region already in use\n", __func__);
		return ERR_PTR(-EBUSY);
	}

	get_device(dev);
	if (!try_module_get(dev->parent->driver->owner)) {
		put_device(dev);
		mutex_unlock(&region->mutex);
		return ERR_PTR(-ENODEV);
	}

	dev_dbg(dev, "get\n");

	return region;
}

/**
 * fpga_region_core_put - release a reference to a region
 *
 * @region: FPGA region
 */
static void fpga_region_core_put(struct fpga_region_core *region)
{
	struct device *dev = &region->dev;

	dev_dbg(dev, "put\n");

	module_put(dev->parent->driver->owner);
	put_device(dev);
	mutex_unlock(&region->mutex);
}

/**
 * fpga_region_core_program_fpga - program FPGA
 *
 * @region: FPGA region
 *
 * Program an FPGA using fpga image info (region->info).
 * If the region has a get_bridges function, the exclusive reference for the
 * bridges will be held if programming succeeds.  This is intended to prevent
 * reprogramming the region until the caller considers it safe to do so.
 * The caller will need to call fpga_bridges_put() before attempting to
 * reprogram the region.
 *
 * Return 0 for success or negative error code.
 */
int fpga_region_core_program_fpga(struct fpga_region_core *region)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info = region->info;
	int ret;

	region = fpga_region_core_get(region);
	if (IS_ERR(region)) {
		dev_err(dev, "failed to get FPGA region\n");
		return PTR_ERR(region);
	}

	ret = fpga_mgr_lock(region->mgr);
	if (ret) {
		dev_err(dev, "FPGA manager is busy\n");
		goto err_put_region;
	}

	/*
	 * In some cases, we already have a list of bridges in the
	 * fpga region struct.  Or we don't have any bridges.
	 */
	if (region->get_interfaces) {
		ret = region->get_interfaces(region);
		if (ret) {
			dev_err(dev, "failed to get fpga region interfaces\n");
			goto err_unlock_mgr;
		}
	}

	ret = fpga_region_interfaces_disable(&region->interface_list);
	if (ret) {
		dev_err(dev, "failed to disable region interfaces\n");
		goto err_put_br;
	}

	ret = fpga_mgr_load(region->mgr, info);
	if (ret) {
		dev_err(dev, "failed to load FPGA image\n");
		goto err_put_br;
	}

	ret = fpga_region_interfaces_enable(&region->interface_list);
	if (ret) {
		dev_err(dev, "failed to enable region interfaces\n");
		goto err_put_br;
	}

	fpga_mgr_unlock(region->mgr);
	fpga_region_core_put(region);

	return 0;

err_put_br:
	if (region->get_interfaces)
		fpga_region_interfaces_put(&region->interface_list);
err_unlock_mgr:
	fpga_mgr_unlock(region->mgr);
err_put_region:
	fpga_region_core_put(region);

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_region_core_program_fpga);

static ssize_t compat_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct fpga_region_core *region = to_fpga_region_core(dev);

	if (!region->compat_id)
		return -ENOENT;

	return sprintf(buf, "%016llx%016llx\n",
		       (unsigned long long)region->compat_id->id_h,
		       (unsigned long long)region->compat_id->id_l);
}

static DEVICE_ATTR_RO(compat_id);

static struct attribute *fpga_region_core_attrs[] = {
	&dev_attr_compat_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fpga_region_core);

/**
 * fpga_region_core_create - alloc and init a struct fpga_region_core
 * @dev: device parent
 * @mgr: manager that programs this region
 * @get_interfaces: optional function to get fpga-region-interfaces to a list
 *
 * The caller of this function is responsible for freeing the resulting region
 * struct with fpga_region_core_free().  Using devm_fpga_region_core_create() instead is
 * recommended.
 *
 * Return: struct fpga_region_core or NULL
 */
struct fpga_region_core
*fpga_region_core_create(struct device *dev,
		    struct fpga_manager *mgr,
		    int (*get_interfaces)(struct fpga_region_core *))
{
	struct fpga_region_core *region;
	int id, ret = 0;

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return NULL;

	id = ida_simple_get(&fpga_region_core_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		goto err_free;

	region->mgr = mgr;
	region->get_interfaces = get_interfaces;
	mutex_init(&region->mutex);
	INIT_LIST_HEAD(&region->interface_list);

	device_initialize(&region->dev);
	region->dev.class = fpga_region_core_class;
	region->dev.parent = dev;
	region->dev.of_node = dev->of_node;
	region->dev.id = id;

	ret = dev_set_name(&region->dev, "region%d", id);
	if (ret)
		goto err_remove;

	return region;

err_remove:
	ida_simple_remove(&fpga_region_core_ida, id);
err_free:
	kfree(region);

	return NULL;
}
EXPORT_SYMBOL_GPL(fpga_region_core_create);

/**
 * fpga_region_core_free - free a FPGA region created by fpga_region_core_create()
 * @region: FPGA region
 */
void fpga_region_core_free(struct fpga_region_core *region)
{
	ida_simple_remove(&fpga_region_core_ida, region->dev.id);
	kfree(region);
}
EXPORT_SYMBOL_GPL(fpga_region_core_free);

static void devm_fpga_region_core_release(struct device *dev, void *res)
{
	struct fpga_region_core *region = *(struct fpga_region_core **)res;

	fpga_region_core_free(region);
}

/**
 * devm_fpga_region_core_create - create and initialize a managed FPGA region core struct
 * @dev: device parent
 * @mgr: manager that programs this region
 * @get_interfaces: optional function to get fpga-region-interfaces to a list
 *
 * This function is intended for use in a FPGA region driver's probe function.
 * After the region driver creates the region struct with
 * devm_fpga_region_core_create(), it should register it with fpga_region_core_register().
 * The region driver's remove function should call fpga_region_core_unregister().
 * The region struct allocated with this function will be freed automatically on
 * driver detach.  This includes the case of a probe function returning error
 * before calling fpga_region_core_register(), the struct will still get cleaned up.
 *
 * Return: struct fpga_region_core or NULL
 */
struct fpga_region_core
*devm_fpga_region_core_create(struct device *dev,
			 struct fpga_manager *mgr,
			 int (*get_interfaces)(struct fpga_region_core *))
{
	struct fpga_region_core **ptr, *region;

	ptr = devres_alloc(devm_fpga_region_core_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	region = fpga_region_core_create(dev, mgr, get_interfaces);
	if (!region) {
		devres_free(ptr);
	} else {
		*ptr = region;
		devres_add(dev, ptr);
	}

	return region;
}
EXPORT_SYMBOL_GPL(devm_fpga_region_core_create);

/**
 * fpga_region_core_register - register a FPGA region core
 * @region: FPGA region core
 *
 * Return: 0 or -errno
 */
int fpga_region_core_register(struct fpga_region_core *region)
{
	return device_add(&region->dev);
}
EXPORT_SYMBOL_GPL(fpga_region_core_register);

/**
 * fpga_region_core_unregister - unregister a FPGA region core
 * @region: FPGA region
 *
 * This function is intended for use in a FPGA region driver's remove function.
 */
void fpga_region_core_unregister(struct fpga_region_core *region)
{
	device_unregister(&region->dev);
}
EXPORT_SYMBOL_GPL(fpga_region_core_unregister);

static void fpga_region_core_dev_release(struct device *dev)
{
}

/**
 * fpga_region_core_init - init function for fpga_region_core class
 * Creates the fpga_region_core class and registers a reconfig notifier.
 */
static int __init fpga_region_core_init(void)
{
	fpga_region_core_class = class_create(THIS_MODULE, "fpga_region_core");
	if (IS_ERR(fpga_region_core_class))
		return PTR_ERR(fpga_region_core_class);

	fpga_region_core_class->dev_groups  = fpga_region_core_groups;
	fpga_region_core_class->dev_release = fpga_region_core_dev_release;

	return 0;
}

static void __exit fpga_region_core_exit(void)
{
	class_destroy(fpga_region_core_class);
	ida_destroy(&fpga_region_core_ida);
}

subsys_initcall(fpga_region_core_init);
module_exit(fpga_region_core_exit);

MODULE_DESCRIPTION("FPGA Region Core");
MODULE_AUTHOR("Alan Tull <atull@kernel.org>");
MODULE_LICENSE("GPL v2");
