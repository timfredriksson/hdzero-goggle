file(GLOB_RECURSE SRC_FILES_LVGL "src/*.c")

add_library(lvgl ${SRC_FILES_LVGL})
target_compile_options(lvgl PRIVATE ${COMMON_COMPILE_OPTIONS})
target_include_directories(lvgl PUBLIC .)
target_include_directories(lvgl PRIVATE src)
target_link_options(lvgl PRIVATE ${COMMON_COMPILE_OPTIONS})