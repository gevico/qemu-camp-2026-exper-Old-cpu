#ifndef GPGPU_UAPI_H
#define GPGPU_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * GPGPU guest 用户 ABI
 * --------------------
 *
 * 本头文件定义 guest 用户态 runtime 与 guest 内核驱动之间的 ABI。
 * 这里的 ioctl 编号、结构体布局和寄存器偏移必须保持稳定，并且要
 * 与 QEMU 设备模型中的 hw/gpgpu/gpgpu.h 保持一致。
 *
 * 注意：这里不能包含 QEMU 内部头文件，也不能暴露内核私有结构。
 * 它应该只包含用户态和内核态都能理解的固定宽度类型与常量。
 */

/* PCI 配置空间中暴露的虚拟 GPGPU 设备 ID。 */
#define GPGPU_PCI_VENDOR_ID 0x1234
#define GPGPU_PCI_DEVICE_ID 0x1337

/*
 * BAR 布局：
 *   BAR0: 控制寄存器区，由驱动通过 ioread32/iowrite32 访问；
 *   BAR2: VRAM，由驱动 mmap 给用户态 runtime 作为数据通道。
 */
#define GPGPU_BAR_CTRL 0
#define GPGPU_BAR_VRAM 2

/* 设备信息寄存器，主要用于 GET_INFO。 */
#define GPGPU_REG_DEV_ID        0x0000
#define GPGPU_REG_DEV_VERSION   0x0004
#define GPGPU_REG_VRAM_SIZE_LO  0x000c
#define GPGPU_REG_VRAM_SIZE_HI  0x0010

/* kernel dispatch 相关寄存器。 */
#define GPGPU_REG_GLOBAL_CTRL     0x0100
#define GPGPU_REG_GLOBAL_STATUS   0x0104
#define GPGPU_REG_KERNEL_ADDR_LO  0x0300
#define GPGPU_REG_KERNEL_ADDR_HI  0x0304
#define GPGPU_REG_KERNEL_ARGS_LO  0x0308
#define GPGPU_REG_KERNEL_ARGS_HI  0x030c
#define GPGPU_REG_GRID_DIM_X      0x0310
#define GPGPU_REG_GRID_DIM_Y      0x0314
#define GPGPU_REG_GRID_DIM_Z      0x0318
#define GPGPU_REG_BLOCK_DIM_X     0x031c
#define GPGPU_REG_BLOCK_DIM_Y     0x0320
#define GPGPU_REG_BLOCK_DIM_Z     0x0324
#define GPGPU_REG_SHARED_MEM_SIZE 0x0328
#define GPGPU_REG_DISPATCH        0x0330

#define GPGPU_CTRL_ENABLE         (1u << 0)
#define GPGPU_STATUS_READY        (1u << 0)
#define GPGPU_STATUS_BUSY         (1u << 1)
#define GPGPU_STATUS_ERROR        (1u << 2)

/* runtime 通过 GET_INFO 获取的设备能力摘要。 */
struct gpgpu_info {
    __u64 vram_size;
    __u32 dev_id;
    __u32 version;
};

/*
 * 一次 kernel launch 的描述符。
 *
 * kernel_addr 和 args_addr 都是 VRAM 内的偏移，不是用户态虚拟地址。
 * 驱动收到该结构体后会写入 BAR0 中的 KERNEL_ADDR、KERNEL_ARGS、
 * GRID_DIM、BLOCK_DIM 等寄存器，最后写 DISPATCH 触发执行。
 */
struct gpgpu_kernel_params {
    __u32 grid_dim[3];
    __u32 block_dim[3];
    __u64 kernel_addr;
    __u64 args_addr;
    __u32 shared_mem;
};

#define GPGPU_IOC_MAGIC      'G'

/* 获取 dev_id/version/vram_size，runtime 用 vram_size 决定 mmap 大小。 */
#define GPGPU_IOCTL_GET_INFO _IOR(GPGPU_IOC_MAGIC, 0, struct gpgpu_info)

/* 启动一个已经写入 VRAM 的 kernel。 */
#define GPGPU_IOCTL_LAUNCH _IOW(GPGPU_IOC_MAGIC, 1, struct gpgpu_kernel_params)

#endif
