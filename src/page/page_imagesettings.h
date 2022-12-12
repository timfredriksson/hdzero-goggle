#ifndef _PAGE_IMAGESETTINGS_H
#define _PAGE_IMAGESETTINGS_H


 #include "lvgl.h"
#include "page_common.h"
lv_obj_t *page_imagesettings_create(lv_obj_t *parent, struct panel_arr *arr);

void page_ims_click();
void set_slider_value();
#endif
