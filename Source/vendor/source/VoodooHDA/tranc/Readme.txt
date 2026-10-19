0.2.6 release note
The original driver hdac.c from FreeBSD is changed so we applied its correction to our driver
* added support for new chipsets and new codecs
* added HDMI support
* added input monitor support
* Multichannel? It is already present because of Apple's audio system. You must choose between autodetect and multichannel. Either one or the other.
Compared to 0.2.5x driver in this version some bugs 

VoodooHDA 0.2.52 release notes
It made by Slice&AutumnRain from Russia based on voodoohda 0.2.2 while original authors dropped the project as is
with numerous mistakes.
The driver tested worldwide in very different hardware. Almost always it works.
The driver was completely revised and additional functionalities were added
* added NotesToPatch into info.plist so the user can manually correct and tune the audio configuration
* no more predefined quirks. In previous version there are incorrect quirks.
* full autodetect of devices in any combinations of outputs and inputs (in 0.2.2 there is only
	 speaker/headphone autodetect)
* device names is changed on the fly (only in 10.5.x, but not in 10.6.x - question to Apple)
* works with internal microphone as never before
* correct sequence of initializations and starts. No more kernel panics.
* more advanced names in Sound Preferencies to differ devices. "Line-out (Black Front)"
* resolved sleep issue didn't mentioned below
* getExtDump added to provide amplifiers control during work
* advanced mixer controls

voodoohda 0.2.2 release notes

note: this driver has been tested on only a few systems, so bugs and glitches should be expected.
      there is currently no official binary release, so while you are free to compile this source or
      use any derivative releases thereof, this driver should be considered experimental.

development
-----------

  * sources are hosted at the voodoohda project site at google code: http://voodoohda.googlecode.com/
  * code guidelines: four-space tab (not spaces), ~110 line width, k&r style
  * the provided "helper.sh" utility can be used to clean or build the project, create a compiled
    release package, or load and unload the driver for testing
  * the latest cvs revision of the freebsd hdac driver can be found here:
    http://www.freebsd.org/cgi/cvsweb.cgi/src/sys/dev/sound/pci/hda/
    (the revision corresponding to voodoohda sources is identified by HDAC_REVISION)
  * one of the main focuses in porting from hdac was to keep the structure and functionality of the
    code (especially the widget parser) relatively close to that of the original code, in order to
    ease integrating changes from upstream; that said, the code has been heavily reworked in some
    places due to porting necessity as well as clarification

hints
-----

  * use the provided 'getdump' utility to get a codec dump for debugging or to see how each logical
    pcm device is configured, see pcmAttach notices (same notion as pcmN with freebsd hdac)
  * one pcm device at a time can be active (unless aggregate devices are used), each is designated
    by "Analog/Digital PCM #N" in sound preference pane - separate selectors for "Master", "PCM",
    etc. are not different devices but correspond to standard oss controls
  * to change sample rate or bit depth or setup aggregate devices use Audio Midi Setup.app

known issues
------------

  * bad things (tm) will happen if some other hda driver is loaded or present in the catalogue
    when voodoohda is loaded
  * extremely cluttered and not particularly user-friendly audio controls in sound preference pane,
    only way around this is to dumb it down or implement a hal plugin (as applehda does)
  * distinction between oss controls and actual "ports" corresponding to pcm device play/rec channels
    is unclear - need to find out how to get audio port name (in "type" column) displayed
  * need to kextunload two or three times to actually unload the driver, seems to be an audio family
    bug as this happens with sample audio drivers too
  * manual specification of quirks and hints is currently unsupported
  * When sample rate and bit-width are changed for VoodooHDA using "Audio Midi Setup", the settings
    are not persisted to disk by coreaudiod in /Library/Preferences/Audio/com.apple.audio.DeviceSettings.plist.
    As a result, the settings are reset to default on next boot.  Cause is unknown.

license
-------

see license.txt for details and copyright notices

changelog
---------
v2.9.2, r154
- fix compilation error in private_xmmintrin.h

v2.9.1, r148
- Eliminate use of Xcode's xmmintrin

