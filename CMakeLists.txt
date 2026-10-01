cmake_minimum_required(VERSION 3.18)
project(zygisk-vrchat-test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(zygisk-vrchat-test SHARED src/main.cpp)

target_include_directories(zygisk-vrchat-test PRIVATE src)

target_link_libraries(zygisk-vrchat-test android log)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET zygisk-vrchat-test POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:zygisk-vrchat-test>
    )
endif()
