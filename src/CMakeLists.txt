cmake_minimum_required(VERSION 3.16)
project(cadMousePro LANGUAGES CXX C)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---------------------------------------------------------------------------
# HIDAPI：系统 pkg-config > CMake 包 > FetchContent（可选）
# ---------------------------------------------------------------------------
set(CADMOUSE_HIDAPI_TARGET "")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HIDAPI_HIDRAW QUIET IMPORTED_TARGET hidapi-hidraw)
    if(TARGET PkgConfig::HIDAPI_HIDRAW)
        set(CADMOUSE_HIDAPI_TARGET PkgConfig::HIDAPI_HIDRAW)
        message(STATUS "Using system HIDAPI via pkg-config: hidapi-hidraw")
    else()
        pkg_check_modules(HIDAPI_LIBUSB QUIET IMPORTED_TARGET hidapi-libusb)
        if(TARGET PkgConfig::HIDAPI_LIBUSB)
            set(CADMOUSE_HIDAPI_TARGET PkgConfig::HIDAPI_LIBUSB)
            message(STATUS "Using system HIDAPI via pkg-config: hidapi-libusb")
        else()
            pkg_check_modules(HIDAPI_GENERIC QUIET IMPORTED_TARGET hidapi)
            if(TARGET PkgConfig::HIDAPI_GENERIC)
                set(CADMOUSE_HIDAPI_TARGET PkgConfig::HIDAPI_GENERIC)
                message(STATUS "Using system HIDAPI via pkg-config: hidapi")
            endif()
        endif()
    endif()
endif()

if(NOT CADMOUSE_HIDAPI_TARGET)
    find_package(hidapi QUIET)
    if(TARGET hidapi::hidapi)
        set(CADMOUSE_HIDAPI_TARGET hidapi::hidapi)
        message(STATUS "Using system HIDAPI CMake target: hidapi::hidapi")
    elseif(TARGET hidapi::hidapi-hidraw)
        set(CADMOUSE_HIDAPI_TARGET hidapi::hidapi-hidraw)
        message(STATUS "Using system HIDAPI CMake target: hidapi::hidapi-hidraw")
    endif()
endif()

if(NOT CADMOUSE_HIDAPI_TARGET)
    option(CADMOUSE_FETCH_HIDAPI "Fetch hidapi from GitHub if not found on system" ON)
    if(CADMOUSE_FETCH_HIDAPI)
        find_path(_CADMOUSE_UDEV_INCLUDE NAMES libudev.h)
        find_library(_CADMOUSE_UDEV_LIB NAMES udev)
        if(NOT _CADMOUSE_UDEV_INCLUDE OR NOT _CADMOUSE_UDEV_LIB)
            message(FATAL_ERROR
                "HIDAPI not found. Install: sudo apt install libhidapi-dev\n"
                "Or install libudev-dev and allow FetchContent.")
        endif()
        include(FetchContent)
        message(STATUS "Fetching HIDAPI from git (libusb/hidapi hidapi-0.14.0)...")
        set(_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
        set(BUILD_SHARED_LIBS OFF)
        set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
        set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            hidapi
            GIT_REPOSITORY https://github.com/libusb/hidapi.git
            GIT_TAG hidapi-0.14.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(hidapi)
        set(BUILD_SHARED_LIBS "${_saved_build_shared_libs}")
        if(TARGET hidapi::hidapi)
            set(CADMOUSE_HIDAPI_TARGET hidapi::hidapi)
        elseif(TARGET hidapi::hidapi-hidraw)
            set(CADMOUSE_HIDAPI_TARGET hidapi::hidapi-hidraw)
        endif()
    endif()
endif()

if(NOT CADMOUSE_HIDAPI_TARGET)
    message(FATAL_ERROR "HIDAPI not found. Install: sudo apt install libhidapi-dev")
endif()

# ---------------------------------------------------------------------------
# 目标
# ---------------------------------------------------------------------------
find_package(Qt5 COMPONENTS Core REQUIRED)
find_package(Qt5 COMPONENTS Widgets Gui Concurrent REQUIRED)

add_library(cadmouse_common STATIC
    cadmousehid.cpp
    cadmousehid.h
    cadmouseconfig.cpp
    cadmouseconfig.h
    cadmouseservice.cpp
    cadmouseservice.h
    cadmouseprotocol.h
)
target_include_directories(cadmouse_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cadmouse_common PUBLIC
    Qt5::Core
    ${CADMOUSE_HIDAPI_TARGET}
)
if(UNIX AND NOT APPLE)
    find_library(UDEV_LIBRARY udev)
    if(UDEV_LIBRARY)
        target_link_libraries(cadmouse_common PUBLIC ${UDEV_LIBRARY})
    endif()
endif()

add_executable(cadmouse-helper
    cadmouse-helper.cpp
)
target_link_libraries(cadmouse-helper PRIVATE cadmouse_common)

add_executable(cadmouse-gui
    cadmouse-gui.cpp
    mousemainwindow.cpp
    mousemainwindow.h
    mousemainwindow.ui
    deviceImages.qrc
)
target_link_libraries(cadmouse-gui PRIVATE
    cadmouse_common
    Qt5::Widgets
    Qt5::Gui
    Qt5::Core
    Qt5::Concurrent
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cadmouse.conf"
    "${CMAKE_BINARY_DIR}/cadmouse.conf"
    COPYONLY
)
