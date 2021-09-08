

Soundplane application 1.8.4 release notes
October 21, 2019

To install this version, drag the "Soundplane" application to the Applications folder.

When you launch the Soundplane application, the menu at the top of the "Zones" page should contain three presets: chromatic, rows in fourths and rows in octaves, followed by a list of all the JSON files in the folder "~/Music/Madrona Labs/Soundplane/Zone Presets". To install the provided Zone examples, copy them to that location. 

The format of the zone .json files is human-readable and relatively self-explaining. Take a look to see how to make your own zone maps. 

The Soundplane Max/MSP examples require the CNMAT Max externals to run. These can be found at http://cnmat.berkeley.edu/downloads . 

The Soundplane application is open-source, available under a permissive license. For source code and more technical information see http://github.com/madronalabs.


changes:

1.8.5:

- improved note-on velocity consistency

1.8.4:

- fix a bug that could cause crashes on first launch when no Zone files were present

1.8.3:

- fixed issue with excessive CPU use / repainting since 1.8.0

1.8.0:

- cmake build rewritten for compatibility with new madronalib and soundplanelib.
- fixed graphics incompatibilities with OS X Mojave
- fixed controller Zone outputs and reduced redundant data output

1.7.0:

- fixed possible crash starting for the first time with Kyma connected
- "destination port" menu now allows connecting to other hosts on the local network
- added wait on startup to resolve ports on other hosts
- added clear all notes when switching OSC destinations and on shutdown
- added test pattern mode
- allow running app hidden / in background without added latency
- fix redundant calibrate on startup
- fix a stuck notes issue
- add quick recovery after unplug / replug instrument

1.6:

- improved isochronous USB driver:
	- lower latency
	- allows restart after pausing app
- fixed an issue selecting cleanest carriers after "select carriers"
- fixed possible crashes on startup/shutdown
- now distributing as signed .dmg

1.5:

- new touch tracker algorithm:
	- consumes much less CPU
 	- improved latency
	- improved pressure sensitivity
	- improved pressure uniformity
	- improved position accuracy
	- allows better tracking into corners
	- lengthy normalization step no longer required
	- fixed hanging touches
- fix Kyma connection
- improve selection of lowest-noise carrier set
- fix a possible crash when switching zone presets
- fixed a window-related crash on shutdown
- fixed latency issue when in background for an extended time

1.4:

- fixed a problem with MIDI output preventing slide between rows
- tweaked pre-touch filtering for lower noise
- (dev) new cmake-based build system 
- (dev) experimental Linux build
- (dev) added HelloSoundplane command line app for low-level testing

1.3:

- MPE MIDI support. Requires Aalto 1.7 / Kaivo 1.2. 
- Implemented note splits to multiple ports over OSC. 
- Improved stability and sensitivity of MIDI velocity
- changed "z max" control to more intuitive "z scale."
- fix crash with uninitialized driver on shutdown
- allow 1-500Hz MIDI data rate
- fix bug where pressure wasn't getting set w/o a MIDI connection
- send quantized pitch bend on MIDI note off
- fix touch tracker issues including zone-switch bug
- fix to allow MIDI note 0
- fix for double note off problem
- fixes for MIDI glissando

1.2.5:

- [1.2.5.1] Fixed a crash on startup if no preferences folder was present.
- Kyma listener off by default to fix collisions on port 3124. Use 'kyma' toggle on Expert page to turn on.
- fixed automatic connection to selected OSC service on startup.
- restored some values from 1.1.2 to improve touch tracking. 
- add automatic saving of window dimensions. This is saved in /Application Support/SoundplaneViewState.txt.
- fixed a problem resolving OSC services
- fixed wrong MIDI note offsets in default Zone setups

1.2.4:

- rendering fixes for Retina display
- make touches easier to get into top and bottom rows
- code signing application 
- turned Kyma polling over MIDI OFF by default
- fixed some state issues on startup that required reselecting zone to refresh
- clamp zone outputs to [0, 1] as documented
- fix touch rotate bug
- fix OSC browser
- made normalizing easier and mre accurate
- sending out x, y, and z from zones via MIDI.

1.1.2:

- more complete fix to the note-off problem
- restored the note lock feature for new zones.
- fixed an odd font-related bug

1.1:

- PLEASE NOTE: moved support files to ~/Music/Madrona Labs.
- fixed a problem where the wrong note value was sent on note-off
- fixed a potential crash sending MIDI if a MIDI device was not set
- made benign errors less alarming

1.0a2:

- two new kinds of zones: z (pressure only) and a toggle switch.
- fixed an error where inactive touches were continuously sending their data
- shortened some JSON zone names (just remove "controller_" to fix your presets)
- updated Max/MSP examples 
- moved matrix message into t3d OSC bundle with touches
- restored some debug printing in the in-app console
- added error info for JSON parsing
- fixed possible bug with zone parsing
- clarified the T3D format in docs
- fixed an error reading calibration files
- fix view issues for Retina display

1.0a1:
-new Zone features allow mapping notes and controllers to key grid
-changed t3d format for wider OSC compatibility
-OpenGL accelerated graphics
-fixed graphics for Retina display
-fixed a bug where SoundplaneController was initialized twice
-fixed OpenGL errors on quit
-fixed a possible crash in adjustPeak()
-fixed note release when quantized
-new raw matrix output


 

