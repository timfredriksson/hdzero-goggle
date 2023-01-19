#include "page_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <log/log.h>
#include <minIni.h>

#include "common.hh"
#include "core/elrs.h"
#include "core/esp32_flash.h"
#include "core/update.h"
#include "driver/dm5680.h"
#include "driver/esp32.h"
#include "driver/fans.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "ui/page_common.h"
#include "ui/ui_main_menu.h"
#include "ui/ui_style.h"
#include "util/file.h"

static lv_coord_t col_dsc[] = {160, 160, 160, 160, 160, 160, 160, LV_GRID_TEMPLATE_LAST};
static lv_coord_t row_dsc[] = {60, 60, 60, 60, 60, 60, 60, 60, 60, 60, LV_GRID_TEMPLATE_LAST};

static lv_obj_t *bar_vtx = NULL;
static lv_obj_t *btn_vtx = NULL;
static lv_obj_t *bar_goggle = NULL;
static lv_obj_t *btn_goggle = NULL;
static lv_obj_t *bar_esp = NULL;
static lv_obj_t *btn_esp = NULL;
static lv_obj_t *label_esp = NULL;

#define ADDR_AL            0x65
#define ADDR_FPGA          0x64
#define I2C_Write(s, a, d) i2c_write(2, s, a, d)
#define I2C_Read(s, a)     i2c_read(2, s, a)

static bool reboot_flag = false;
static lv_obj_t *cur_ver_label;

#undef RETURN_ON_ERROR
#define RETURN_ON_ERROR(m, x)              \
    do {                                   \
        esp_loader_error_t _err_ = (x);    \
        if (_err_ != ESP_LOADER_SUCCESS) { \
            LOGE("ERR %s: %d", m, _err_);  \
            return _err_;                  \
        }                                  \
    } while (0)

static esp_loader_error_t flash_esp32_file(char *path, uint32_t offset) {
    char fpath[80];
    strcpy(fpath, "/mnt/extsd/ELRS/");
    strcat(fpath, path);

    FILE *image = fopen(fpath, "r");
    if (image == NULL) {
        LOGI("Firmware file does not exist %s", fpath);
        return ESP_LOADER_SUCCESS;
    }

    lv_label_set_text(btn_esp, path);
    lv_timer_handler();

    fseek(image, 0L, SEEK_END);
    size_t size = ftell(image);
    rewind(image);

    uint8_t buffer[4096];
    RETURN_ON_ERROR("start", esp_loader_flash_start(offset, size, sizeof(buffer)));
    uint32_t read, current = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), image))) {
        RETURN_ON_ERROR("write", esp_loader_flash_write(buffer, read));
        current += read;
        int percent = (current * 100) / size;
        lv_bar_set_value(bar_esp, percent, LV_ANIM_OFF);
        lv_timer_handler();
        LOGD("%d %d %d", current, size, percent);
    }
    return ESP_LOADER_SUCCESS;
}

static esp_loader_error_t flash_esp32() {
    disable_esp32();

    esp_loader_connect_args_t config = ESP_LOADER_CONNECT_DEFAULT();
    RETURN_ON_ERROR("init", loader_port_init());
    RETURN_ON_ERROR("connect", esp_loader_connect(&config));
    RETURN_ON_ERROR("get_target", esp_loader_get_target() == ESP32_CHIP ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_UNSUPPORTED_CHIP);

    lv_bar_set_value(bar_esp, 0, LV_ANIM_OFF);
    RETURN_ON_ERROR("flash", flash_esp32_file("bootloader.bin", 0x1000));
    lv_bar_set_value(bar_esp, 0, LV_ANIM_OFF);
    RETURN_ON_ERROR("flash", flash_esp32_file("partitions.bin", 0x8000));
    lv_bar_set_value(bar_esp, 0, LV_ANIM_OFF);
    RETURN_ON_ERROR("flash", flash_esp32_file("boot_app0.bin", 0xE000));
    lv_bar_set_value(bar_esp, 0, LV_ANIM_OFF);
    RETURN_ON_ERROR("flash", flash_esp32_file("firmware.bin", 0x10000));

    RETURN_ON_ERROR("finish", esp_loader_flash_finish(true));
    loader_port_close();

    if (g_setting.elrs.enable)
        enable_esp32();

    return ESP_LOADER_SUCCESS;
}

static bool flash_elrs() {
    esp_loader_error_t ret = flash_esp32();
    loader_port_close();

    if (g_setting.elrs.enable)
        enable_esp32();

    return ret;
}

int generate_current_version(sys_version_t *sys_ver) {
    char strline[128];
    char strtmp[25];
    memset(strtmp, 0, sizeof(strtmp));
    sys_ver->va = I2C_Read(ADDR_FPGA, 0xff);
    sys_ver->app = 0;
    sys_ver->rx = rx_status[0].rx_ver;

    FILE *fp = fopen("/mnt/app/version", "r");
    if (!fp)
        goto err_open;

    while (!feof(fp)) {
        char *p = fgets(strline, sizeof(strline), fp);
        if (!p)
            goto err_fget;

        if (strncmp(strline, "major", 5) == 0) {
            strcat(strtmp, &strline[7]);
            sys_ver->app = atoi(strtmp);
            break;
        }
        LOGI(">>%s", strline);
    }

    LOGI("va:%d, rx:%d, app:%d", sys_ver->va,
         sys_ver->rx,
         sys_ver->app);
    fclose(fp);

    sprintf(sys_ver->current, "%d.%d.%d",
            sys_ver->app,
            sys_ver->rx,
            sys_ver->va);
    return 0;

err_fget:
    fclose(fp);
err_open:
    return -1;
}

