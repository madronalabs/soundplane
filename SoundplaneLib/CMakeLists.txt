set(SP_DRIVER_SOURCES
  InertSoundplaneDriver.cpp
  InertSoundplaneDriver.h
  SoundplaneDriver.cpp
  SoundplaneDriver.h
  SoundplaneModelA.cpp
  SoundplaneModelA.h
  TestSoundplaneDriver.cpp
  TestSoundplaneDriver.h
  ThreadUtility.cpp
  ThreadUtility.h
  )

if(SP_LIBUSB_DIR OR NOT APPLE)
  set(SP_USE_LIBUSB yes)
endif()

if(SP_USE_LIBUSB)
  set(SP_DRIVER_SOURCES
    ${SP_DRIVER_SOURCES}
    LibusbSoundplaneDriver.cpp
    LibusbSoundplaneDriver.h
    )
else(SP_USE_LIBUSB)
  set(SP_DRIVER_SOURCES
    ${SP_DRIVER_SOURCES}
    MacSoundplaneDriver.cpp
    MacSoundplaneDriver.h
    )
endif()

add_library(soundplanelib ${SP_DRIVER_SOURCES})
target_link_libraries(soundplanelib madronalib)
target_include_directories(soundplanelib PUBLIC .)

if(SP_USE_LIBUSB)
  add_subdirectory(../external/libusb libusb)
  target_link_libraries(soundplanelib libusb)
else()
  target_link_libraries(soundplanelib "-framework IOKit")
endif()

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  target_link_libraries(soundplanelib atomic)
endif()
