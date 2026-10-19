#!/bin/sh

VERSION="0.2.2"
RELFILE="VoodooHDA-$VERSION.tar.bz2"
ACTION="$1"
TARGET="Debug"
KEXT="build/$TARGET/VoodooHDA.kext"
TMPDIR="tmp"
TMPKEXT="$TMPDIR/VoodooHDA.kext"

if [ "$ACTION" = "clean" ]; then
	set -x
	rm -rf release $RELFILE getdump build
	[ -e $TMPDIR ] && sudo rm -rf $TMPDIR
elif [ "$ACTION" = "build" ]; then
	set -x
	xcodebuild -configuration $TARGET -target FloatSupport -target VoodooHDA
	gcc getdump.c -o getdump -framework IOKit -framework CoreFoundation -Wall -Wextra -Werror
elif [ "$ACTION" = "release" ]; then
	if [ ! -e $KEXT ] || [ ! -e getdump ]; then
		echo "please run with 'build' argument first"
		exit
	fi
	set -x
	rm -rf release
	mkdir release
	cp getdump release
	cp License.h release/License.txt
	cp Readme.txt release/Readme.txt
	cp -r $KEXT release
	COPYFILE_DISABLE=true tar --owner root --group wheel -C release -c . | bzip2 -9 > $RELFILE
	rm -rf release
elif [ "$ACTION" = "load" ]; then
	if [ ! -e $KEXT ]; then
		echo "please run with 'build' argument first"
		exit
	fi
	set -x
	sudo rm -rf $TMPDIR
	mkdir $TMPDIR
	cp -r $KEXT $TMPKEXT
	sudo chown -R root:wheel $TMPKEXT
	sync
	sudo kextunload $TMPKEXT
	sudo kextunload $TMPKEXT
	sync
	sudo kextload $TMPKEXT
elif [ "$ACTION" = "unload" ]; then
	if [ ! -e $TMPKEXT ]; then
		echo "driver is either not loaded or the build was cleaned"
		exit
	fi
	set -x
	sync
	sudo kextunload $TMPKEXT
	sudo kextunload $TMPKEXT
	sudo rm -rf $TMPDIR
else
	echo "usage: $0 [clean|build|release|load|unload]"
fi
