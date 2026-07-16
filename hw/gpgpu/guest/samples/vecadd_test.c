#include <stdio.h>
#include <stdint.h>

#include "../gpgpu_kernel/VecAdd.h"
#include "../gpgpu_runtime/gpgpu.h"

#define VECADD_N 8

int main(int argc, char **argv)
{
    const char *kernel_path = argc > 1 ? argv[1] : "../kernels/VecAdd.bin";
    GPGPUDevice dev;
    uint32_t d_a;
    uint32_t d_b;
    uint32_t d_c;
    uint32_t d_args;
    uint32_t d_kernel;
    uint32_t grid_dim[3] = { 1, 1, 1 };
    uint32_t block_dim[3] = { VECADD_N, 1, 1 };
    float a[VECADD_N] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    float b[VECADD_N] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    float c[VECADD_N] = { 0 };
    VecAddArgs args;
    int ret;

    ret = gpgpuOpen(&dev);
    if (ret < 0) {
        printf("gpgpuOpen failed: %d\n", ret);
        return 1;
    }

    ret = gpgpuLoadKernelFromFile(&dev, kernel_path, &d_kernel);
    if (ret < 0) {
        printf("load kernel failed: %d (%s)\n", ret, kernel_path);
        gpgpuClose(&dev);
        return 1;
    }

    gpgpuMalloc(&dev, &d_a, sizeof(a));
    gpgpuMalloc(&dev, &d_b, sizeof(b));
    gpgpuMalloc(&dev, &d_c, sizeof(c));
    gpgpuMalloc(&dev, &d_args, sizeof(args));

    args.a = d_a;
    args.b = d_b;
    args.c = d_c;
    args.n = VECADD_N;

    gpgpuMemcpyHostToDevice(&dev, d_a, a, sizeof(a));
    gpgpuMemcpyHostToDevice(&dev, d_b, b, sizeof(b));
    gpgpuMemcpyHostToDevice(&dev, d_c, c, sizeof(c));
    gpgpuMemcpyHostToDevice(&dev, d_args, &args, sizeof(args));

    ret = gpgpuLaunchKernel(&dev, d_kernel, d_args,
                            grid_dim, block_dim, 0);
    if (ret < 0) {
        printf("gpgpuLaunchKernel failed: %d\n", ret);
        gpgpuClose(&dev);
        return 1;
    }

    gpgpuMemcpyDeviceToHost(&dev, c, d_c, sizeof(c));

    for (int i = 0; i < VECADD_N; i++) {
        float expected = a[i] + b[i];

        printf("c[%d] = %.1f\n", i, c[i]);
        if (c[i] != expected) {
            printf("vecadd mismatch at %d: got %.1f expected %.1f\n",
                   i, c[i], expected);
            gpgpuClose(&dev);
            return 1;
        }
    }

    printf("vecadd test passed\n");
    gpgpuClose(&dev);
    return 0;
}
