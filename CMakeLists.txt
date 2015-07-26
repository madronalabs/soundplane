cmake_minimum_required(VERSION 3.2.1)
project(soundplane)

set(SP_VERSION 1.3)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Release")
  add_definitions(
    -DDEBUG
  )
endif()

set(ML_JUCE_HEADER_PATH "${CMAKE_SOURCE_DIR}/Source")
if(NOT SP_MADRONALIB_DIR)
  set(SP_MADRONALIB_DIR "${CMAKE_SOURCE_DIR}/external/madronalib")
endif()

add_subdirectory(${SP_MADRONALIB_DIR})
add_subdirectory(SoundplaneLib)

set(SP_SOURCES
  Data/SoundplaneBinaryData/SoundplaneBinaryData.cpp
  Data/SoundplaneBinaryData/SoundplaneBinaryData.h
  Source/AppConfig.h
  Source/Filters2D.cpp
  Source/Filters2D.h
  Source/JuceHeader.h
  Source/MLProjectInfo.h
  Source/SoundplaneApp.cpp
  Source/SoundplaneApp.h
  Source/SoundplaneController.cpp
  Source/SoundplaneController.h
  Source/SoundplaneDataListener.h
  Source/SoundplaneGridView.cpp
  Source/SoundplaneGridView.h
  Source/SoundplaneMIDIOutput.cpp
  Source/SoundplaneMIDIOutput.h
  Source/SoundplaneModel.cpp
  Source/SoundplaneModel.h
  Source/SoundplaneOSCOutput.cpp
  Source/SoundplaneOSCOutput.h
  Source/SoundplaneTouchGraphView.cpp
  Source/SoundplaneTouchGraphView.h
  Source/SoundplaneView.cpp
  Source/SoundplaneView.h
  Source/SoundplaneZoneView.cpp
  Source/SoundplaneZoneView.h
  Source/TouchTracker.cpp
  Source/TouchTracker.h
  Source/TrackerCalibrateView.cpp
  Source/TrackerCalibrateView.h
  Source/Zone.cpp
  Source/Zone.h
)

set(ICON_FULL_PATH "Data/soundplane.icns")
set(PLIST_FULL_PATH "Data/Info.plist.in")
set(SP_DATA
  ${ICON_FULL_PATH}
  ${PLIST_FULL_PATH}
  )

set(SP_CODE_SIGN_IDENTITY "" CACHE STRING "Code sign identity")

add_executable(
  Soundplane
  MACOSX_BUNDLE
  ${SP_DATA}
  ${SP_SOURCES})
set_source_files_properties(${ICON_FULL_PATH} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
set_target_properties(Soundplane
  PROPERTIES
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${SP_CODE_SIGN_IDENTITY}"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/${PLIST_FULL_PATH}"
  MACOSX_BUNDLE_ICON_FILE soundplane.icns
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.madronalabs.soundplane"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${SP_VERSION}"
  MACOSX_BUNDLE_BUNDLE_NAME "Soundplane"
  )
target_link_libraries(Soundplane madronalib)
target_link_libraries(Soundplane soundplanelib)
target_include_directories(Soundplane PRIVATE "${ML_JUCE_DIR}")
target_include_directories(Soundplane PRIVATE Data/SoundplaneBinaryData)

add_subdirectory("Developer extras")
