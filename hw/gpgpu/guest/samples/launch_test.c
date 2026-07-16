#include <stdint.h>
#include <stdio.h>

#include "../gpgpu_runtime/gpgpu.h"

/*
 * 单文件 launch 测试：
 * 这里直接内嵌一个最小 GPGPU kernel，避免再准备 empty.S/empty.bin。
 *
 * 0x00100073 是 RISC-V ebreak 指令的小端字节序。
 * 对当前模拟器来说，它表示 kernel 正常结束。
 */
static const uint8_t empty_kernel[] = {
    0x73, 0x00, 0x10, 0x00,
};

int main(void)
{
    GPGPUDevice dev;
    uint32_t kernel_addr;
    uint32_t grid_dim[3] = { 1, 1, 1 };
    uint32_t block_dim[3] = { 1, 1, 1 };
    int ret;

    ret = gpgpuOpen(&dev);
    if (ret < 0) {
        printf("gpgpuOpen failed: %d\n", ret);
        return 1;
    }

    ret = gpgpuMalloc(&dev, &kernel_addr, sizeof(empty_kernel));
    if (ret < 0) {
        printf("gpgpuMalloc kernel failed: %d\n", ret);
        gpgpuClose(&dev);
        return 1;
    }

    ret = gpgpuMemcpyHostToDevice(&dev, kernel_addr,
                                  empty_kernel, sizeof(empty_kernel));
    if (ret < 0) {
        printf("copy kernel failed: %d\n", ret);
        gpgpuClose(&dev);
        return 1;
    }

    ret = gpgpuLaunchKernel(&dev, kernel_addr, 0,
                            grid_dim, block_dim, 0);
    if (ret < 0) {
        printf("gpgpuLaunchKernel failed: %d\n", ret);
        gpgpuClose(&dev);
        return 1;
    }

    printf("launch test passed, kernel_addr=0x%08x\n", kernel_addr);

    gpgpuClose(&dev);
    return 0;
}
