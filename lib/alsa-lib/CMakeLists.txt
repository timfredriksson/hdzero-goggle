include(ExternalProject)

ExternalProject_Add(
    alsa-lib-project
    URL      ${CMAKE_CURRENT_LIST_DIR}/alsa-lib-1.1.4.1.tar.bz2
    URL_HASH MD5=29fa3e69122d3cf3e8f0e01a0cb1d183
    PATCH_COMMAND 
        patch -p1 < ${CMAKE_CURRENT_LIST_DIR}/patches/001_dmix_support_S16_LE_format.patch &&
        patch -p1 < ${CMAKE_CURRENT_LIST_DIR}/patches/002-add-muslabihf.patch &&
        patch -p1 < ${CMAKE_CURRENT_LIST_DIR}/patches/100-link_fix.patch &&
        patch -p1 < ${CMAKE_CURRENT_LIST_DIR}/patches/200-fix_include_file_redirect_warnings.patch &&
        patch -p1 < ${CMAKE_CURRENT_LIST_DIR}/patches/201-fix-snd_pcm_area_silence-remaining-samples-issue.patch &&
        patch -p1 < ${CMAKE_CURRENT_LIST_DIR}/patches/300-ensure-ringbuffer-empty-with-dmix.patch
    CONFIGURE_COMMAND 
        env PATH=${CMAKE_PROGRAM_PATH}:$ENV{PATH}/bin:$ENV{PATH}
        ./configure
        --host=arm-linux
        --target=arm-linux
        --prefix=/usr
        --enable-shared=no
        --enable-static=yes
        --disable-python
        --without-debug
        --with-versioned=no
    BUILD_COMMAND env PATH=${CMAKE_PROGRAM_PATH}:$ENV{PATH}/bin:$ENV{PATH} make -j${CPU_COUNT}
    INSTALL_COMMAND env PATH=${CMAKE_PROGRAM_PATH}:$ENV{PATH}/bin:$ENV{PATH} make install DESTDIR=<INSTALL_DIR>
    BUILD_BYPRODUCTS <INSTALL_DIR>/usr/include <INSTALL_DIR>/usr/lib/libasound.a
    DOWNLOAD_EXTRACT_TIMESTAMP 1
    BUILD_IN_SOURCE 1
)

ExternalProject_Get_property(alsa-lib-project INSTALL_DIR)

file (MAKE_DIRECTORY ${INSTALL_DIR}/usr/include)

add_library(alsa-lib STATIC IMPORTED GLOBAL)
add_dependencies(alsa-lib alsa-lib-project)
set_target_properties(alsa-lib PROPERTIES
  IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/${INSTALL_DIR}/usr/lib/libasound.a
  INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/usr/include
)