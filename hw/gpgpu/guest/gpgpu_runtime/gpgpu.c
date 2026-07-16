#include "gpgpu.h"
#include "../gpgpu_driver/uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * 前 1MB 预留给早期调试、kernel binary 或固定参数区。
 * 当前 runtime 使用简单 bump allocator 管理 [HEAP_BASE, vram_size)。
 */
#define GPGPU_HEAP_BASE 0x00100000u


/*
 * 建立 runtime 与内核驱动的连接。
 *
 * 这一步把三条基础通道准备好：
 *   1. open  获取 /dev/gpgpu0；
 *   2. ioctl 获取设备信息，尤其是 VRAM 大小；
 *   3. mmap  把 BAR2/VRAM 映射到用户态地址空间。
 *
 * 该实验版保留最小成功路径，默认驱动已经加载且 /dev/gpgpu0 可用。
 */
int gpgpuOpen(GPGPUDevice *dev){
    struct gpgpu_info info;

    dev->fd = open("/dev/gpgpu0", O_RDWR);

    ioctl(dev->fd, GPGPU_IOCTL_GET_INFO, &info);

    dev->heap = GPGPU_HEAP_BASE;

    dev->vram = mmap(NULL, info.vram_size, 
                    PROT_READ | PROT_WRITE, 
                    MAP_SHARED, 
                    dev->fd,
                    0);

    dev->vram_size = info.vram_size;
    return 0;
}

void gpgpuClose(GPGPUDevice *dev)
{
    if (!dev) {
        return;
    }

    if (dev->vram && dev->vram != MAP_FAILED) {
        munmap(dev->vram, dev->vram_size);
    }

    if (dev->fd >= 0) {
        close(dev->fd);
    }

    memset(dev, 0, sizeof(*dev));
    dev->fd = -1;
}

int gpgpuMalloc(GPGPUDevice *dev, uint32_t *dev_ptr, size_t size){
    
    uint32_t aligned_size;

    /*
     * GPGPU 指令目前以 32-bit load/store 为主，先按 4 字节对齐。
     * 返回给调用者的是 VRAM 偏移，也就是 device pointer。
     */
    aligned_size = (size + 3) & ~3u;

    *dev_ptr = dev->heap;
    dev->heap += aligned_size;

    return 0;
}

int gpgpuMemcpyHostToDevice(GPGPUDevice *dev, uint32_t dst_dev, const void *src_host, size_t size){

    /*
     * BAR2 已经 mmap 到 dev->vram，所以当前模型下主机到设备
     * 数据搬运就是一次普通 memcpy。真实 GPU 通常会经由 DMA/命令队列。
     */
    memcpy((uint8_t *)dev->vram + dst_dev, src_host, size);
    return 0;
}

int gpgpuMemcpyDeviceToHost(GPGPUDevice *dev, void *dst_host,
                            uint32_t src_dev, size_t size)
{
    /* 设备到主机同理，从 VRAM 映射读回用户缓冲区。 */
    memcpy(dst_host, (uint8_t *)dev->vram + src_dev, size);
    return 0;
}


int gpgpuLoadKernelFromFile(GPGPUDevice *dev, const char *path,
                            uint32_t *kernel_addr)
{
    FILE *fp;
    long size;
    uint32_t addr;

    fp = fopen(path, "rb");
    if (!fp) {
        return -errno;
    }

    if (fseek(fp, 0, SEEK_END) < 0) {
        int err = -errno;
        fclose(fp);
        return err;
    }

    size = ftell(fp);
    if (size < 0) {
        int err = -errno;
        fclose(fp);
        return err;
    }
    rewind(fp);

    /*
     * kernel binary 对 runtime 来说也是一段 VRAM 数据。驱动不解析
     * 文件格式，只接收最终的 VRAM 偏移并写入 KERNEL_ADDR 寄存器。
     */
    if (gpgpuMalloc(dev, &addr, (size_t)size) < 0) {
        fclose(fp);
        return -ENOMEM;
    }

    if (fread((uint8_t *)dev->vram + addr, 1, (size_t)size, fp) !=
        (size_t)size) {
        fclose(fp);
        return -EIO;
    }

    fclose(fp);
    *kernel_addr = addr;
    return 0;
}

int gpgpuLaunchKernel(GPGPUDevice *dev,
                      uint32_t kernel_addr,
                      uint32_t args_addr,
                      const uint32_t grid_dim[3],
                      const uint32_t block_dim[3],
                      uint32_t shared_mem)
{
    struct gpgpu_kernel_params params = {0};

    /*
     * runtime 只构造 launch 描述符；真正的 BAR0 MMIO 写寄存器动作
     * 在内核驱动中完成。这样用户态不需要直接访问控制寄存器。
     */
    params.kernel_addr = kernel_addr;
    params.args_addr = args_addr;
    params.grid_dim[0] = grid_dim[0];
    params.grid_dim[1] = grid_dim[1];
    params.grid_dim[2] = grid_dim[2];
    params.block_dim[0] = block_dim[0];
    params.block_dim[1] = block_dim[1];
    params.block_dim[2] = block_dim[2];
    params.shared_mem = shared_mem;

    if (ioctl(dev->fd, GPGPU_IOCTL_LAUNCH, &params) < 0) {
        return -errno;
    }

    return 0;
}
