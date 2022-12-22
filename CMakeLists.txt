cmake_minimum_required(VERSION 3.16)

project(HDZGOGGLE VERSION 1.1)

set(TRIPLE arm-buildroot-linux-musleabihf)
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
option(USE_SYSTEM_GLOG "use system glog" OFF)
if(USE_SYSTEM_GLOG)
	find_package(glog REQUIRED)
	add_library(GLOG_TARGET ALIAS glog::glog)
else(USE_SYSTEM_GLOG)
	set(BUILD_SHARED_LIBS OFF)
	set(WITH_GFLAGS OFF)
	set(WITH_GTEST OFF)
	set(WITH_PKGCONFIG OFF)
	set(WITH_UNWIND OFF)
	set(BUILD_TESTING OFF)
	add_subdirectory(lib/glog)
	add_library(GLOG_TARGET ALIAS glog)
endif(USE_SYSTEM_GLOG)

add_subdirectory(lib/log)
add_subdirectory(lib/lvgl)

option(USE_SYSTEM_ALSA_LIB "use system alsa-lib" OFF)
if(USE_SYSTEM_ALSA_LIB)
	find_package(ALSA REQUIRED)
	add_library(ALSA_LIB_TARGET ALIAS ALSA::ALSA)
else(USE_SYSTEM_ALSA_LIB)
	add_subdirectory(lib/alsa-lib)
	add_library(ALSA_LIB_TARGET ALIAS alsa-lib)
endif(USE_SYSTEM_ALSA_LIB)

option(USE_SYSTEM_ZLIB "use system zlib" OFF)
if(USE_SYSTEM_ZLIB)
	find_package(ZLIB REQUIRED)
	add_library(ZLIB_TARGET ALIAS ZLIB::ZLIB)
else(USE_SYSTEM_ZLIB)
	add_subdirectory(lib/zlib)
	add_library(ZLIB_TARGET ALIAS zlib)
endif(USE_SYSTEM_ZLIB)

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
	${STANDARD_LIBRARIES}

	lvgl
	log
	softwinner

	GLOG_TARGET
	ZLIB_TARGET
    ALSA_LIB_TARGET
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