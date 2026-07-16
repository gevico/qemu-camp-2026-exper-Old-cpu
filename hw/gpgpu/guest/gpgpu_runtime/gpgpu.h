#ifndef GPGPU_RUNTIME_H
#define GPGPU_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

/*
 * 用户态 GPGPU runtime。
 *
 * 这一层是应用程序看到的“类 CUDA”接口。它不直接访问 PCI BAR，
 * 而是通过 /dev/gpgpu0 调用驱动：
 *
 *   - ioctl(GET_INFO/LAUNCH) 作为控制通道；
 *   - mmap(BAR2) 作为数据通道；
 *   - uint32_t 设备指针表示 VRAM 偏移，不是用户态指针。
 */
typedef struct GPGPUDevice{
    int fd;              /* /dev/gpgpu0 的文件描述符。 */
    void *vram;          /* mmap 后的 VRAM 用户态虚拟地址。 */
    uint64_t vram_size;  /* 驱动通过 gpgpu_info 返回的 VRAM 大小。 */
    uint32_t heap;       /* 简单递增分配器的下一个 VRAM 偏移。 */
}GPGPUDevice;

/* 打开设备，读取设备信息，并把 VRAM 映射到当前进程。 */
int gpgpuOpen(GPGPUDevice *dev);

/* 释放 gpgpuOpen() 获取的 mmap 映射和设备文件描述符。 */
void gpgpuClose(GPGPUDevice *dev);

/*
 * 在 VRAM 中分配连续空间。
 *
 * 当前版本只递增 heap，不支持 free；返回值 dev_ptr 是 VRAM 偏移。
 */
int gpgpuMalloc(GPGPUDevice *dev, uint32_t *dev_ptr, size_t size);

/* 从普通用户态内存复制到 VRAM。 */
int gpgpuMemcpyHostToDevice(GPGPUDevice *dev, uint32_t dst_dev, 
                    const void *src_host, size_t size);

/* 从 VRAM 复制回普通用户态内存。 */
int gpgpuMemcpyDeviceToHost(GPGPUDevice *dev, void *dst_host,
                    uint32_t src_dev, size_t size);

/* 读取 kernel binary 文件，将其放入 VRAM，并返回 kernel 的 VRAM 偏移。 */
int gpgpuLoadKernelFromFile(GPGPUDevice *dev, const char *path,
                      uint32_t *kernel_addr);

/*
 * 启动一个已加载到 VRAM 的 kernel。
 *
 * args_addr 是参数块的 VRAM 偏移。当前 QEMU GPGPU core 会在执行
 * lane 前把该地址放入 x10(a0)，kernel 通过 x10 读取参数。
 */
int gpgpuLaunchKernel(GPGPUDevice *dev,
                      uint32_t kernel_addr,
                      uint32_t args_addr,
                      const uint32_t grid_dim[3],
                      const uint32_t block_dim[3],
                      uint32_t shared_mem);

#endif
