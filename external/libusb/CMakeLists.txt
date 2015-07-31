include(findlibusb.cmake)

if(NOT LIBUSB_1_FOUND)
  message(FATAL_ERROR "Did not find libusb")
endif()
