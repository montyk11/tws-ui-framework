cmake_minimum_required(VERSION 3.16)
project(simple_qt_app LANGUAGES C CXX)

# Qt automoc/auto ui/rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Help moc find headers in current dir
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer toolchain-provided sysroot if set (or from environment)
if(NOT DEFINED CMAKE_SYSROOT AND DEFINED ENV{SYSROOT})
  set(CMAKE_SYSROOT $ENV{SYSROOT})
endif()

if(DEFINED CMAKE_SYSROOT)
  message(STATUS "CMAKE_SYSROOT = ${CMAKE_SYSROOT}")
endif()

# -------------------------
# Project include dir (headers moved to "inc/")
# Add this early so Qt autogen can find headers.
# -------------------------
set(PROJECT_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/inc")
if(EXISTS "${PROJECT_INC_DIR}")
  message(STATUS "Adding project include dir: ${PROJECT_INC_DIR}")
  include_directories("${PROJECT_INC_DIR}")   # helps Qt autogen find headers
else()
  message(WARNING "Include dir '${PROJECT_INC_DIR}' does not exist — ensure headers are in 'inc/'.")
endif()

# -------------------------
# Qt host/target selection notes:
# - For cross-builds pass -DQt6_DIR=<target-sysroot Qt> and -DQT_HOST_PATH=<host Qt prefix>
# - Optionally pass -DQt6HostInfo_DIR to point to host Qt's Qt6HostInfo config dir
# -------------------------
if(NOT DEFINED Qt6_DIR AND DEFINED ENV{Qt6_DIR})
  set(Qt6_DIR $ENV{Qt6_DIR})
endif()

if(NOT DEFINED QT_HOST_PATH)
  set(QT_HOST_PATH "/usr" CACHE PATH "Prefix of host Qt installation (moc/uic location).")
endif()

if(NOT DEFINED QT_NO_PACKAGE_VERSION_CHECK)
  set(QT_NO_PACKAGE_VERSION_CHECK FALSE CACHE BOOL "Allow using Qt package even if version doesn't match exactly")
endif()

if(DEFINED Qt6HostInfo_DIR)
  # user-specified
  set(Qt6HostInfo_DIR "${Qt6HostInfo_DIR}")
else()
  # try a common host location (can be overridden at configure time)
  set(Qt6HostInfo_DIR "${QT_HOST_PATH}/lib/cmake/Qt6HostInfo" CACHE PATH "Qt6HostInfo dir for host Qt (optional).")
endif()

# Find Qt (this will use Qt6_DIR if provided via -DQt6_DIR=... at configure)
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# -------------------------
# Sources (ensure shim included)
# -------------------------
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/capturethread.cpp
    src/gpio_buttons_shim.c
    src/OverlayLabel.cpp
)

add_executable(simple_qt_app ${SOURCES})

# Per-target include paths (source dir + inc + sysroot includes)
target_include_directories(simple_qt_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${PROJECT_INC_DIR}
)

# If you put headers into 'inc/', add them explicitly to target sources so automoc sees them.
# This prevents Qt autogen from remembering old 'SRC:/src/...' header locations.
if(EXISTS "${PROJECT_INC_DIR}")
  file(GLOB_RECURSE PROJECT_HEADERS
       RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
       "${PROJECT_INC_DIR}/*.h" "${PROJECT_INC_DIR}/*.hpp")
  if(PROJECT_HEADERS)
    message(STATUS "Adding project headers to target for automoc: ${PROJECT_HEADERS}")
    # add as PRIVATE sources (doesn't compile; just informs automoc/autouic)
    target_sources(simple_qt_app PRIVATE ${PROJECT_HEADERS})
  endif()
endif()

if(DEFINED CMAKE_SYSROOT)
  # add sysroot includes so cross compile can resolve linux / system headers
  target_include_directories(simple_qt_app PRIVATE
    ${CMAKE_SYSROOT}/usr/include
    ${CMAKE_SYSROOT}/usr/local/include
  )
endif()

# Link Qt
target_link_libraries(simple_qt_app PRIVATE Qt6::Widgets)

# Threads
find_package(Threads REQUIRED)

# -------------------------
# Find and link libgpiod:
# prefer library inside sysroot (for cross build), fallback to host /usr
# -------------------------
set(_gpiod_names gpiod gpiod-1)

if(DEFINED CMAKE_SYSROOT)
  find_library(GPIOD_LIB NAMES ${_gpiod_names}
               PATHS ${CMAKE_SYSROOT}/usr/lib ${CMAKE_SYSROOT}/lib
               NO_DEFAULT_PATH)
  if(NOT GPIOD_LIB)
    find_library(GPIOD_LIB NAMES ${_gpiod_names}
                 PATHS ${CMAKE_SYSROOT}/usr/lib/* ${CMAKE_SYSROOT}/lib/* 
                 NO_DEFAULT_PATH)
  endif()
endif()

if(NOT GPIOD_LIB)
  # fallback to host system
  find_library(GPIOD_LIB NAMES ${_gpiod_names} PATHS /usr/lib /usr/local/lib)
endif()

if(GPIOD_LIB)
  message(STATUS "Found libgpiod: ${GPIOD_LIB}")
  target_link_libraries(simple_qt_app PRIVATE ${GPIOD_LIB})
else()
  message(WARNING "libgpiod not found. gpio_buttons_shim requires libgpiod to link successfully.")
endif()

# -------------------------
# Find and link libjpeg (for MJPEG decoding)
# -------------------------
find_package(JPEG)

if(JPEG_FOUND)
  message(STATUS "Found libjpeg: ${JPEG_LIBRARIES}")
  target_link_libraries(simple_qt_app PRIVATE ${JPEG_LIBRARIES})
  target_include_directories(simple_qt_app PRIVATE ${JPEG_INCLUDE_DIR})
else()
  message(WARNING "libjpeg not found via find_package — trying manual search.")

  # Manual fallback: look in sysroot or /usr
  if(DEFINED CMAKE_SYSROOT)
    find_library(JPEG_LIB NAMES jpeg turbojpeg
                 PATHS ${CMAKE_SYSROOT}/usr/lib ${CMAKE_SYSROOT}/lib
                 NO_DEFAULT_PATH)
  else()
    find_library(JPEG_LIB NAMES jpeg turbojpeg PATHS /usr/lib /usr/local/lib)
  endif()

  if(JPEG_LIB)
    message(STATUS "Found libjpeg manually: ${JPEG_LIB}")
    target_link_libraries(simple_qt_app PRIVATE ${JPEG_LIB})
  else()
    message(WARNING "⚠️ libjpeg not found — MJPEG decoding will fail to link!")
  endif()
endif()

# pthreads
target_link_libraries(simple_qt_app PRIVATE Threads::Threads)

# Install rule
install(TARGETS simple_qt_app DESTINATION bin)

