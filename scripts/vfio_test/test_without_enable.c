#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#define VFIO_TYPE (';')
#define VFIO_BASE 100
#define VFIO_GET_API_VERSION _IO(VFIO_TYPE, VFIO_BASE + 0)
#define VFIO_CHECK_EXTENSION _IO(VFIO_TYPE, VFIO_BASE + 1)
#define VFIO_SET_IOMMU _IO(VFIO_TYPE, VFIO_BASE + 2)
#define VFIO_GROUP_SET_CONTAINER _IO(VFIO_TYPE, VFIO_BASE + 4)
#define VFIO_GROUP_GET_DEVICE_FD _IO(VFIO_TYPE, VFIO_BASE + 6)
#define VFIO_DEVICE_GET_INFO _IO(VFIO_TYPE, VFIO_BASE + 7)
#define VFIO_TYPE1_IOMMU 1

struct vfio_group_status {
    uint32_t argsz;
    uint32_t flags;
};

struct vfio_device_info {
    uint32_t argsz;
    uint32_t flags;
    uint16_t num_regions;
    uint16_t num_irqs;
};

int main() {
    int container_fd = open("/dev/vfio/vfio", O_RDWR);
    if (container_fd < 0) {
        printf("ERROR: Cannot open container: %s\n", strerror(errno));
        return 1;
    }
    printf("✓ Opened container\n");
    
    int group_fd = open("/dev/vfio/8", O_RDWR);
    if (group_fd < 0) {
        printf("ERROR: Cannot open group: %s\n", strerror(errno));
        close(container_fd);
        return 1;
    }
    printf("✓ Opened group 8\n");
    
    if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd) < 0) {
        printf("ERROR: SET_CONTAINER failed: %s\n", strerror(errno));
        goto cleanup;
    }
    printf("✓ Group added to container\n");
    
    if (ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) < 0) {
        printf("ERROR: SET_IOMMU failed: %s\n", strerror(errno));
        goto cleanup;
    }
    printf("✓ IOMMU type set\n");
    
    // Skip IOMMU_ENABLE and try to get device FD
    int device_fd = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, "0000:01:00.0");
    if (device_fd < 0) {
        printf("ERROR: GET_DEVICE_FD failed: %s\n", strerror(errno));
        goto cleanup;
    }
    printf("✓ Got device FD: %d\n", device_fd);
    
    struct vfio_device_info info = { .argsz = sizeof(info) };
    if (ioctl(device_fd, VFIO_DEVICE_GET_INFO, &info) < 0) {
        printf("ERROR: GET_INFO failed: %s\n", strerror(errno));
        close(device_fd);
        goto cleanup;
    }
    printf("✓ Device info: regions=%u, irqs=%u\n", info.num_regions, info.num_irqs);
    
    close(device_fd);
cleanup:
    close(group_fd);
    close(container_fd);
    return 0;
}
