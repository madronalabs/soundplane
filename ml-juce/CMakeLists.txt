
# soundplane/ml-juce/CMakeLists.txt

#--------------------------------------------------------------------
# Add include directories
#--------------------------------------------------------------------


include_directories(deprecated)
include_directories(JuceApp)
include_directories(JuceLookAndFeel)


# add madronalib headers location. TODO use find_package for cross-platform support
include_directories(/usr/local/include/madronalib)


#--------------------------------------------------------------------
# Add files in subdirectories
#--------------------------------------------------------------------


file(GLOB DEPRECATED_SOURCES "deprecated/*.cpp")
file(GLOB JUCE_APP_SOURCES "JuceApp/*.cpp")
file(GLOB JUCE_LOOK_AND_FEEL_SOURCES "JuceLookAndFeel/*.cpp")

file(GLOB DEPRECATED_HDRS "deprecated/*.h")
file(GLOB JUCE_APP_HDRS "JuceApp/*.h")
file(GLOB JUCE_LOOK_AND_FEEL_HDRS "JuceLookAndFeel/*.h")

set(ML_JUCE_SOURCES 

	"AppConfig.h"
	${DEPRECATED_SOURCES} 
	${JUCE_APP_SOURCES} 
	${JUCE_LOOK_AND_FEEL_SOURCES}

	${DEPRECATED_HDRS} 
	${JUCE_APP_HDRS} 
	${JUCE_LOOK_AND_FEEL_HDRS}
	)


#--------------------------------------------------------------------
# create library
#--------------------------------------------------------------------

add_library(ml-juce STATIC ${ML_JUCE_SOURCES})
