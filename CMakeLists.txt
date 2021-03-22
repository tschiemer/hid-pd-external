cmake_minimum_required(VERSION 3.17)
project(hid_pd_external C)

set(CMAKE_C_STANDARD 99)

if (APPLE)
    set(MAKEFILE "Makefile.macos")
elseif(UNIX)
    set(MAKEFILE "Makefile.unix")
else()
    message(ERROR system not supported)
endif()

add_custom_target(hid
        COMMAND make -f ${MAKEFILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_target(install-externals
        DEPENDS hid
        COMMAND make -f ${MAKEFILE} install
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})


add_custom_target(clean-externals
        COMMAND make -f ${MAKEFILE} clean
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})