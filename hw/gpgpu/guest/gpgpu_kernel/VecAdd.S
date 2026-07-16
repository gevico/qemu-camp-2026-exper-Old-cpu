/*
 * VecAdd kernel
 *
 * 约定：
 *   x10(a0) = VecAddArgs 在 VRAM 中的地址
 *
 * 参数块布局：
 *   args + 0  = A 数组 VRAM 地址
 *   args + 4  = B 数组 VRAM 地址
 *   args + 8  = C 数组 VRAM 地址
 *   args + 12 = n，目前第一版由 block_dim_x 控制，kernel 内不检查 n
 */
    .section .text
    .globl _start
_start:
    csrrs x5, mhartid, x0      /* x5 = mhartid */
    andi  x5, x5, 0x1f         /* x5 = lane/thread id */
    slli  x6, x5, 2            /* x6 = tid * sizeof(float) */

    lw    x11, 0(x10)          /* x11 = args->a */
    lw    x12, 4(x10)          /* x12 = args->b */
    lw    x13, 8(x10)          /* x13 = args->c */

    add   x11, x11, x6         /* x11 = &A[tid] */
    add   x12, x12, x6         /* x12 = &B[tid] */
    add   x13, x13, x6         /* x13 = &C[tid] */

    flw   f1, 0(x11)           /* f1 = A[tid] */
    flw   f2, 0(x12)           /* f2 = B[tid] */
    fadd.s f3, f1, f2          /* f3 = A[tid] + B[tid] */
    fsw   f3, 0(x13)           /* C[tid] = f3 */

    ebreak
