cmake_minimum_required(VERSION 3.28)

project(PycpLanguage)

set(BUILD_SHARED_LIBS ON)
set(CMAKE_INSTALL_PREFIX ${CMAKE_HOME_DIRECTORY}/build)

add_subdirectory(${CMAKE_HOME_DIRECTORY}/3rdparty/xxHash/cmake_unofficial)
add_subdirectory(${CMAKE_HOME_DIRECTORY}/src)
