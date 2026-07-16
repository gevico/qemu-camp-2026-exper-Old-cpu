#include <stdio.h>
#include <string.h>

#include "../gpgpu_runtime/gpgpu.h"

int main(void)
{
    GPGPUDevice dev;
    uint32_t d_a;
    float a[4] = {1, 2, 3, 4};
    float b[4] = {0};

    if (gpgpuOpen(&dev) < 0) {
        printf("gpgpuOpen failed\n");
        return 1;
    }

    gpgpuMalloc(&dev, &d_a, sizeof(a));
    gpgpuMemcpyHostToDevice(&dev, d_a, a, sizeof(a));
    gpgpuMemcpyDeviceToHost(&dev, b, d_a, sizeof(b));

    for (int i = 0; i < 4; i++) {
        printf("b[%d] = %.1f\n", i, b[i]);
    }

    gpgpuClose(&dev);
    return 0;
}
