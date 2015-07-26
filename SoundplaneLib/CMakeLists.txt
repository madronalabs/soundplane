set(SP_DRIVER_SOURCES
  SoundplaneDriver.cpp
  SoundplaneDriver.h
  SoundplaneDriverMac.h
  SoundplaneDriverMac.cpp
  SoundplaneModelA.cpp
  SoundplaneModelA.h
  ThreadUtility.cpp
  ThreadUtility.h
  )
add_library(soundplanelib ${SP_DRIVER_SOURCES})
target_link_libraries(soundplanelib portaudio)
target_link_libraries(soundplanelib "-framework IOKit")
target_include_directories(soundplanelib PUBLIC .)
