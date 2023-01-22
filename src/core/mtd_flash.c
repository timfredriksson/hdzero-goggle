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

#define BUFFER_SIZE (8UL * 1024UL)

#define DRIVER_BIND        "/sys/bus/spi/drivers/m25p80/bind"
#define DRIVER_LEGACY_BIND "/sys/bus/spi/drivers/w25q128/bind"

#define DRIVER_UNBIND        "/sys/bus/spi/drivers/m25p80/unbind"
#define DRIVER_LEGACY_UNBIND "/sys/bus/spi/drivers/w25q128/unbind"

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
        LOGW("%s not found in /proc/mtd", name);
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

static void spi_driver_bind(const char *dev) {
    file_printf(DRIVER_BIND, dev);
    usleep(2000);
    file_printf(DRIVER_LEGACY_BIND, dev);
    usleep(2000);
}

static void spi_driver_unbind(const char *dev) {
    file_printf(DRIVER_UNBIND, dev);
    usleep(2000);
    file_printf(DRIVER_LEGACY_UNBIND, dev);
    usleep(2000);
}

static bool mtd_erase_all(update_progress_cb_t progress, int fd) {
    struct mtd_info_user mtd;
    if (getmeminfo(fd, &mtd) < 0) {
        LOGE("getmeminfo failed");
        return false;
    }

    progress(0);

    struct erase_info_user ei;
    ei.length = mtd.erasesize;
    for (ei.start = 0; ei.start < mtd.size; ei.start += ei.length) {
        LOGD("erasing bytes 0x%x - 0x%x", ei.start, ei.start + ei.length);
        if (memerase(fd, &ei) < 0) {
            LOGE("memerase failed");
            return false;
        }
        progress((ei.start * 100) / mtd.size);
    }

    progress(100);

    return true;
}

static bool mtd_write_all(update_progress_cb_t progress, int fd, const char *filename) {
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

    progress(0);
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
        LOGD("wrote %d bytes", offset);
        offset += written;
        progress((offset * 100) / file_size);
    }

    free(buf);
    fclose(fp);

    progress(100);

    return true;
}

static bool mtd_write_file(update_progress_cb_t progress, const char *dev_name, const char *filename) {
    char dev_path[64];
    if (!mtd_find_device(dev_name, dev_path)) {
        return false;
    }

    int fd = open(dev_path, O_SYNC | O_RDWR);
    if (fd < 0) {
        LOGE("open %s failed", dev_path);
        return false;
    }

    if (!mtd_erase_all(progress, fd)) {
        close(fd);
        return false;
    }

    bool ret = mtd_write_all(progress, fd, filename);
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

bool mtd_update_rx(update_progress_cb_t progress, const char *filename) {
    LOGD("disconnect hdz rx");
    disconnect_hdz_rx();

    spi_driver_bind("spi1.0");
    spi_driver_bind("spi1.1");

    bool ret = mtd_write_file(progress, "spi1.0", filename);
    if (ret) {
        ret = mtd_write_file(progress, "spi1.1", filename);
    }

    spi_driver_unbind("spi1.1");
    spi_driver_unbind("spi1.0");

    LOGD("connect hdz rx");
    connect_hdz_rx();

    return ret;
}

bool mtd_update_fpga(update_progress_cb_t progress, const char *filename) {
    LOGD("disconnect fpga");
    disconnect_fpga();

    spi_driver_bind("spi3.0");
    bool ret = mtd_write_file(progress, "spi3.0", filename);
    spi_driver_unbind("spi3.0");

    LOGD("connect fpga");
    connect_fpga();

    return ret;
}

bool mtd_update_system(update_progress_cb_t progress, const char *filename) {
    // erase boot sector
    {
        int fd = open("/dev/mtd0", O_SYNC | O_RDWR);
        if (fd < 0) {
            LOGE("open /dev/mtd0 failed");
            return false;
        }
        if (!mtd_erase_all(progress, fd)) {
            close(fd);
            return false;
        }
        close(fd);
    }

    // re-read partion table, should be empty now,
    // giving us access to the whole device on mtd0
    spi_driver_unbind("spi0.0");
    spi_driver_bind("spi0.0");

    // write the new image to device
    {
        int fd = open("/dev/mtd0", O_SYNC | O_RDWR);
        if (fd < 0) {
            LOGE("open /dev/mtd0 failed");
            return false;
        }
        if (!mtd_erase_all(progress, fd)) {
            close(fd);
            return false;
        }
        if (!mtd_write_all(progress, fd, filename)) {
            close(fd);
            return false;
        }
        close(fd);
    }

    // re-read partion table, giving us back partions as written
    spi_driver_unbind("spi0.0");
    spi_driver_bind("spi0.0");

    return true;
}

bool mtd_update_app(update_progress_cb_t progress, const char *filename) {
    return mtd_write_file(progress, "app", filename);
}

bool mtd_detect_vtx() {
    spi_driver_bind("spi1.0");
    const uint32_t size = mtd_get_size("spi1.0");
    spi_driver_unbind("spi1.0");

    // we expect a flash chip with 1MB
    // this check should probably be extended
    return size == 1048576;
}

bool mtd_update_vtx(update_progress_cb_t progress, const char *filename) {
    spi_driver_bind("spi1.0");
    bool ret = mtd_write_file(progress, "spi1.0", filename);
    spi_driver_unbind("spi1.0");
    return ret;
}