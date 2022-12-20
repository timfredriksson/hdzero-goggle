file(GLOB SRC_FILES "src/*.cpp" "include/*.h")

add_library(log ${SRC_FILES})
target_include_directories(log PUBLIC include)
target_compile_options(log PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_libraries(log
	-static
    glog::glog
)