v2.9.0d10, r140
- sync from tranc@139
- Fix issues involving AudioControls
  - audioCtlParse should be done early so amps on disabled widgets, connections or pin directions are muted.
  - in audioCtlParse, reverse ndir only for pin.
  - in audioCtlParse, mute amps disabled by vendorPatchParse prior to creating ctls.
  - in audioDisableUseless, mute amps for disabled connections from non-disabled pins.
  - in audioDisableUnassociated, exclude audio-input as well as pin
    (they both have one input amp for multiple inputs.)
  - in audioDisableCrossAssociations, disable connection from input monitor to output
    pins other than the first (Green) pin.  Don't need to hear input monitor on all
    channels of multichannel analog output, because input monitor is stereo.
  - in dumpNodes, extDumpNodes, don't dump multiple input amps for widget that uses same one for multiple inputs.

v2.9.0d8, r138
- sync from tranc@137

v2.9.0d7, r135
- Version -> 2.9.0
- Fix bug in DMA setup when sample-frame size is not a power of 2 (for 6-channel engine.)
- Fix bug in HDMI multichannel ASP->codec channel mapping.

v2.8.10d6, r133
- Support multichannel on HDMI or DP.
- Fix minor bug in stripe code.

v2.8.10d5, r132
- Support striping to maximize available output channel bandwidth.
- Set InputSampleLatency, TerminalType on IOAudioEngine.
- Use HDMI PortType for HDMI/DP instead of SPDIF.

v2.8.10d4, r130
- Publish default channel layout for CoreAudio.
- Set ClockIsStable property.

v2.8.10d4, r129
- Fix max number of formats (can be overrun by analog multichannel association.)
- Fix channel->pin mapping for analog multichannel association (pins are arranged
  in sequence order same as expected channel order.)
- Turn off copy protection for digital audio output.
- For S/PDIF-encapsulated AC3, use stereo, and limit rates to 32KHz, 44.1KHz, 48KHz.
- Do not support 32-bit depth for digital channel as it is used for software-formatted S/PDIF.
- Allow 20-bit depth if it is available and 24-bit depth is unavailable.

v2.8.10d3, r127
- More models, update quirks.

v2.8.10d3, r126
- Bugfixes in Parser.cpp.

v2.8.10d3, r123
- Review AppleAudioClip.cpp
    Fix bug in stereo mixer.
    Fix bug in handling float format.
    Implement masking for 20, 24 bit depths with 32 bit width.

v2.8.10d2, r120
- Fixed handling of hpredir that prevented analog multichannel from working (Parser.cpp, VoodooHDADevice.cpp).
- Upgraded project to Xcode 8.3, and bumped version to 2.8.10d2.
- Assign analog 4/5.1/7.1 multichannel engines pretty names (VoodooHDAEngine.cpp).
- Lower default bit depth to 24 for engines that support both 24 and 32 (VoodooHDAEngine.cpp).
- Add mute control for digital outputs that support them (VoodooHDAEngine.cpp).
- Discard obsolete code from VoodooHDAEngine, VoodooHDAUserClient.
- Make non-virtual member functions in VoodooHDAEngine, VoodooHDAUserClient hidden.

v2.8.10d1, r119
- Revert some of rev 107.

v2.8.10d1, r118
- Fix setting of 20, 24, 32 bit widths.

v2.8.10d1, r117
- Update for XCode 8.2.1, Add some models.

v2.8.10d1, r116
- Merge all changes tranc@106 through tranc@115 to branches/zdev.

v2.8.8d1, r102
- Merge all changes tranc@95 through tranc@101 to branches/zdev.

v2.8.3d2, r91
- Use unwired memory in clientMemoryForType.

v2.8.2d6, r87
- Remove calls to IOConnectUnmapMemory

v2.8.2d6, r86
- Fix MSI interrupt concurrency.

v2.8.2d5, r84
- Implement no-snoop for ATI & Nvidia (code from FreeBSD).
- Implemented Selector Audio Control to set correct "Type" in "Sound" Preference Pane.

v2.8.2d4, r82
- Simplified DmaMemory.
- Moved interrupt timestamp closer to interrupt.
- Fixed some locking errors.
- Added 64bit quirk from FreeBSD.
- removed unneeded
  VoodooHDADevice::deactivateAllAudioEngines
  clearSampleBuffer from r80
  IOSleep(50) in VoodooHDAEngine::initHardware.
- hide static methods.
- update HDAC_REVISION.

v2.8.2d2, r81
- Make AllowMSI false for Nvidia if not in Info.plist. (+applied to tranc)
- Reverted loose open/close policy from r78. (+applied to tranc)
- Removed IOMatchCategory in Info.plist - so it be same as AppleHDA.kext.

v2.8.2d1, r80
- create development branch
- Update from FreeBSD:
  HDA models, codec models, misc macros
  code in VoodooHDADevice::widgetGetCaps, ::widgetParse, ::initRirb
