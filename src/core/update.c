#include "update.h"

#include <glob.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    "tar xf " TMP_DIR "/hdzgoggle_app_ota*.tar -C " TMP_DIR "\n"

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

    progress(0);
    RUN_SCRIPT(UNTAR);

    bool is_legacy_root = !file_exists("/version");
    if (is_legacy_root) {
        system("insmod /mnt/app/ko/w25q128.ko");
    }

    // disable it66021
    IT66021_srst();

    LOGD("flashing hdz rx");
    if (!mtd_update_rx(TMP_DIR "/HDZGOGGLE_RX.bin")) {
        return UPDATE_ERROR;
    }
    progress(33);

    LOGD("flashing fpga");
    if (!mtd_update_fpga(TMP_DIR "/HDZGOGGLE_VA.bin")) {
        return UPDATE_ERROR;
    }
    progress(66);

    LOGD("flashing app");
    if (!mtd_update_app(TMP_DIR "/app.fex")) {
        return UPDATE_ERROR;
    }
    progress(100);

    if (is_legacy_root) {
        system("rmmod /mnt/app/ko/w25q128.ko");
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

    progress(0);
    if (!mtd_update_vtx(VTX_UPDATE_FILE)) {
        return UPDATE_ERROR;
    }
    progress(100);

    if (is_legacy_root) {
        system("rmmod /mnt/app/ko/w25q128.ko");
    }

    return UPDATE_SUCCESS;
}