cmake_minimum_required (VERSION 3.10)
project(DesktopBuild)

cmake_policy(SET CMP0077 NEW)
set(CMAKE_CXX_STANDARD 17)

add_definitions(-DPLATFORM_BUILD_DESKTOP)

add_library(vhm_domain INTERFACE)
target_include_directories(vhm_domain INTERFACE components/vhm_domain/include)

add_library(probe_protocol STATIC
    components/probe_protocol/frame_codec.cpp
    components/probe_protocol/messages.cpp
)
target_include_directories(probe_protocol PUBLIC components/probe_protocol/include)
target_link_libraries(probe_protocol PUBLIC vhm_domain)

add_library(probe_link STATIC components/probe_link/probe_client.cpp)
target_include_directories(probe_link PUBLIC components/probe_link/include)
target_link_libraries(probe_link PUBLIC probe_protocol vhm_domain)

# Mooncake 
set(MOONCAKE_BUILD_EXAMPLE OFF)
add_subdirectory(dependencies/mooncake)

# Mooncake log
set(MOONCAKE_EVENT_BUILD_EXAMPLE OFF)
add_subdirectory(dependencies/mooncake_log)

# Smooth ui shit
set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF)
add_subdirectory(dependencies/smooth_ui_toolkit)

# App layer
file(GLOB_RECURSE APP_LAYER_SRCS
    app/*.c
    app/*.cc
    app/*.cpp
)
set(APP_LAYER_INCS
    app/
)

# Lvgl
set(LV_CONF_INCLUDE_SIMPLE OFF)
set(LV_CONF_PATH ../../lv_conf.h)
add_subdirectory(dependencies/lvgl)

# SDL
find_package(SDL2 REQUIRED SDL2)
include_directories(PUBLIC ${SDL2_INCLUDE_DIRS})
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})

# App layer
file(GLOB_RECURSE APP_LAYER_SRCS
    app/*.c
    app/*.cc
    app/*.cpp
)
set(APP_LAYER_INCS
    app/
)

# 桌面端源文件
file(GLOB_RECURSE APP_DESKTOP_BUILD_SRCS
    platforms/desktop/*.cpp
    platforms/desktop/*.cc
    platforms/desktop/*.c
)
add_executable(app_desktop_build ${APP_DESKTOP_BUILD_SRCS} ${APP_LAYER_SRCS})
target_include_directories(app_desktop_build PUBLIC ${APP_LAYER_INCS})
target_link_libraries(app_desktop_build PUBLIC 
    mooncake 
    mooncake_log
    lvgl 
    lvgl_examples 
    lvgl_demos 
    ${SDL2_LIBRARIES}
    smooth_ui_toolkit
    probe_protocol
    probe_link
)

if(WIN32)
    # The SDL2 VC development package stores headers directly in include/,
    # while LVGL's SDL driver includes them as <SDL2/SDL.h>. The Windows
    # build helper creates this compatibility directory in the build tree.
    target_include_directories(app_desktop_build PRIVATE
        ${CMAKE_BINARY_DIR}/sdl2-headers
    )
endif()

if(UNIX)
    target_link_libraries(app_desktop_build PUBLIC pthread)
endif()

# 设置构建路径
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/build/desktop)

# 确保构建路径存在
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR})

# 设置生成的可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

# # 设置生成的库文件输出路径
# set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})
