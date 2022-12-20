include(ExternalProject)

ExternalProject_Add(
    zlib-project
    URL file://${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8.tar.gz
    URL_HASH MD5=44d667c142d7cda120332623eab69f40 
    DOWNLOAD_EXTRACT_TIMESTAMP true
    PATCH_COMMAND ""
    CONFIGURE_COMMAND
        env PATH=${TOOLS}/bin:$ENV{PATH} CROSS_PREFIX=${TRIPLE}-
        ./configure
        --static
        --prefix=/usr
    BUILD_IN_SOURCE true
    BUILD_COMMAND env PATH=${TOOLS}/bin:$ENV{PATH} make
    INSTALL_COMMAND env PATH=${TOOLS}/bin:$ENV{PATH} make install DESTDIR=<INSTALL_DIR>
    BUILD_BYPRODUCTS <INSTALL_DIR>/usr/include <INSTALL_DIR>/usr/lib/libz.a
)

ExternalProject_Get_property(zlib-project INSTALL_DIR)

file (MAKE_DIRECTORY ${INSTALL_DIR}/usr/include)

add_library(zlib STATIC IMPORTED GLOBAL)
add_dependencies(zlib zlib-project)
set_target_properties(zlib PROPERTIES
  IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/${INSTALL_DIR}/usr/lib/libz.a
  INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/usr/include
)