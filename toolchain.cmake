set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TRIPLE arm-linux-musleabihf)
set(TOOLS ${PROJECT_SOURCE_DIR}/toolchain/${TRIPLE}-cross)

set(CMAKE_FIND_ROOT_PATH ${TOOLS})
set(CMAKE_SYSROOT ${TOOLS})

set(CMAKE_AR                        ${TOOLS}/bin/${TRIPLE}-ar)
set(CMAKE_ASM_COMPILER              ${TOOLS}/bin/${TRIPLE}-gcc)
set(CMAKE_C_COMPILER                ${TOOLS}/bin/${TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER              ${TOOLS}/bin/${TRIPLE}-g++)
set(CMAKE_LINKER                    ${TOOLS}/bin/${TRIPLE}-ld)
set(CMAKE_OBJCOPY                   ${TOOLS}/bin/${TRIPLE}-objcopy)
set(CMAKE_RANLIB                    ${TOOLS}/bin/${TRIPLE}-ranlib)
set(CMAKE_SIZE                      ${TOOLS}/bin/${TRIPLE}-size)
set(CMAKE_STRIP                     ${TOOLS}/bin/${TRIPLE}-strip)
