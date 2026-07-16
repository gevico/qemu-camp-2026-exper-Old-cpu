#ifndef GPGPU_DRIVER_H
#define GPGPU_DRIVER_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/pci.h>

#include "uapi.h"

/*
 * 一个被驱动绑定的 GPGPU PCI 设备实例。
 *
 * 当前实验只支持单设备，但仍然把状态放进 per-device 结构中，
 * 这样 probe/remove/open/ioctl/mmap 都能通过同一个对象传递上下文。
 */
struct gpgpu_dev {
    struct pci_dev *pdev;   /* Linux PCI 子系统提供的设备对象。 */
    void __iomem *bar0;     /* BAR0 控制寄存器的内核映射地址。 */

    resource_size_t vram_phys;  /* BAR2/VRAM 的 guest 物理起始地址。 */
    resource_size_t vram_len;   /* BAR2/VRAM 的长度。 */

    struct cdev cdev;       /* /dev/gpgpu0 对应的字符设备。 */
    dev_t devt;             /* 动态分配的设备号。 */
    struct device *device;  /* sysfs/devtmpfs 中的设备对象。 */
};


#endif
