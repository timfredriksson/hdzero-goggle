#pragma once

#include <stdbool.h>

#include "core/update.h"

bool mtd_update_rx(update_progress_cb_t progress, const char *filename);
bool mtd_update_fpga(update_progress_cb_t progress, const char *filename);
bool mtd_update_system(update_progress_cb_t progress, const char *filename);
bool mtd_update_app(update_progress_cb_t progress, const char *filename);

bool mtd_detect_vtx();
bool mtd_update_vtx(update_progress_cb_t progress, const char *filename);