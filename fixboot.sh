#!/bin/sh
# Runs as root on the camera's Linux side (started by the fixfl autostart app).
H=/android/storage/sdcard0/HAX
cp $H/fixfl /tmp/fixfl && chmod 755 /tmp/fixfl
# already running? (the pattern can't match this script's own command line)
ps | grep 'fixwatch[.]sh' > /dev/null && exit 0
BK=/usr/bin/bk.elf sh $H/fixwatch.sh > /dev/null 2>&1