static lv_obj_t *page_version_create(lv_obj_t *parent, panel_arr_t *arr) {
    lv_obj_t *page = lv_menu_page_create(parent, NULL);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(page, 1053, 900);
    lv_obj_add_style(page, &style_subpage, LV_PART_MAIN);
    lv_obj_set_style_pad_top(page, 94, 0);

    lv_obj_t *section = lv_menu_section_create(page);
    lv_obj_add_style(section, &style_submenu, LV_PART_MAIN);
    lv_obj_set_size(section, 1053, 894);

    create_text(NULL, section, false, "Firmware:", LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t *cont = lv_obj_create(section);
    lv_obj_set_size(cont, 960, 600);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_layout(cont, LV_LAYOUT_GRID);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont, &style_context, LV_PART_MAIN);

    lv_obj_set_style_grid_column_dsc_array(cont, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(cont, row_dsc, 0);

    create_select_item(arr, cont);
    cur_ver_label = create_label_item(cont, "Current Version:", 1, 0, 2);

    btn_vtx = create_label_item(cont, "Update VTX", 1, 1, 2);
    btn_goggle = create_label_item(cont, "Update Goggle", 1, 2, 2);
    btn_esp = create_label_item(cont, "Update ESP32", 1, 3, 2);
    label_esp = create_label_item(cont, "", 3, 3, 2);
    create_label_item(cont, "< Back", 1, 4, 1);

    bar_vtx = lv_bar_create(cont);
    lv_obj_set_size(bar_vtx, 320, 20);
    lv_obj_set_grid_cell(bar_vtx, LV_GRID_ALIGN_CENTER, 3, 3,
                         LV_GRID_ALIGN_CENTER, 1, 1);
    lv_obj_add_flag(bar_vtx, LV_OBJ_FLAG_HIDDEN);

    bar_goggle = lv_bar_create(cont);
    lv_obj_set_size(bar_goggle, 320, 20);
    lv_obj_set_grid_cell(bar_goggle, LV_GRID_ALIGN_CENTER, 3, 3,
                         LV_GRID_ALIGN_CENTER, 2, 1);
    lv_obj_add_flag(bar_goggle, LV_OBJ_FLAG_HIDDEN);

    bar_esp = lv_bar_create(cont);
    lv_obj_set_size(bar_esp, 320, 20);
    lv_obj_set_grid_cell(bar_esp, LV_GRID_ALIGN_CENTER, 3, 3,
                         LV_GRID_ALIGN_CENTER, 3, 1);
    lv_obj_add_flag(bar_esp, LV_OBJ_FLAG_HIDDEN);

    return page;
}

static void elrs_version_timer(struct _lv_timer_t *timer) {
    char label[80];
    uint8_t version[32] = {0};
    uint16_t size = sizeof(version) - 1;

    if (!msp_read_resposne(MSP_GET_BP_VERSION, &size, version)) {
        msp_send_packet(MSP_GET_BP_VERSION, MSP_PACKET_COMMAND, 0, NULL);
        return;
    }
    lv_timer_del(timer);
    sprintf(label, "ver: %s", version);
    lv_label_set_text(label_esp, label);
}

static void update_vtx_progress(uint32_t val) {
    if (!bar_vtx) {
        return;
    }

    lv_bar_set_value(bar_vtx, val, LV_ANIM_OFF);
    lv_timer_handler();
}

static void update_goggle_progress(uint32_t val) {
    if (!bar_goggle) {
        return;
    }

    lv_bar_set_value(bar_goggle, val, LV_ANIM_OFF);
    lv_timer_handler();
}

static void page_version_enter() {
    version_update_title();

    lv_label_set_text(label_esp, "");
    msp_send_packet(MSP_GET_BP_VERSION, MSP_PACKET_COMMAND, 0, NULL);
    lv_timer_t *timer = lv_timer_create(elrs_version_timer, 250, NULL);
    lv_timer_set_repeat_count(timer, 20);
}

static void page_version_on_roller(uint8_t key) {
    version_update_title();
}

static void page_version_on_click(uint8_t key, int sel) {
    version_update_title();
    if (sel == 0) {
        FILE *fp;
        char buf[80];
        int dat[16];
        fp = fopen("/tmp/wr_reg", "r");
        if (fp) {
            while (fgets(buf, 80, fp)) {
                sscanf(buf, "%x %x %x", &dat[0], &dat[1], &dat[2]);
                DM5680_WriteReg(dat[0], dat[1], dat[2]);
                LOGI("DM5680 REG[%02x,%02x]<-%02x", dat[0], dat[1], dat[2]);
                usleep(100000);
            }
            fclose(fp);
            // system("rm /tmp/wr_reg");
        }

        fp = fopen("/tmp/rd_reg", "r");
        if (!fp)
            return;
        while (fgets(buf, 80, fp)) {
            sscanf(buf, "%x %x", &dat[0], &dat[1]);
            DM5680_ReadReg(dat[0], dat[1]);
            sleep(1);
            LOGI("DM5680_0 REG[%02x,%02x]-> %02x", dat[0], dat[1], rx_status[0].rx_regval);
            LOGI("DM5680_1 REG[%02x,%02x]-> %02x", dat[0], dat[1], rx_status[1].rx_regval);
        }
        fclose(fp);
        // system("rm /tmp/rd_reg");
    } else if (sel == 1) {
        lv_obj_clear_flag(bar_vtx, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(btn_vtx, "Flashing...");
        lv_timer_handler();

        const update_result_t ret = update_vtx(update_vtx_progress);

        lv_obj_add_flag(bar_vtx, LV_OBJ_FLAG_HIDDEN);
        lv_timer_handler();

        switch (ret) {
        case UPDATE_SUCCESS:
            lv_label_set_text(btn_vtx, "#000FF00 SUCCESS#");
            lv_timer_handler();
            break;

        case UPDATE_VERIFY_FAIL:
            lv_label_set_text(btn_vtx, "#FF0000 Verification failed, try it again#");
            lv_timer_handler();
            break;

        case UPDATE_NOT_FOUND:
            lv_label_set_text(btn_vtx, "#FFFF00 No firmware found.#");
            lv_timer_handler();
            break;

        case UPDATE_MULTIPLE_FILES:
        case UPDATE_ERROR:
            lv_label_set_text(btn_vtx, "#FF0000 Failed, check connection...#");
            lv_timer_handler();
            break;
        }
    } else if ((sel == 2) && !reboot_flag) {
        lv_obj_clear_flag(bar_goggle, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(btn_goggle, "WAIT... DO NOT POWER OFF... ");
        lv_timer_handler();

        const update_result_t ret = update_goggle(update_goggle_progress);

        lv_obj_add_flag(bar_goggle, LV_OBJ_FLAG_HIDDEN);
        lv_timer_handler();

        switch (ret) {
        case UPDATE_SUCCESS:
            reboot_flag = true;

            lv_label_set_text(btn_goggle, "#00FF00 Update success, repower goggle NOW!#");
            lv_timer_handler();

            beep();
            usleep(1000000);
            beep();
            usleep(1000000);
            beep();

            while (1)
                ; // dead loop

            break;

        case UPDATE_VERIFY_FAIL:
            lv_label_set_text(btn_goggle, "#FF0000 Verification failed, try it again#");
            lv_timer_handler();
            break;

        case UPDATE_NOT_FOUND:
            lv_label_set_text(btn_goggle, "#FFFF00 No firmware found.#");
            lv_timer_handler();
            break;

        case UPDATE_MULTIPLE_FILES:
            lv_label_set_text(btn_goggle, "#FFFF00 Multiple versions been found. Keep only one.#");
            lv_timer_handler();
            break;

        case UPDATE_ERROR:
            lv_label_set_text(btn_goggle, "#FF0000 FAILED#");
            lv_timer_handler();
            break;
        }
    } else if (sel == 3) { // flash ESP via SD
        lv_obj_clear_flag(bar_esp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label_esp, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(btn_esp, "Flashing...");
        lv_timer_handler();
        esp_loader_error_t ret = flash_elrs();
        lv_obj_add_flag(bar_esp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label_esp, LV_OBJ_FLAG_HIDDEN);
        if (ret == ESP_LOADER_SUCCESS)
            lv_label_set_text(btn_esp, "#00FF00 Success#");
        else
            lv_label_set_text(btn_esp, "#FF0000 FAILED#");
        page_version_enter();
    }
}

void update_current_version() {
    char strtmp[128];
    static bool bInit = true;
    if (bInit) {
        sys_version_t sys_version;
        generate_current_version(&sys_version);
        memset(strtmp, 0, sizeof(strtmp));
        strcat(strtmp, "Current Version: ");
        strcat(strtmp, sys_version.current);
        lv_label_set_text(cur_ver_label, strtmp);
        bInit = false;
    }
}

void version_update_title() {
    update_current_version();
    lv_label_set_text(btn_vtx, "Update VTX");
    if (!reboot_flag)
        lv_label_set_text(btn_goggle, "Update Goggle");
    lv_label_set_text(btn_esp, "Update ESP32");
}

void *thread_version(void *ptr) {
    for (;;) {
        /// Progess bar
        progress_bar_update();
        usleep(100000);
    }
    return NULL;
}

page_pack_t pp_version = {
    .p_arr = {
        .cur = 0,
        .max = 5,
    },

    .create = page_version_create,
    .enter = page_version_enter,
    .exit = NULL,
    .on_roller = page_version_on_roller,
    .on_click = page_version_on_click,
    .on_right_button = NULL,
};