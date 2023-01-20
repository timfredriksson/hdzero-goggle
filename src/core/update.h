#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UPDATE_SUCCESS,
    UPDATE_NOT_FOUND,
    UPDATE_MULTIPLE_FILES,
    UPDATE_VERIFY_FAIL, // unsued rn
    UPDATE_ERROR
} update_result_t;

typedef void (*update_progress_cb_t)(uint32_t);

// check if we need to finish a previous (legacy) system update
bool update_needs_finalizing();
update_result_t update_finalize(update_progress_cb_t progress);

update_result_t update_goggle(update_progress_cb_t progress);
update_result_t update_vtx(update_progress_cb_t progress);
