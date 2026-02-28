cmake_minimum_required(VERSION 3.15)
project(PycpRuntime VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# 查找Python和pybind11
find_package(Python REQUIRED COMPONENTS Interpreter Development)
set(pybind11_DIR "C:/Program Files/Python314/Lib/site-packages/pybind11/share/cmake/pybind11")
find_package(pybind11 CONFIG REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include ${Python_INCLUDE_DIRS})

# -------------------------
# MinGW静态链接设置（使库不依赖MinGW运行时）
# -------------------------
if(MINGW)
    # 静态链接MinGW运行时库
    set(MINGW_STATIC_LINK_OPTIONS 
        -static
    )
		# -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
endif()

# -------------------------
# PyBind11 模块 (.pyd) - 直接编译所有源文件
# -------------------------
# 收集所有需要编译的源文件（除了PycpExporter.cpp）
set(PYCP_RUNTIME_SOURCES
    ${CMAKE_SOURCE_DIR}/src/PycpInteger.cpp
    ${CMAKE_SOURCE_DIR}/src/PycpObject.cpp
    ${CMAKE_SOURCE_DIR}/src/PycpString.cpp
)

# 创建pybind11模块，包含所有源文件
pybind11_add_module(PycpRuntime_pyd 
    ${CMAKE_SOURCE_DIR}/src/PycpExporter.cpp
    ${PYCP_RUNTIME_SOURCES}
)

set_target_properties(PycpRuntime_pyd PROPERTIES
    PREFIX ""
    SUFFIX ".pyd"
    OUTPUT_NAME "PycpRuntime4Python"
)

if(WIN32)
    target_compile_definitions(PycpRuntime_pyd PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
    target_compile_options(PycpRuntime_pyd PRIVATE /W4 /EHsc)
else()
    target_compile_options(PycpRuntime_pyd PRIVATE -Wall -Wextra -Wpedantic)
    
    # 为pyd模块添加MinGW静态链接选项
    if(MINGW)
        target_link_options(PycpRuntime_pyd PRIVATE ${MINGW_STATIC_LINK_OPTIONS})
    endif()
endif()

target_link_libraries(PycpRuntime_pyd PRIVATE 
    pybind11::headers 
    ${Python_LIBRARIES}
)

# -------------------------
# 核心库 - 动态版本（PycpRuntime.dll）单独编译
# -------------------------
add_library(PycpRuntime_dynamic SHARED ${PYCP_RUNTIME_SOURCES})

set_target_properties(PycpRuntime_dynamic PROPERTIES
    PREFIX ""
    OUTPUT_NAME "PycpRuntime"
)

if(WIN32)
    target_compile_definitions(PycpRuntime_dynamic PRIVATE _CRT_SECURE_NO_WARNINGS PYCP_EXPORTS)
endif()

if(MSVC)
    target_compile_options(PycpRuntime_dynamic PRIVATE /W4 /EHsc)
else()
    target_compile_options(PycpRuntime_dynamic PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(PycpRuntime_dynamic PRIVATE ${Python_LIBRARIES})

# 为动态库添加MinGW静态链接选项
if(MINGW)
    target_link_options(PycpRuntime_dynamic PRIVATE ${MINGW_STATIC_LINK_OPTIONS})
endif()

# -------------------------
# 核心库 - 静态版本（libPycpRuntime.a）单独编译
# -------------------------
add_library(PycpRuntime_static STATIC ${PYCP_RUNTIME_SOURCES})

set_target_properties(PycpRuntime_static PROPERTIES
    OUTPUT_NAME "PycpRuntime"
)

if(WIN32)
    target_compile_definitions(PycpRuntime_static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
    target_compile_options(PycpRuntime_static PRIVATE /W4 /EHsc)
else()
    target_compile_options(PycpRuntime_static PRIVATE -Wall -Wextra -Wpedantic)
    
    if(MINGW)
        target_compile_options(PycpRuntime_static PRIVATE 
            -static-libgcc 
            -static-libstdc++
        )
    endif()
endif()

target_link_libraries(PycpRuntime_static PRIVATE ${Python_LIBRARIES})

# -------------------------
# 测试可执行文件
# -------------------------
add_executable(test_pycp ${CMAKE_SOURCE_DIR}/src/test.cpp)

target_include_directories(test_pycp PRIVATE ${CMAKE_SOURCE_DIR}/include ${Python_INCLUDE_DIRS})

# 测试程序可以链接动态库或静态库
target_link_libraries(test_pycp PRIVATE PycpRuntime_dynamic ${Python_LIBRARIES})

if(MSVC)
    target_compile_options(test_pycp PRIVATE /W4 /EHsc)
    set_target_properties(test_pycp PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "PATH=${CMAKE_SOURCE_DIR}/bin;%PATH%"
    )
else()
    target_compile_options(test_pycp PRIVATE -Wall -Wextra -Wpedantic)
    
    if(MINGW)
        target_link_options(test_pycp PRIVATE ${MINGW_STATIC_LINK_OPTIONS})
    endif()
endif()

# -------------------------
# 安装/打包规则
# -------------------------

#add_custom_target(dist ALL
#    COMMAND ${CMAKE_COMMAND} -E echo "Creating distribution package..."
#    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist
#    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bin/PycpRuntime.dll ${CMAKE_SOURCE_DIR}/dist/
#    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bin/libPycpRuntime.a ${CMAKE_SOURCE_DIR}/dist/
#    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bin/PycpRuntime4Python.pyd ${CMAKE_SOURCE_DIR}/dist/
#    COMMAND ${CMAKE_COMMAND} -E echo "Distribution files copied to ./dist/"
#)

# -------------------------
# 打印信息
# -------------------------
message(STATUS "========================================")
message(STATUS "Python version: ${Python_VERSION}")
message(STATUS "Python executable: ${Python_EXECUTABLE}")
message(STATUS "pybind11 version: ${pybind11_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
if(MINGW)
    message(STATUS "MinGW static linking: Enabled")
endif()
message(STATUS "========================================")
message(STATUS "Build targets:")
message(STATUS "  - PycpRuntime_static  -> libPycpRuntime.a (static library)")
message(STATUS "  - PycpRuntime_dynamic -> PycpRuntime.dll (dynamic library)")
message(STATUS "  - PycpRuntime_pyd     -> PycpRuntime4Python.pyd (独立Python模块，包含所有实现)")
message(STATUS "  - test_pycp           -> test_pycp.exe (测试程序，链接动态库)")
message(STATUS "========================================")
message(STATUS "Output directory: ${CMAKE_SOURCE_DIR}/bin")
message(STATUS "========================================")