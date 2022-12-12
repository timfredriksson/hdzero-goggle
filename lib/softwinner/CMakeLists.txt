file(GLOB_RECURSE SRC_FILES 
    src/algo/*.c
    src/audio/*.c
    src/cedarc/*.c
    src/cedarx/*.c
    src/component/*.c
    src/demux/*.c
    src/fswriter/*.c
    src/iniparser/*.c
    src/ion/*.c
    src/isp/*.c
    src/media/*.c
    src/muxer/*.c
    # src/parser/*.c
    src/plugin/*.c
    src/render/*.c
    # src/stream/*.c
    src/system/*.c
    src/textenc/*.c
    src/utils/*.c
    src/videoin/*.c
)

add_library(softwinner OBJECT ${SRC_FILES})
target_compile_definitions(softwinner PRIVATE
    -D__OS_LINUX
    -DHAVE_AV_CONFIG_H
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
    ../linux/include
    ../linux/include/media
)
add_dependencies(softwinner alsa-lib-project)
target_link_libraries(softwinner PUBLIC
    log
    alsa-lib

    ${CMAKE_CURRENT_LIST_DIR}/lib/lib_eis.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/lib_ise_bi.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/lib_ise_mo.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/lib_ise_sti.a
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
    ${CMAKE_CURRENT_LIST_DIR}/lib/libvenc_codec.a
    ${CMAKE_CURRENT_LIST_DIR}/lib/libvideoengine.a
)

