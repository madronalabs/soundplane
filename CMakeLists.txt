# soundplane/CMakeLists.txt
# root CMake file for soundplane project.
#

#--------------------------------------------------------------------
# set min version and deployment target -- before project
#--------------------------------------------------------------------

cmake_minimum_required(VERSION 2.8.12)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.10" CACHE STRING "Minimum OS X deployment version")

#--------------------------------------------------------------------
# project and version
#--------------------------------------------------------------------

project(soundplane)

set(PROJECT_NAME Soundplane)
set(SP_VERSION_MAJOR 1)
set(SP_VERSION_MINOR 8)
set(SP_VERSION_UPDATE 3)
set(SP_VERSION "${SP_VERSION_MAJOR}.${SP_VERSION_MINOR}.${SP_VERSION_UPDATE}")

if(APPLE)
  set(EXECUTABLE_NAME Soundplane)
else(APPLE)
  set(EXECUTABLE_NAME soundplane)
endif()

#--------------------------------------------------------------------
# Compiler flags
#--------------------------------------------------------------------

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3")

if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Release")
  add_definitions(
    -DDEBUG
  )
endif()

#--------------------------------------------------------------------
# Preprocessor macros
#--------------------------------------------------------------------

# enable JUCE compatibility for Timers
add_compile_definitions(MADRONALIB_TIMERS_USE_JUCE)

#--------------------------------------------------------------------
# Setup paths
#--------------------------------------------------------------------

# set Juce header path(required by external/juce)

set(ML_JUCE_HEADER_PATH "${CMAKE_SOURCE_DIR}/Source")

#--------------------------------------------------------------------
# Enforce out of source build
#--------------------------------------------------------------------

if(CMAKE_BINARY_DIR EQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR "Soundplane requires an out of source build")
endif()

#--------------------------------------------------------------------
# Add include directories
#--------------------------------------------------------------------

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/source)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ml-juce)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ml-juce/deprecated)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ml-juce/JuceApp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ml-juce/JuceLookAndFeel)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/juce)

# madronalib headers
# TODO Windows - use find_package?
include_directories(/usr/local/include/madronalib)

# soundplanelib headers
# TODO Windows - use find_package?
include_directories(/usr/local/include/soundplane)

# oscpack headers (TEMP)
# TODO Windows - use find_package?
include_directories(/usr/local/include/madronalib/oscpack)

include_directories(data/SoundplaneBinaryData)

#--------------------------------------------------------------------
# Add sources
#--------------------------------------------------------------------

file(GLOB SP_SOURCES "source/*.cpp")
file(GLOB SP_HEADERS "source/*.h")

list(APPEND SP_SOURCES
    "data/SoundplaneBinaryData/SoundplaneBinaryData.cpp"
    )

# add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/source)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/juce)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ml-juce)



#--------------------------------------------------------------------
# Add executable code signing and icon
#--------------------------------------------------------------------

set(ICON_FULL_PATH "Data/soundplane.icns")

set(PLIST_FULL_PATH "Data/Info.plist.in")
set(SP_DATA
  ${ICON_FULL_PATH}
  ${PLIST_FULL_PATH}
  )

set(SP_CODE_SIGN_IDENTITY "" CACHE STRING "Code sign identity")

#--------------------------------------------------------------------
# Add executable
#--------------------------------------------------------------------

add_executable(
  ${EXECUTABLE_NAME}
  MACOSX_BUNDLE
  ${SP_DATA}
  ${SP_SOURCES}
  ${SP_HEADERS})


target_include_directories(${EXECUTABLE_NAME} PRIVATE "${ML_JUCE_DIR}")

target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/SoundplaneLib/")

set_source_files_properties(${ICON_FULL_PATH} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
set_target_properties(${EXECUTABLE_NAME}
  PROPERTIES
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${SP_CODE_SIGN_IDENTITY}"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/${PLIST_FULL_PATH}"
  MACOSX_BUNDLE_ICON_FILE soundplane.icns
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.madronalabs.soundplane"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${SP_VERSION}"
  MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
  )



#--------------------------------------------------------------------
# Link with libraries
#--------------------------------------------------------------------

# madronalib
find_library(MADRONA_LIB madrona)
target_link_libraries("${EXECUTABLE_NAME}" "${MADRONA_LIB}")

#TEMP
message("madronalib:" ${MADRONA_LIB} )


# soundplanelib
find_library(SOUNDPLANE_LIB soundplane)
target_link_libraries("${EXECUTABLE_NAME}" "${SOUNDPLANE_LIB}")

# oscpack
 if(APPLE)
  find_library(OSCPACK_LIB oscpack)
  target_link_libraries("${EXECUTABLE_NAME}" "${OSCPACK_LIB}")
else()
endif()

# ml-juce adapters 
target_link_libraries("${EXECUTABLE_NAME}" "ml-juce")

# platform frameworks not included by ml-juce
if(APPLE)
  target_link_libraries("${EXECUTABLE_NAME}" "-framework IOKit")
  target_link_libraries("${EXECUTABLE_NAME}" "-framework GLUT")
else(APPLE)
  #target_link_libraries("${EXECUTABLE_NAME}" ${DNSSD_LIBRARIES})
endif()

# juce

target_link_libraries("${EXECUTABLE_NAME}" juce_audio_basics)
target_link_libraries("${EXECUTABLE_NAME}" juce_audio_devices)
target_link_libraries("${EXECUTABLE_NAME}" juce_core)
target_link_libraries("${EXECUTABLE_NAME}" juce_graphics)
target_link_libraries("${EXECUTABLE_NAME}" juce_gui_basics)
target_link_libraries("${EXECUTABLE_NAME}" juce_gui_extra)
target_link_libraries("${EXECUTABLE_NAME}" juce_opengl)


#--------------------------------------------------------------------
# Install
#--------------------------------------------------------------------


if(APPLE)
  install(TARGETS ${EXECUTABLE_NAME} DESTINATION ~/Applications)
elseif(WINDOWS)
  install(TARGETS ${EXECUTABLE_NAME} DESTINATION ????)
else()
  install(TARGETS ${EXECUTABLE_NAME} DESTINATION /usr/bin)
endif()


#--------------------------------------------------------------------
# Linux package generation
#--------------------------------------------------------------------

install(FILES Data/59-soundplane.rules DESTINATION /lib/udev/rules.d)
install(
  FILES Data/postinst
  PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  DESTINATION DEBIAN)

set(PACKAGE_NAME ${PROJECT_NAME})
set(PACKAGE_VERSION "${SP_VERSION}")
set(PACKAGE_RELEASE 0)
set(PACKAGE_MAINTAINER_NAME "Randy Jones")
set(PACKAGE_MAINTAINER_EMAIL "randy@madronalabs.com")
set(PACKAGE_DESCRIPTION "Client Software for the Madrona Labs Soundplane")
set(PACKAGE_DESCRIPTION_SUMMARY "Soundplane Client")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
  "libx11-6 (>= 1.6.2), libusb-1.0-0 (>= 1.0.17), libfreetype6 (>= 2.5.2), libxrandr2 (>= 1.4.2), libxinerama1 (>= 1.1.3), libxcursor1 (>= 1.1.14), libasound2 (>= 1.0.27), freeglut3 (>= 2.8.1), libavahi-compat-libdnssd1 (>= 0.6.31)")

include(CPack)

# Debian packages
include(cmake/DpkgDeb.cmake)
if(DPKG_FOUND)
  add_debian_package(${PROJECT_NAME})
endif()
