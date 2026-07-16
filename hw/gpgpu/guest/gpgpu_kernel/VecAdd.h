#ifndef HW_GPGPU_KERNEL_VECADD_H
#define HW_GPGPU_KERNEL_VECADD_H

#include <stdint.h>

/*
 * VecAdd 的参数块放在 VRAM 中。运行时把该地址写入
 * GPGPU_REG_KERNEL_ARGS，gpgpu_core_exec_warp() 再把它放入 x10(a0)。
 */
typedef struct VecAddArgs {
    uint32_t a;  /* A 数组在 VRAM 中的起始地址，元素类型为 FP32 */
    uint32_t b;  /* B 数组在 VRAM 中的起始地址，元素类型为 FP32 */
    uint32_t c;  /* C 数组在 VRAM 中的起始地址，元素类型为 FP32 */
    uint32_t n;  /* 元素个数，第一版 kernel 先约定 n <= block_dim_x <= 32 */
} VecAddArgs;

#endif /* HW_GPGPU_KERNEL_VECADD_H */
