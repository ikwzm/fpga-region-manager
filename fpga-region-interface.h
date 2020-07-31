/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_FPGA_REGION_INTERFACE_H
#define _LINUX_FPGA_REGION_INTERFACE_H

#include <linux/device.h>
#include <linux/fpga/fpga-mgr.h>

struct fpga_region_interface;

/**
 * struct fpga_region_interface_ops - ops for low level FPGA regsion interface drivers
 * @enable_show: returns the FPGA region interface's status
 * @enable_set: set a FPGA region interface as enabled or disabled
 * @of_setup: setup a FPGA region interface by device tree node
 * @fpga_region_interface_remove: set FPGA into a specific state during driver remove
 * @groups: optional attribute groups.
 */
struct fpga_region_interface_ops {
	int (*enable_show)(struct fpga_region_interface *bridge);
	int (*enable_set)(struct fpga_region_interface *bridge, bool enable);
	int (*of_setup)(struct fpga_region_interface *bridge, struct device_node* np);
	void (*remove)(struct fpga_region_interface *bridge);
	const struct attribute_group **groups;
};

/**
 * struct fpga_region_interface - FPGA region interface structure
 * @name: name of low level FPGA region interface
 * @dev: FPGA bridge device
 * @mutex: enforces exclusive reference to FPGA region interface
 * @ops: pointer to struct of FPGA region interface ops
 * @info: fpga image specific information
 * @node: FPGA region interface list node
 * @priv: low level driver private date
 */
struct fpga_region_interface {
	const char *name;
	struct device dev;
	struct mutex mutex; /* for exclusive reference to bridge */
	const struct fpga_region_interface_ops *ops;
	struct fpga_image_info *info;
	struct list_head node;
	void *priv;
};

#define to_fpga_region_interface(d) container_of(d, struct fpga_region_interface, dev)

struct fpga_region_interface *of_fpga_region_interface_get(struct device_node *node,
				       struct fpga_image_info *info);
struct fpga_region_interface *fpga_region_interface_get(struct device *dev,
				    struct fpga_image_info *info);
void fpga_region_interface_put(struct fpga_region_interface *bridge);
int fpga_region_interface_enable(struct fpga_region_interface *bridge);
int fpga_region_interface_disable(struct fpga_region_interface *bridge);
int fpga_region_interface_of_setup(struct fpga_region_interface* interface, struct device_node* np);

int fpga_region_interfaces_enable(struct list_head *bridge_list);
int fpga_region_interfaces_disable(struct list_head *bridge_list);
int fpga_region_interfaces_of_setup(struct list_head* interface_list, struct device_node* np);
void fpga_region_interfaces_put(struct list_head *bridge_list);
int fpga_region_interface_get_to_list(struct device *dev,
			    struct fpga_image_info *info,
			    struct list_head *bridge_list);
int of_fpga_region_interface_get_to_list(struct device_node *np,
			       struct fpga_image_info *info,
			       struct list_head *bridge_list);

struct fpga_region_interface *fpga_region_interface_create(struct device *dev, const char *name,
				       const struct fpga_region_interface_ops *ops,
				       void *priv);
void fpga_region_interface_free(struct fpga_region_interface *br);
int fpga_region_interface_register(struct fpga_region_interface *br);
void fpga_region_interface_unregister(struct fpga_region_interface *br);

struct fpga_region_interface
*devm_fpga_region_interface_create(struct device *dev, const char *name,
			 const struct fpga_region_interface_ops *ops, void *priv);

#endif /* _LINUX_FPGA_REGION_INTERFACE_H */
