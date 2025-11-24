#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>

#define VFIO_TYPE (';')
#define VFIO_BASE 100
#define VFIO_GET_API_VERSION _IO(VFIO_TYPE, VFIO_BASE + 0)
#define VFIO_CHECK_EXTENSION _IO(VFIO_TYPE, VFIO_BASE + 1)
#define VFIO_TYPE1_IOMMU 1

int main() {
    int fd = open("/dev/vfio/vfio", O_RDWR);
    if (fd < 0) {
        printf("ERROR: Cannot open /dev/vfio/vfio: %s\n", strerror(errno));
        return 1;
    }
    printf("✓ Opened /dev/vfio/vfio\n");
    
    int api_version = ioctl(fd, VFIO_GET_API_VERSION);
    if (api_version < 0) {
        printf("ERROR: VFIO_GET_API_VERSION failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("✓ VFIO API version: %d\n", api_version);
    
    int supported = ioctl(fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU);
    if (supported) {
        printf("✓ VFIO Type1 IOMMU is supported\n");
    } else {
        printf("✗ VFIO Type1 IOMMU is NOT supported\n");
    }
    
    close(fd);
    return 0;
}
