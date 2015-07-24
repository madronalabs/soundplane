cmake_minimum_required(VERSION 3.2.1)
project(soundplane)

set(SOUNDPLANE_VERSION 1.3)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if (CMAKE_BUILD_TYPE EQUAL "Release")
  add_definitions(
    -DNDEBUG
  )
else()
  add_definitions(
    -DDEBUG
  )
endif()

set(SOUNDPLANE_SOURCES
  Data/SoundplaneBinaryData/SoundplaneBinaryData.cpp
  Data/SoundplaneBinaryData/SoundplaneBinaryData.h
  SoundplaneLib/SoundplaneDriver.cpp
  SoundplaneLib/SoundplaneDriver.h
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
  Source/SoundplaneModelA.h
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
set(SOUNDPLANE_DATA
  ${ICON_FULL_PATH}
  ${PLIST_FULL_PATH}
  )

set(ML_JUCE_HEADER_PATH "${CMAKE_SOURCE_DIR}/Source")

if(NOT ML_MADRONALIB_DIR)
  set(ML_MADRONALIB_DIR "${CMAKE_SOURCE_DIR}/external/madronalib")
endif()

add_subdirectory(${ML_MADRONALIB_DIR})

add_executable(
  Soundplane
  MACOSX_BUNDLE
  ${SOUNDPLANE_DATA}
  ${SOUNDPLANE_SOURCES})
set_source_files_properties(${ICON_FULL_PATH} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
set_target_properties(Soundplane
  PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/${PLIST_FULL_PATH}"
  MACOSX_BUNDLE_ICON_FILE soundplane.icns
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.madronalabs.soundplane"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${SOUNDPLANE_VERSION}"
  MACOSX_BUNDLE_BUNDLE_NAME "Soundplane"
  )
target_link_libraries(Soundplane madronalib)
target_link_libraries(Soundplane "-framework IOKit")
target_include_directories(Soundplane PRIVATE "${ML_JUCE_DIR}")
target_include_directories(Soundplane PRIVATE SoundplaneLib)
target_include_directories(Soundplane PRIVATE Data/SoundplaneBinaryData)