- Memory caching policy as follows
  If InhibitCache=true, all memory access is uncacheable.
  If InhibitCache=false,
    For PCI bar, CORB, RIRB, DMAPos buffer - uncacheable.
    For BDL buffer - write-through.
    For sample input and output buffers - write-back.
- reports audio device type as "built-in" instead of "other" (same as Apple's audio drivers.)
- In VoodooHDAEngine
  eliminate unneeded mDescription.
  eliminated unneeded duplicate registration of non-mixable format.
  default sample rate for for audio streams no greater than 48KHz.
  Added necessary timestamp at audio-engine-start whose lack was causing delay at audio start.
  in performFormatChange - remove incorrect calls to base class,
    determine sample rate if not given,
    clear sample+mix buffer for new format.
  in createAudioControls - fix memory leak,
    give controls unique id,
    remove unusable gain controls.
    disable selector control for now because it's not correctly initialised with name and type of pins (TODO).
- Add a bit more detail to dump in Parser.cpp

v2.8.1, r78
- add mixerResume to restore mixer settings to pre-sleep values on wake.
- eliminate incorrect calls to base class in VoodooHDADevice::suspend and ::resume.
- enable PCI power management.
- sanitised use of mPciNub.
- Make so InhibitCache=false actually enables caching on DMA buffers.  PCI bar still uncacheable.

v2.8.0, r77
- Fix prefPane xib file to eliminate duplicate NSPreferencePane object.  All connections are on File's owner now.
- Eliminate useless methods in VoodooHDAUserClient.
- Update mPrefPanelMemoryBuf right before send to prefPane so it's always right.
- Fix prefPane to update if window closed back to System Preferences and then reopened.
- Vectorize, AllowMSI default to true if don't appear in Info.plist.
- prefPane to v1.2.1.

v2.8.0, r76
- eliminate rest of non-const static data for multi-instance safety.
- fixed bug in VoodooHDADevice::messageHandler - double of use va_list causes panic if verbose level 2.
- fixed initialisation and updating of mPrefPanelMemoryBuf to be passed to prefPane.

v2.7.6, r74
- Add support for message-signalled interrupts with AllowMSI option in Info.plist.

v2.7.6, r71
- in kext, eliminate non-const static data for multi-instance safety.
- VoodooHDA.prefPane, VoodooHdaSettingsLoader support for configuring multiple instances of VoodooHDADevice.
- prefPane and SettingsLoader to v1.2.
- fixed some memory leaks in prefPane, SettingsLoader.
- format of settings file upgraded to support multi-instance.
- file now stored in ~/Library/Preferences/VoodooHDA.Settings.plist.
- SettingsLoader will upgrade old settings file if it exists and new settings file doesn't exist.

v2.7.6, r70
Fix unsafe release in VoodooHDADevice::free that may panic.

v2.7.6, r69
Fix memory overrun in VoodooHDADevice::audioAssociationParse that may panic.
Modified getdump to dump all instances of VoodooHDADevice.

v2.7.5, r68
Add some macros for build with XCode 4.5.x.

0.2.2 (4/14/09):

  * mute controls implemented
  * seperate left/right audio level controls implemented
  * showing just one device for each input/output in Sound preference pane

0.2.1 (4/10/09):

  * minor source clean-up in preparation for release
  * synchronized sources with hdac cvs revision 20090401_0132 (numerous codec-specific fixes, more
    controller/codec ids, improved widget parsing, cosmetic fixes)

0.2 (12/28/08):

  * new message logging system, can be adjusted with VoodooHDAVerboseLevel setting in Info.plist;
    overview follows, each level is inclusive of previous one
      0: quiet (except codec/controller id info at init, all errors)
      1: verbose init messages, audio engine/control debugging
      2: codec dump
      3: interrupt stats
      4: lock debugging
  * support for multiple sample rates and bit depths (16, 24, and 32-bit)
  * new audio controls in sound preference pane, separate selector for each logical oss control;
    each logical pcm device correponds to pcmN devices as with freebsd hdac (check codec dump for
    information on configurations) - this is temporary until a hal plugin is implemented which
    will allow better organization of controls
  * synchronized sources with latest hdac cvs revision 20081226_0122 (new controller and codec ids,
    also some special handling in parser for ad1986a)
  * implemented user client and included tool (getdump) for obtaining codec dump
  * many internal fixes and improvements as well as massive source cleanup
