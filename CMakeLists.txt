cmake_minimum_required(VERSION 3.2.1)
project(soundplane)

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

set(soundplane_sources
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

set(ML_JUCE_HEADER_PATH "${CMAKE_SOURCE_DIR}/Source")

if(NOT ML_MADRONALIB_DIR)
  set(ML_MADRONALIB_DIR "${CMAKE_SOURCE_DIR}/external/madronalib")
endif()

add_subdirectory(${ML_MADRONALIB_DIR})

add_executable(soundplane ${soundplane_sources})
target_link_libraries(soundplane madronalib)
target_link_libraries(soundplane "-framework IOKit")
target_include_directories(soundplane PRIVATE "${ML_JUCE_DIR}")
target_include_directories(soundplane PRIVATE SoundplaneLib)
target_include_directories(soundplane PRIVATE Data/SoundplaneBinaryData)
