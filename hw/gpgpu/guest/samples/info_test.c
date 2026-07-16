#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "uapi.h"

int main(void)
{
    int fd;
    struct gpgpu_info info;

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

    printf("dev_id    = 0x%08x\n", info.dev_id);
    printf("version   = 0x%08x\n", info.version);
    printf("vram_size = %llu bytes\n", (unsigned long long)info.vram_size);

    close(fd);
    return 0;
}