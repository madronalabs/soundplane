set(SP_DRIVER_SOURCES
  SoundplaneDriver.cpp
  SoundplaneDriver.h
  SoundplaneModelA.cpp
  SoundplaneModelA.h
  ThreadUtility.cpp
  ThreadUtility.h
  )

if(SP_USE_LIBUSB)
  set(SP_DRIVER_SOURCES
    ${SP_DRIVER_SOURCES}
    SoundplaneDriverLibusb.cpp
    SoundplaneDriverLibusb.h
    )
else(SP_USE_LIBUSB)
  set(SP_DRIVER_SOURCES
    ${SP_DRIVER_SOURCES}
    SoundplaneDriverMac.cpp
    SoundplaneDriverMac.h
    )
endif()

add_library(soundplanelib ${SP_DRIVER_SOURCES})
target_link_libraries(soundplanelib portaudio)
target_link_libraries(soundplanelib "-framework IOKit")
target_include_directories(soundplanelib PUBLIC .)

if(SP_USE_LIBUSB)
  include(findlibusb.cmake)
  if(NOT LIBUSB_1_FOUND)
    message(FATAL_ERROR "Did not find libusb")
  endif()
  add_definitions(${LIBUSB_1_DEFINITIONS})
  target_include_directories(soundplanelib PUBLIC ${LIBUSB_1_INCLUDE_DIRS})
  target_link_libraries(soundplanelib ${LIBUSB_1_LIBRARIES})
endif()
