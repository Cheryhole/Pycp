cmake_minimum_required(VERSION 3.28)

project(PycpLanguage)

set(BUILD_SHARED_LIBS ON)
set(CMAKE_INSTALL_PREFIX ${CMAKE_HOME_DIRECTORY}/build)

# mpdecimal automatic building
if (BUILD_SHARED_LIBS)
	execute_process(COMMAND sh configure --enable-cxx "--prefix=${CMAKE_INSTALL_PREFIX}" --enable-shared --disable-static
		WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/3rdparty/mpdecimal)
else()
	execute_process(COMMAND sh configure --enable-cxx "--prefix=${CMAKE_INSTALL_PREFIX}" --disable-shared --enable-static
		WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/3rdparty/mpdecimal)
endif()
execute_process(COMMAND make -j4 "--makefile=${CMAKE_HOME_DIRECTORY}/3rdparty/mpdecimal/Makefile" && make install "--makefile=${CMAKE_HOME_DIRECTORY}/3rdparty/mpdecimal/Makefile"
	WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/3rdparty/mpdecimal)

#add_subdirectory(${CMAKE_HOME_DIRECTORY}/3rdparty/xxHash/cmake_unofficial)
#add_subdirectory(${CMAKE_HOME_DIRECTORY}/src)
