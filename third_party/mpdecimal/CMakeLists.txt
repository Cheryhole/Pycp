cmake_minimum_required(VERSION 3.15)

project(mpdecimal C)

set(MPDEC_SOURCES
    libmpdec/basearith.c
    libmpdec/constants.c
    libmpdec/context.c
    libmpdec/convolute.c
    libmpdec/crt.c
    libmpdec/difradix2.c
    libmpdec/fnt.c
    libmpdec/fourstep.c
    libmpdec/io.c
    libmpdec/mpalloc.c
    libmpdec/mpdecimal.c
    libmpdec/mpsignal.c
    libmpdec/numbertheory.c
    libmpdec/sixstep.c
    libmpdec/transpose.c
)

add_library(mpdec STATIC ${MPDEC_SOURCES})

# 对外仅暴露 include/mpdecimal.h：libmpdec/ 目录内含 io.h 等与系统头同名的
# 文件，PUBLIC 泄漏会劫持消费方（linenoise、flex 生成词法器）的
# #include <io.h>，导致编译错误。libmpdec 目录仅作为编译源码的私有路径。
target_include_directories(mpdec
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libmpdec)

if(MSVC)
    target_compile_definitions(mpdec PRIVATE CONFIG_64 ANSI _CRT_SECURE_NO_WARNINGS)
    target_compile_options(mpdec PRIVATE /W3 /wd4200 /wd4204 /wd4221)
else()
    target_compile_definitions(mpdec PRIVATE CONFIG_64 ANSI)
endif()

set_property(TARGET mpdec PROPERTY POSITION_INDEPENDENT_CODE ON)
