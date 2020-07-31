// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Bridge Framework Driver
 *
 *  Copyright (C) 2013-2016 Altera Corporation, All Rights Reserved.
 *  Copyright (C) 2017 Intel Corporation
 *  Copyright (C) 2020 Ichiro Kawazome
 */
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/fpga/fpga-bridge.h>
#include "fpga-region-interface.h"

static DEFINE_IDA(fpga_region_interface_ida);
static struct class *fpga_region_interface_class;

/* Lock for adding/removing bridges to linked lists*/
static spinlock_t fpga_region_interface_list_lock;

/**
 * fpga_region_interface_enable - Enable transactions on the fpga region interface
 *
 * @interface: FPGA region interface
 *
 * Return: 0 for success, error code otherwise.
 */
int fpga_region_interface_enable(struct fpga_region_interface* interface)
{
	dev_dbg(&interface->dev, "enable\n");

	if (interface->dev.class != fpga_region_interface_class) 
		return fpga_bridge_enable((struct fpga_bridge*)interface);

	if (interface->ops && interface->ops->enable_set)
		return interface->ops->enable_set(interface, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_region_interface_enable);

/**
 * fpga_region_interface_disable - Disable transactions on the fpga region interface
 *
 * @interface: FPGA region interface
 *
 * Return: 0 for success, error code otherwise.
 */
int fpga_region_interface_disable(struct fpga_region_interface* interface)
{
	dev_dbg(&interface->dev, "disable\n");

	if (interface->dev.class != fpga_region_interface_class) 
		return fpga_bridge_disable((struct fpga_bridge*)interface);

	if (interface->ops && interface->ops->enable_set)
		return interface->ops->enable_set(interface, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_region_interface_disable);

/**
 * fpga_region_interface_of_setup - Setup the fpga region interface by device tree node
 *
 * @interface: FPGA region interface
 * @np: node pointer of device tree
 *
 * Return: 0 for success, error code otherwise.
 */
int fpga_region_interface_of_setup(struct fpga_region_interface* interface, struct device_node* np)
{
  
	dev_dbg(&interface->dev, "setup\n");

	if (interface->dev.class != fpga_region_interface_class)
		return 0;

	if (interface->ops && interface->ops->of_setup) {
		struct device_node* node = of_find_node_by_name(of_node_get(np), interface->name);
		if (node) {
			int retval = interface->ops->of_setup(interface, node);
			of_node_put(node);
			return retval;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_region_interface_of_setup);

static struct fpga_region_interface *__fpga_region_interface_get(
	struct device *dev,
	struct fpga_image_info *info)
{
	struct fpga_region_interface* interface;
	int ret = -ENODEV;

	interface = to_fpga_region_interface(dev);

	interface->info = info;

	if (!mutex_trylock(&interface->mutex)) {
		ret = -EBUSY;
		goto err_dev;
	}

	if (!try_module_get(dev->parent->driver->owner))
		goto err_ll_mod;

	dev_dbg(&interface->dev, "get\n");

	return interface;

err_ll_mod:
	mutex_unlock(&interface->mutex);
err_dev:
	put_device(dev);
	return ERR_PTR(ret);
}

/**
 * of_fpga_region_interface_get - get an exclusive reference to a fpga region interface
 *
 * @np: node pointer of a FPGA region_interface
 * @info: fpga image specific information
 *
 * Return fpga_region_interface struct if successful.
 * Return -EBUSY if someone already has a reference to the region_interface.
 * Return -ENODEV if @np is not a FPGA Region_Interface.
 */
struct fpga_region_interface *of_fpga_region_interface_get(
	struct device_node *np,
	struct fpga_image_info *info)
{
	struct device *dev;

	dev = class_find_device_by_of_node(fpga_region_interface_class, np);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return __fpga_region_interface_get(dev, info);
}
EXPORT_SYMBOL_GPL(of_fpga_region_interface_get);

static int fpga_region_interface_dev_match(struct device *dev, const void *data)
{
	return dev->parent == data;
}

/**
 * fpga_region_interface_get - get an exclusive reference to a fpga region interface
 * @dev:	parent device that fpga region_interface was registered with
 * @info:	fpga manager info
 *
 * Given a device, get an exclusive reference to a fpga region interface.
 *
 * Return: fpga region_interface struct or IS_ERR() condition containing error code.
 */
struct fpga_region_interface *fpga_region_interface_get(
	struct device *dev,
	struct fpga_image_info *info)
{
	struct device *interface_dev;

	interface_dev = class_find_device(fpga_region_interface_class, NULL, dev,
					  fpga_region_interface_dev_match);
	if (!interface_dev)
		return ERR_PTR(-ENODEV);

	return __fpga_region_interface_get(interface_dev, info);
}
EXPORT_SYMBOL_GPL(fpga_region_interface_get);

/**
 * fpga_region_interface_put - release a reference to a fpga region interface
 *
 * @interface: FPGA region_interface
 */
void fpga_region_interface_put(struct fpga_region_interface* interface)
{
	dev_dbg(&interface->dev, "put\n");

	interface->info = NULL;
	module_put(interface->dev.parent->driver->owner);
	mutex_unlock(&interface->mutex);
	put_device(&interface->dev);
}
EXPORT_SYMBOL_GPL(fpga_region_interface_put);

/**
 * fpga_region_interfaces_enable - enable fpga region interfaces in a list
 * @interface_list: list of fpga region interfaces
 *
 * Enable each interface in the list.  If list is empty, do nothing.
 *
 * Return 0 for success or empty interface list; return error code otherwise.
 */
int fpga_region_interfaces_enable(struct list_head* interface_list)
{
	struct fpga_region_interface* interface;
	int ret;

	list_for_each_entry(interface, interface_list, node) {
		ret = fpga_region_interface_enable(interface);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_region_interfaces_enable);

/**
 * fpga_region_interfaces_disable - disable fpga region interfaces in a list
 *
 * @interface_list: list of fpga region interfaces
 *
 * Disable each interface in the list.  If list is empty, do nothing.
 *
 * Return 0 for success or empty interface list; return error code otherwise.
 */
int fpga_region_interfaces_disable(struct list_head* interface_list)
{
	struct fpga_region_interface* interface;
	int ret;

	list_for_each_entry_reverse(interface, interface_list, node) {
		ret = fpga_region_interface_disable(interface);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_region_interfaces_disable);

/**
 * fpga_region_interfaces_of_setup - setup fpga region interfaces in a list
 *
 * @interface_list: list of fpga region interfaces
 * @np: node pointer of device tree
 *
 * Setup each interface in the list.  If list is empty, do nothing.
 *
 * Return 0 for success or empty interface list; return error code otherwise.
 */
int fpga_region_interfaces_of_setup(struct list_head* interface_list, struct device_node* np)
{
	struct fpga_region_interface* interface;
	int ret;

	list_for_each_entry(interface, interface_list, node) {
		ret = fpga_region_interface_of_setup(interface, np);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_region_interfaces_of_setup);

/**
 * fpga_region_interfaces_put - put fpga region interfaces
 *
 * @interface_list: list of fpga region interfaces
 *
 * For each interface in the list, put the interface and remove it from the list.
 * If list is empty, do nothing.
 */
void fpga_region_interfaces_put(struct list_head* interface_list)
{
	struct fpga_region_interface *interface, *next;
	unsigned long flags;

	list_for_each_entry_safe(interface, next, interface_list, node) {
		if (interface->dev.class == fpga_region_interface_class)
			fpga_region_interface_put(interface);
		else
			fpga_bridge_put((struct fpga_bridge*)interface);
		spin_lock_irqsave(&fpga_region_interface_list_lock, flags);
		list_del(&interface->node);
		spin_unlock_irqrestore(&fpga_region_interface_list_lock, flags);
	}
}
EXPORT_SYMBOL_GPL(fpga_region_interfaces_put);

/**
 * of_fpga_region_interface_get_to_list - get a fpga region interface, add it to a list
 *
 * @np: node pointer of a FPGA region_interface
 * @info: fpga image specific information
 * @interface_list: list of FPGA region_interfaces
 *
 * Get an exclusive reference to the fpga region interface and and it to the list.
 *
 * Return 0 for success, error code from of_fpga_region_interface_get() othewise.
 */
int of_fpga_region_interface_get_to_list(
	struct device_node *np,
	struct fpga_image_info *info,
	struct list_head *interface_list)
{
	struct fpga_region_interface* interface;
	struct fpga_bridge*           bridge;
	unsigned long                 flags;

	interface = of_fpga_region_interface_get(np, info);
	if (!IS_ERR(interface)) {
		spin_lock_irqsave(&fpga_region_interface_list_lock, flags);
		list_add(&interface->node, interface_list);
		spin_unlock_irqrestore(&fpga_region_interface_list_lock, flags);
		return 0;
        }
	bridge = of_fpga_bridge_get(np, info);
	if (!IS_ERR(bridge)) {
		spin_lock_irqsave(&fpga_region_interface_list_lock, flags);
		list_add(&bridge->node, interface_list);
		spin_unlock_irqrestore(&fpga_region_interface_list_lock, flags);
		return 0;
        }
	return PTR_ERR(bridge);
}
EXPORT_SYMBOL_GPL(of_fpga_region_interface_get_to_list);

/**
 * fpga_region_interface_get_to_list - given device, get a fpga region interface, add it to a list
 *
 * @dev: FPGA region_interface device
 * @info: fpga image specific information
 * @interface_list: list of FPGA region_interfaces
 *
 * Get an exclusive reference to the region_interface and and it to the list.
 *
 * Return 0 for success, error code from fpga_region_interface_get() othewise.
 */
int fpga_region_interface_get_to_list(
	struct device *dev,
	struct fpga_image_info *info,
	struct list_head *interface_list)
{
	struct fpga_region_interface* interface;
	struct fpga_bridge*           bridge;
	unsigned long                 flags;

	interface = fpga_region_interface_get(dev, info);
	if (!IS_ERR(interface)) {
		spin_lock_irqsave(&fpga_region_interface_list_lock, flags);
		list_add(&interface->node, interface_list);
		spin_unlock_irqrestore(&fpga_region_interface_list_lock, flags);
		return 0;
        }
	bridge = fpga_bridge_get(dev, info);
	if (!IS_ERR(bridge)) {
		spin_lock_irqsave(&fpga_region_interface_list_lock, flags);
		list_add(&bridge->node, interface_list);
		spin_unlock_irqrestore(&fpga_region_interface_list_lock, flags);
		return 0;
        }
	return PTR_ERR(bridge);
}
EXPORT_SYMBOL_GPL(fpga_region_interface_get_to_list);

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct fpga_region_interface* interface = to_fpga_region_interface(dev);

	return sprintf(buf, "%s\n", interface->name);
}

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct fpga_region_interface* interface = to_fpga_region_interface(dev);
	int enable = 1;

	if (interface->ops && interface->ops->enable_show)
		enable = interface->ops->enable_show(interface);

	return sprintf(buf, "%s\n", enable ? "enabled" : "disabled");
}

static DEVICE_ATTR_RO(name);
static DEVICE_ATTR_RO(state);

static struct attribute *fpga_region_interface_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fpga_region_interface);

/**
 * fpga_region_interface_create - create and initialize a struct fpga_region_interface
 * @dev:	FPGA region_interface device from pdev
 * @name:	FPGA region_interface name
 * @ops:	pointer to structure of fpga region_interface ops
 * @priv:	FPGA region_interface private data
 *
 * The caller of this function is responsible for freeing the region_interface with
 * fpga_region_interface_free().  Using devm_fpga_region_interface_create() instead is recommended.
 *
 * Return: struct fpga_region_interface or NULL
 */
struct fpga_region_interface *fpga_region_interface_create(
	struct device *dev,
	const char *name,
	const struct fpga_region_interface_ops *ops,
	void *priv)
{
	struct fpga_region_interface* interface;
	int id, ret = 0;

	if (!name || !strlen(name)) {
		dev_err(dev, "Attempt to register with no name!\n");
		return NULL;
	}

	interface = kzalloc(sizeof(*interface), GFP_KERNEL);
	if (!interface)
		return NULL;

	id = ida_simple_get(&fpga_region_interface_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto error_kfree;
	}

	mutex_init(&interface->mutex);
	INIT_LIST_HEAD(&interface->node);

	interface->name = name;
	interface->ops  = ops;
	interface->priv = priv;

	device_initialize(&interface->dev);
	interface->dev.groups  = ops->groups;
	interface->dev.class   = fpga_region_interface_class;
	interface->dev.parent  = dev;
	interface->dev.of_node = dev->of_node;
	interface->dev.id      = id;

	if (name)
		ret = dev_set_name(&interface->dev, "%s", name);
	else
		ret = dev_set_name(&interface->dev, "br%d", id);
	if (ret)
		goto error_device;

	return interface;

error_device:
	ida_simple_remove(&fpga_region_interface_ida, id);
error_kfree:
	kfree(interface);

	return NULL;
}
EXPORT_SYMBOL_GPL(fpga_region_interface_create);

/**
 * fpga_region_interface_free - free a fpga region_interface created by fpga_region_interface_create()
 * @region_interface:	FPGA region_interface struct
 */
void fpga_region_interface_free(struct fpga_region_interface *interface)
{
	ida_simple_remove(&fpga_region_interface_ida, interface->dev.id);
	kfree(interface);
}
EXPORT_SYMBOL_GPL(fpga_region_interface_free);

static void devm_fpga_region_interface_release(struct device *dev, void *res)
{
	struct fpga_region_interface* interface = *(struct fpga_region_interface **)res;

	fpga_region_interface_free(interface);
}

/**
 * devm_fpga_region_interface_create - create and init a managed struct fpga_region_interface
 * @dev:	FPGA region_interface device from pdev
 * @name:	FPGA region_interface name
 * @ops:	pointer to structure of fpga region_interface ops
 * @priv:	FPGA region_interface private data
 *
 * This function is intended for use in a FPGA region_interface driver's probe function.
 * After the region_interface driver creates the struct with devm_fpga_region_interface_create(), it
 * should register the region_interface with fpga_region_interface_register().  The region_interface driver's
 * remove function should call fpga_region_interface_unregister().  The region_interface struct
 * allocated with this function will be freed automatically on driver detach.
 * This includes the case of a probe function returning error before calling
 * fpga_region_interface_register(), the struct will still get cleaned up.
 *
 *  Return: struct fpga_region_interface or NULL
 */
struct fpga_region_interface
*devm_fpga_region_interface_create(
	struct device *dev,
	const char *name,
	const struct fpga_region_interface_ops *ops,
	void *priv)
{
	struct fpga_region_interface **ptr, *interface;

	ptr = devres_alloc(devm_fpga_region_interface_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	interface = fpga_region_interface_create(dev, name, ops, priv);
	if (!interface) {
		devres_free(ptr);
	} else {
		*ptr = interface;
		devres_add(dev, ptr);
	}

	return interface;
}
EXPORT_SYMBOL_GPL(devm_fpga_region_interface_create);

/**
 * fpga_region_interface_register - register a FPGA region_interface
 *
 * @region_interface: FPGA region_interface struct
 *
 * Return: 0 for success, error code otherwise.
 */
int fpga_region_interface_register(struct fpga_region_interface *interface)
{
	struct device *dev = &interface->dev;
	int ret;

	ret = device_add(dev);
	if (ret)
		return ret;

	of_platform_populate(dev->of_node, NULL, NULL, dev);

	dev_info(dev->parent, "fpga region interface [%s] registered\n", interface->name);

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_region_interface_register);

/**
 * fpga_region_interface_unregister - unregister a FPGA region_interface
 *
 * @interface: FPGA region_interface struct
 *
 * This function is intended for use in a FPGA region_interface driver's remove function.
 */
void fpga_region_interface_unregister(struct fpga_region_interface* interface)
{
	/*
	 * If the low level driver provides a method for putting region_interface into
	 * a desired state upon unregister, do it.
	 */
	if (interface->ops && interface->ops->remove)
		interface->ops->remove(interface);

	device_unregister(&interface->dev);
}
EXPORT_SYMBOL_GPL(fpga_region_interface_unregister);

static void fpga_region_interface_dev_release(struct device *dev)
{
}

static int __init fpga_region_interface_module_init(void)
{
	spin_lock_init(&fpga_region_interface_list_lock);

	fpga_region_interface_class = class_create(THIS_MODULE, "fpga_region_interface");
	if (IS_ERR(fpga_region_interface_class))
		return PTR_ERR(fpga_region_interface_class);

	fpga_region_interface_class->dev_groups  = fpga_region_interface_groups;
	fpga_region_interface_class->dev_release = fpga_region_interface_dev_release;

	return 0;
}

static void __exit fpga_region_interface_module_exit(void)
{
	class_destroy(fpga_region_interface_class);
	ida_destroy(&fpga_region_interface_ida);
}

MODULE_DESCRIPTION("FPGA Region Interface Driver");
MODULE_AUTHOR("Alan Tull <atull@kernel.org>");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_region_interface_module_init);
module_exit(fpga_region_interface_module_exit);
