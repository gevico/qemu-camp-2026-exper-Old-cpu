#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../gpgpu_driver/uapi.h"

int main(void)
{
    int fd;
    struct gpgpu_info info;
    uint32_t *vram;

    fd = open("/dev/gpgpu0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, GPGPU_IOCTL_GET_INFO, &info) < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    printf("vram_size = %llu bytes\n", (unsigned long long)info.vram_size);

    vram = mmap(NULL, info.vram_size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                fd,
                0);
    if (vram == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    vram[0] = 0x12345678;
    vram[1] = 0xaabbccdd;

    printf("vram[0] = 0x%08x\n", vram[0]);
    printf("vram[1] = 0x%08x\n", vram[1]);

    if (vram[0] != 0x12345678 || vram[1] != 0xaabbccdd) {
        fprintf(stderr, "VRAM mmap readback mismatch\n");
        munmap(vram, info.vram_size);
        close(fd);
        return 1;
    }

    munmap(vram, info.vram_size);
    close(fd);

    printf("mmap test passed\n");
    return 0;
}
