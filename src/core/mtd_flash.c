#include "mtd_flash.h"

#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <log/log.h>

#include "defines.h"
#include "driver/gpio.h"
#include "util/file.h"

#define PROC_MTD_DELIM ": \""

#define BUFFER_SIZE (4UL * 1024UL * 1024UL)

#define DRIVER_BIND   "/sys/bus/spi/drivers/m25p80/bind"
#define DRIVER_UNBIND "/sys/bus/spi/drivers/m25p80/unbind"

static void disconnect_fpga() {
    gpio_set(GPIO_FPGA_RESET, 1);
}

static void connect_fpga() {
    gpio_set(GPIO_FPGA_RESET, 0);
}

static void disconnect_hdz_rx() {
    gpio_set(GPIO_HDZ_RX_RESET, 0);
    gpio_set(GPIO_HDZ_TX_RESET, 1);
}

static void connect_hdz_rx() {
    gpio_set(GPIO_HDZ_RX_RESET, 1);
    gpio_set(GPIO_HDZ_TX_RESET, 0);
}

// /proc/mtd:
// dev:    size   erasesize  name
// mtd0: 00100000 00010000 "uboot"
static bool mtd_find_device(const char *name, char *dev) {
    FILE *fp = fopen("/proc/mtd", "r");
    if (!fp) {
        return false;
    }

    char *tokens[4];

    char *line = NULL;
    size_t len = 0;
    bool found = false;

    // skip header
    ssize_t read = getline(&line, &len, fp);
    while ((read = getline(&line, &len, fp)) != -1) {
        tokens[0] = strtok(line, PROC_MTD_DELIM);
        for (size_t i = 1; i < 4; i++) {
            tokens[i] = strtok(NULL, PROC_MTD_DELIM);
        }
        if (tokens[3] == NULL) {
            continue;
        }

        if (strcmp(tokens[3], name) == 0) {
            found = true;
            break;
        }
    }
    fclose(fp);

    if (!found) {
        LOGW("%s not found in /proc/mtd");
        return false;
    }

    strcpy(dev, "/dev/");
    strcat(dev, tokens[0]);
    LOGD("identified %s as %s in /proc/mtd", name, dev);
    return true;
}

static int memerase(int fd, struct erase_info_user *erase) {
    return ioctl(fd, MEMERASE, erase);
}

static int getmeminfo(int fd, struct mtd_info_user *mtd) {
    return ioctl(fd, MEMGETINFO, mtd);
}

static bool mtd_erase_all(int fd) {
    struct mtd_info_user mtd;
    if (getmeminfo(fd, &mtd) < 0) {
        LOGE("getmeminfo failed");
        return false;
    }

    struct erase_info_user erase;
    erase.start = 0x0;
    erase.length = mtd.size;
    LOGD("erasing %d bytes", mtd.size);
    if (memerase(fd, &erase) < 0) {
        LOGE("memerase failed");
        return false;
    }

    return true;
}

static bool mtd_write_all(int fd, const char *filename) {
    uint8_t *buf = (uint8_t *)malloc(BUFFER_SIZE);
    if (!buf) {
        return false;
    }

    if (lseek(fd, 0, SEEK_SET) != 0) {
        return false;
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        LOGE("fopen %s failed", filename);
        free(buf);
        return false;
    }

    fseek(fp, 0L, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    LOGD("writing %d bytes from %s", file_size, filename);

    size_t offset = 0;
    while (offset < file_size) {
        size_t read = fread(buf, 1, BUFFER_SIZE, fp);
        if (read == 0) {
            break;
        }

        ssize_t written = write(fd, buf, read);
        if (written < 0) {
            LOGE("write error %d", written);
            break;
        }
        offset += written;
    }

    free(buf);
    fclose(fp);

    return true;
}

static bool mtd_write_file(const char *dev_name, const char *filename) {
    char dev_path[64];
    if (!mtd_find_device(dev_name, dev_path)) {
        return false;
    }

    int fd = open(dev_path, O_SYNC | O_RDWR);
    if (fd < 0) {
        LOGE("open %s failed", dev_path);
        return false;
    }

    if (!mtd_erase_all(fd)) {
        close(fd);
        return false;
    }

    bool ret = mtd_write_all(fd, filename);
    close(fd);
    return ret;
}

static uint32_t mtd_get_size(const char *dev_name) {
    char dev_path[64];
    if (!mtd_find_device("spi1.0", dev_path)) {
        return 0;
    }

    int fd = open(dev_path, O_SYNC | O_RDWR);
    if (fd < 0) {
        LOGE("open %s failed", dev_path);
        return 0;
    }

    struct mtd_info_user mtd;
    if (getmeminfo(fd, &mtd) < 0) {
        LOGE("getmeminfo failed");
        close(fd);
        return 0;
    }

    close(fd);
    return mtd.size;
}

bool mtd_update_rx(const char *filename) {
    LOGD("disconnect hdz rx");
    disconnect_hdz_rx();

    file_printf(DRIVER_BIND, "spi1.0");
    file_printf(DRIVER_BIND, "spi1.1");

    bool ret = mtd_write_file("spi1.0", filename);
    if (ret) {
        ret = mtd_write_file("spi1.1", filename);
    }

    file_printf(DRIVER_UNBIND, "spi1.1");
    file_printf(DRIVER_UNBIND, "spi1.0");

    LOGD("connect hdz rx");
    connect_hdz_rx();

    return ret;
}

bool mtd_update_fpga(const char *filename) {
    LOGD("disconnect fpga");
    disconnect_fpga();

    file_printf(DRIVER_BIND, "spi3.0");
    bool ret = mtd_write_file("spi3.0", filename);
    file_printf(DRIVER_UNBIND, "spi3.0");

    LOGD("connect fpga");
    connect_fpga();

    return ret;
}

bool mtd_update_app(const char *filename) {
    return mtd_write_file("app", filename);
}

bool mtd_detect_vtx() {
    file_printf(DRIVER_BIND, "spi1.0");
    const uint32_t size = mtd_get_size("spi1.0");
    file_printf(DRIVER_UNBIND, "spi1.0");

    // we expect a flash chip with 1MB
    // this check should probably be extended
    return size == 1048576;
}

bool mtd_update_vtx(const char *filename) {
    file_printf(DRIVER_BIND, "spi1.0");
    bool ret = mtd_write_file("spi1.0", filename);
    file_printf(DRIVER_UNBIND, "spi1.0");
    return ret;
}