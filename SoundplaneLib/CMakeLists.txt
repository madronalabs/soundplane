set(SP_DRIVER_SOURCES
  SoundplaneDriver.cpp
  SoundplaneDriver.h
  SoundplaneDriverData.cpp
  SoundplaneDriverData.h
  SoundplaneModelA.h
  )
add_library(soundplanelib ${SP_DRIVER_SOURCES})
target_link_libraries(soundplanelib portaudio)
target_link_libraries(soundplanelib "-framework IOKit")
target_include_directories(soundplanelib PUBLIC .)
