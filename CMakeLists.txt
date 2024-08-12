cmake_minimum_required(VERSION 3.20)

project(PycpLanguage)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(BUILD_SHARED_LIBS "Build shared Pycp library." ON)
set(CMAKE_INSTALL_PREFIX ${CMAKE_HOME_DIRECTORY}/build CACHE PATH "A directory which to install Pycp.")
set(CMAKE_BUILD_TYPE "Release" CACHE STRING)

set(XXHASH_INCLUDE_DIR "" CACHE PATH)
set(XXHASH_LIBRARY "" CACHE FILEPATH)

include_directories(${CMAKE_HOME_DIRECTORY}/include)

add_subdirectory(${CMAKE_HOME_DIRECTORY}/src)
