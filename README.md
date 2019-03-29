# Soundplane

Client application for the Madrona Labs Soundplane.

Copyright (c) 2019 Madrona Labs LLC. http://www.madronalabs.com

Distributed under the MIT license: http://madrona-labs.mit-license.org/

## Building

### OS X

To compile this project, the Xcode Command Line Tools and CMake are required.
Xcode Command Line Tools can be downloaded and installed from the "Downloads"
tab in Xcode's settings. An easy way of installing CMake is to do it via
Homebrew: With Homebrew installed, type `brew install cmake` in a terminal.

The madronalib and soundplanelib projects at (https://github.com/madronalabs/) are also required. Follow the directions in these projects to build them and to install the headers and libraries to /usr/local.

In order to prepare the build, the following commands can be used.

    $ mkdir build
    $ cd build
    $ cmake -GXcode ..


At this point, cmake should have created an Xcode project `soundplane.xcodeproj` ready for compiling in `build/`. You can change to the build directory and open it in the XCode app, or if you just want to build the application and run it from the comfort of the terminal, these commands can be used

    $ xcodebuild -project soundplane.xcodeproj -target Soundplane -configuration RelWithDebInfo
    $ open ./RelWithDebInfo/Soundplane.app

### Linux

note: untested. CMake files may need work. 

On Linux, the Soundplane application requires a few packages to be installed in
order to be built. GCC is required, but that is bundled with most Linux
distributions. On Ubuntu, the following command installs the dependencies:

    $ sudo apt-get install cmake libx11-dev libusb-1.0.0-dev libfreetype6-dev \
          libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev \
          libasound2-dev freeglut3-dev libavahi-compat-libdnssd-dev

In order to fetch source dependencies and build, the following commands can be
used (in a terminal)

    $ mkdir build
    $ cd build
    $ cmake ..
    $ make

On Linux, the Soundplane application typically uses udev to access Soundplane
via USB. On most stock Linux installations, the default configuration is to block
non-superuser access to the Soundplane. In order to allow the Soundplane
application to access the device, add a udev rule that allows it:

    $ sudo cp Data/59-soundplane.rules /etc/udev/rules.d/
    $ sudo udevadm control --reload-rules

The app can now be run with

    $ ./soundplane

If desired, it is possible to build a Debian package with the command

    $ make Soundplane_deb
