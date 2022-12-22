cmake_minimum_required(VERSION 3.16)

project(HDZGOGGLE VERSION 1.1)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(OS_APP_PATH ${PROJECT_SOURCE_DIR}/mkapp/app/app)

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
	set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto")
	set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto")
endif()

add_definitions(
	-DAWCHIP=AW_V5
)

set(COMMON_COMPILE_OPTIONS
	-march=armv7-a
	-mtune=cortex-a7
	-mfpu=neon
	-mfloat-abi=hard
	-Wno-unused-function
	-Wno-unused-variable
	-ffunction-sections
	-fdata-sections 
	-Wl,-gc-sections
)

include(ProcessorCount)
ProcessorCount(CPU_COUNT)

# deps
set(BUILD_SHARED_LIBS OFF)
set(WITH_GFLAGS OFF)
set(WITH_GTEST OFF)
set(WITH_PKGCONFIG OFF)
set(WITH_UNWIND OFF)
set(BUILD_TESTING OFF)
add_subdirectory(lib/glog)

add_subdirectory(lib/log)
add_subdirectory(lib/lvgl)
add_subdirectory(lib/alsa-lib)
add_subdirectory(lib/zlib)
add_subdirectory(lib/softwinner)

# build settings
set(STANDARD_LIBRARIES
	c
	crypt
	dl
	m
	pthread
	rt
	stdc++
)

file(GLOB SRC_FILES_CORE   "src/core/*.c" "src/core/*.h")
file(GLOB SRC_FILES_DRIVER "src/driver/*.c" "src/driver/*.h")
file(GLOB SRC_FILES_IMAGE  "src/image/*.c" "src/image/*.h")
file(GLOB SRC_FILES_PAGE   "src/page/*.c" "src/page/*.h")
file(GLOB SRC_FILES_BMI   "src/bmi270/*.c" "src/bmi270/*.h")
file(GLOB SRC_FILES_WINDOW "src/window/*.c" "src/window/*.h")
file(GLOB SRC_FILES_MINIINI   "src/minIni/minIni.c" "src/minIni/*.h")
file(GLOB SRC_FILES_PLAYER "src/player/*.c" "src/player/*.h")

add_executable(${PROJECT_NAME}  
	${SRC_FILES_CORE} 
	${SRC_FILES_DRIVER} 
	${SRC_FILES_IMAGE} 
	${SRC_FILES_PAGE} 
	${SRC_FILES_BMI} 
	${SRC_FILES_WINDOW}
	${SRC_FILES_MINIINI}
	${SRC_FILES_PLAYER}
)
target_include_directories(${PROJECT_NAME} PRIVATE
	src
	src/core
	src/driver
	src/page
	src/player
)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE 
	lib/linux/include
)
target_link_directories(${PROJECT_NAME} PRIVATE 
	media/lib
)
target_link_libraries(${PROJECT_NAME}
	-static
	${STANDARD_LIBRARIES}
	lvgl
	glog::glog
	log
	softwinner
)
target_compile_options(${PROJECT_NAME} PRIVATE ${COMMON_COMPILE_OPTIONS})
target_link_options(${PROJECT_NAME} PRIVATE ${COMMON_COMPILE_OPTIONS})

file(GLOB RESOURCE_FILES 
	${PROJECT_SOURCE_DIR}/mkapp/app/app/resource/*
)

# ota target
add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/out/${PROJECT_NAME}
	COMMAND size -A ${PROJECT_NAME}
	COMMAND mkdir -p ${PROJECT_SOURCE_DIR}/out/
	COMMAND cp ${PROJECT_NAME} ${PROJECT_SOURCE_DIR}/out/
	COMMAND cp ${PROJECT_NAME} ${OS_APP_PATH}
	COMMAND cd ${PROJECT_SOURCE_DIR}/mkapp && rm -f ./ota_app/hdzgoggle_app_ota*.tar && ./mkapp_ota.sh && ./make_fw.sh
	COMMAND mv ${PROJECT_SOURCE_DIR}/mkapp/HDZERO_GOGGLE-*.bin ${PROJECT_SOURCE_DIR}/out/
	DEPENDS ${RESOURCE_FILES} ${PROJECT_NAME} 
)
add_custom_target(${PROJECT_NAME}-OTA ALL
  DEPENDS ${PROJECT_SOURCE_DIR}/out/${PROJECT_NAME}
)