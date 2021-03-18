cmake_minimum_required(VERSION 3.17)
project(hid_pd_external C)

set(CMAKE_C_STANDARD 99)


add_custom_target(hid
        COMMAND make
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_target(install-externals
        DEPENDS hid
        COMMAND make install
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})


add_custom_target(clean-externals
        COMMAND make clean
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})