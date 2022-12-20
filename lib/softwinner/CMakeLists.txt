file(GLOB_RECURSE SRC_FILES 
    src/algo/*.c src/algo/*.cpp
    src/audio/*.c src/audio/*.cpp
    src/cedarc/*.c src/cedarc/*.cpp
    src/cedarx/*.c src/cedarx/*.cpp
    src/component/*.c src/component/*.cpp
    src/demux/*.c src/demux/*.cpp
    src/fswriter/*.c src/fswriter/*.cpp
    src/iniparser/*.c src/iniparser/*.cpp
    src/ion/*.c src/ion/*.cpp
    src/isp/*.c src/isp/*.cpp
    src/media/*.c src/media/*.cpp
    src/muxer/*.c src/muxer/*.cpp
    src/plugin/*.c src/plugin/*.cpp
    src/render/*.c src/render/*.cpp
    src/system/*.c src/system/*.cpp
    src/textenc/*.c src/textenc/*.cpp
    src/utils/*.c src/utils/*.cpp
    src/videoin/*.c src/videoin/*.cpp
    
    # partial builds
    src/stream/base/*.c src/stream/base/*.cpp
    src/stream/file/*.c src/stream/file/*.cpp

    src/parser/base/*.c src/parser/base/*.cpp
    src/parser/mov/*.c src/parser/mov/*.cpp
    src/parser/ts/*.c src/parser/ts/*.cpp
)

add_library(softwinner OBJECT ${SRC_FILES})
target_compile_definitions(softwinner PRIVATE
    -D__OS_LINUX
    -DHAVE_AV_CONFIG_H
    -DCDXCFG_FILE_SYSTEM=1
    -DCDXCFG_HW_DISPLAY=1
    -DMPPCFG_VI=1
    -DMPPCFG_VO=1
    -DMPPCFG_TEXTENC=0
    -DMPPCFG_VENC=1
    -DMPPCFG_VDEC=1
    -DMPPCFG_MUXER
    -DMPPCFG_DEMUXER=1
)
target_compile_options(softwinner PRIVATE
    ${COMMON_COMPILE_OPTIONS}
    -Wno-multichar
)
target_include_directories(softwinner PUBLIC 
    include 
    include/audio
    include/audio/osal
    include/cedarc
    include/cedarx
    include/component
    include/config
    include/demux
    include/fswriter
    include/iniparser
    include/ion
    include/ise
    include/isp
    include/media
    include/media/utils
    include/muxer
    include/parser
    include/plugin
    include/render
    include/stream
    include/system
    include/textenc
    include/utils
    include/videoin
)
target_include_directories(softwinner PRIVATE
    src
    src/cedarc/base
    src/cedarc/vdecoder
    src/isp
    src/muxer/common
    src/muxer/mp4_muxer
    src/parser/base
    ../linux/include
    ../linux/include/media
)
add_dependencies(softwinner alsa-lib-project)
target_link_libraries(softwinner PUBLIC
    log
    zlib
    alsa-lib

    ${CMAKE_CURRENT_LIST_DIR}/lib/lib_eis.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/lib_ise_bi.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/lib_ise_mo.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/lib_ise_sti.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libvideoengine.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libvenc_codec.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libaacenc.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libawh264.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libawh265.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libawmjpegplus.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_ae.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_af.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_afs.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_awb.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_base.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_gtm.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_iso.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_md.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_pltm.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libisp_rolloff.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libmp3enc.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libVE.a
)

