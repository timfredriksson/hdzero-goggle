#pragma once

#include <stdbool.h>

bool mtd_update_rx(const char *filename);
bool mtd_update_fpga(const char *filename);
bool mtd_update_app(const char *filename);

bool mtd_detect_vtx();
bool mtd_update_vtx(const char *filename);