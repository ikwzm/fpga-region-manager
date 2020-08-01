/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _FPGA_REGION_CORE_H
#define _FPGA_REGION_CORE_H

#include <linux/device.h>
#include <linux/fpga/fpga-mgr.h>
#include "fpga-region-interface.h"

/**
 * struct fpga_region_core - FPGA Region Core structure
 * @dev: FPGA Region device
 * @mutex: enforces exclusive reference to region
 * @interface_list: list of FPGA bridges specified in region
 * @mgr: FPGA manager
 * @info: FPGA image info
 * @compat_id: FPGA region id for compatibility check.
 * @priv: private data
 * @get_interfaces: optional function to get fpga-region-interfaces to a list
 */
struct fpga_region_core {
	struct device dev;
	struct mutex mutex; /* for exclusive reference to region */
	struct list_head interface_list;
	struct fpga_manager *mgr;
	struct fpga_image_info *info;
	struct fpga_compat_id *compat_id;
	void *priv;
	int (*get_interfaces)(struct fpga_region_core *region);
};

#define to_fpga_region_core(d) container_of(d, struct fpga_region_core, dev)

struct fpga_region_core *fpga_region_core_class_find(
	struct device *start, const void *data,
	int (*match)(struct device *, const void *));

int fpga_region_core_program_fpga(struct fpga_region_core *region);

struct fpga_region_core
*fpga_region_core_create(struct device *dev, struct fpga_manager *mgr,
		    int (*get_interfaces)(struct fpga_region_core *));
void fpga_region_core_free(struct fpga_region_core *region);
int fpga_region_core_register(struct fpga_region_core *region);
void fpga_region_core_unregister(struct fpga_region_core *region);

struct fpga_region_core
*devm_fpga_region_core_create(struct device *dev, struct fpga_manager *mgr,
			int (*get_interfaces)(struct fpga_region_core *));

#endif /* _FPGA_REGION_CORE_H */
