#include "update.h"

#include <glob.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <log/log.h>

#include "core/mtd_flash.h"
#include "driver/it66021.h"
#include "util/file.h"

#define TMP_DIR            "/tmp/goggle_update"
#define GOGGLE_UPDATE_FILE "/mnt/extsd/HDZERO_GOGGLE*.bin"
#define VTX_UPDATE_FILE    "/mnt/extsd/HDZERO_TX.bin"

#define _RUN_SCRIPT(script) system("sh -s <<'EOF'\n" script "EOF")
#define RUN_SCRIPT(script)  _RUN_SCRIPT(script)

#define UNTAR                                                         \
    "rm -rf " TMP_DIR "\n"                                            \
    "mkdir " TMP_DIR "\n"                                             \
    "tar xf " GOGGLE_UPDATE_FILE " -C " TMP_DIR "\n"                  \
    "mv " TMP_DIR "/HDZGOGGLE_RX*.bin " TMP_DIR "/HDZGOGGLE_RX.bin\n" \
    "mv " TMP_DIR "/HDZGOGGLE_VA*.bin " TMP_DIR "/HDZGOGGLE_VA.bin\n" \
    "tar xf " TMP_DIR "/hdzgoggle_app_ota*.tar -C " TMP_DIR "\n"      \
    "mv " TMP_DIR "/system*.img " TMP_DIR "/system.img\n"

uint32_t glob_count(const char *name) {
    glob_t gstruct;

    if (glob(name, GLOB_ERR, NULL, &gstruct) != 0) {
        return 0;
    }

    uint32_t count = 0;
    char **found = gstruct.gl_pathv;
    while (*found) {
        count++;
        found++;
    }

    globfree(&gstruct);

    return count;
}

update_result_t update_goggle(update_progress_cb_t progress) {
    LOGD("starting goggle update");

    const uint32_t count = glob_count(GOGGLE_UPDATE_FILE);
    if (count == 0) {
        return UPDATE_NOT_FOUND;
    }
    if (count > 1) {
        return UPDATE_MULTIPLE_FILES;
    }

    RUN_SCRIPT(UNTAR);

    bool is_legacy_root = !file_exists("/version");
    if (is_legacy_root) {
        system("insmod /mnt/app/ko/w25q128.ko");
    }

    // disable it66021
    IT66021_srst();

    // unlink the app to force the kernel to preserve the inode
    system("mv /mnt/app/app/HDZGOGGLE /tmp/HDZGOGGLE");

    // lock all memory pages to prevent paging request to the removed binary
    mlockall(MCL_CURRENT | MCL_FUTURE);

    LOGD("flashing hdz rx");
    if (!mtd_update_rx(progress, TMP_DIR "/HDZGOGGLE_RX.bin")) {
        return UPDATE_ERROR;
    }

    LOGD("flashing fpga");
    if (!mtd_update_fpga(progress, TMP_DIR "/HDZGOGGLE_VA.bin")) {
        return UPDATE_ERROR;
    }

    if (file_exists(TMP_DIR "/system.img")) {
        LOGD("flashing system");
        if (!mtd_update_system(progress, TMP_DIR "/system.img")) {
            return UPDATE_ERROR;
        }
    } else {
        LOGD("flashing app");
        if (!mtd_update_app(progress, TMP_DIR "/app.fex")) {
            return UPDATE_ERROR;
        }
    }

    if (is_legacy_root) {
        system("rmmod /mnt/app/ko/w25q128.ko");
    }

    return UPDATE_SUCCESS;
}

bool update_needs_finalizing() {
    if (file_exists("/mnt/UDISK/.version_lock")) {
        // version lock is present, we are up to date
        return false;
    }

    return true;
}

update_result_t update_finalize(update_progress_cb_t progress) {
    LOGD("finalizing update");

    const uint32_t count = glob_count(GOGGLE_UPDATE_FILE);
    if (count == 0) {
        return UPDATE_NOT_FOUND;
    }
    if (count > 1) {
        return UPDATE_MULTIPLE_FILES;
    }

    // unlink the app to force the kernel to preserve the inode
    system("mv /mnt/app/app/HDZGOGGLE /tmp/HDZGOGGLE");

    // lock all memory pages to prevent paging request to the removed binary
    mlockall(MCL_CURRENT | MCL_FUTURE);

    system("rmmod /mnt/app/ko/w25q128.ko");

    RUN_SCRIPT(UNTAR);

    if (!file_exists(TMP_DIR "/system.img")) {
        return UPDATE_NOT_FOUND;
    }

    LOGD("flashing system");
    if (!mtd_update_system(progress, TMP_DIR "/system.img")) {
        return UPDATE_ERROR;
    }

    return UPDATE_SUCCESS;
}

update_result_t update_vtx(update_progress_cb_t progress) {
    LOGD("starting vtx update");

    if (!file_exists(VTX_UPDATE_FILE)) {
        return UPDATE_NOT_FOUND;
    }

    if (!mtd_detect_vtx()) {
        return UPDATE_ERROR;
    }

    bool is_legacy_root = !file_exists("/version");
    if (is_legacy_root) {
        system("insmod /mnt/app/ko/w25q128.ko");
    }

    if (!mtd_update_vtx(progress, VTX_UPDATE_FILE)) {
        return UPDATE_ERROR;
    }

    if (is_legacy_root) {
        system("rmmod /mnt/app/ko/w25q128.ko");
    }

    return UPDATE_SUCCESS;
}