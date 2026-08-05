#!/bin/bash
#**************************************************************
#* Class::  CSC-415-01 Summer 2026
#* Name:: Manisha Chand
#* Student ID:: 924844476
#* GitHub-Name:: build4me2
#* Project:: Assignment 6 - Device Driver
#*
#* File:: demo.sh
#*
#* Description:: Scripted, repeatable demonstration of the Codex Micro device
#* driver, intended for capturing write-up screenshots in one run. It builds and
#* loads the module, reads the initial device state, writes agent statuses and
#* reads them back, runs the ioctl interface test, and unloads the module -
#* showing the kernel-log load and unload confirmations at each end. The live
#* colour dashboard is launched separately (see the closing message) because it
#* is interactive.
#*
#**************************************************************

# Work from the directory that holds this script so the relative paths below
# resolve no matter where it is invoked from.
cd "$(dirname "$0")" || exit 1

DEV=/dev/codexmicro

echo "==== 1. BUILD AND LOAD THE MODULE ===="
make -C Module >/dev/null
# Remove any earlier copy first so the load starts from a known clean state.
sudo rmmod codexmicro 2>/dev/null
sudo insmod Module/codexmicro.ko
sudo dmesg | grep codexmicro | tail -1

echo
echo "==== 2. INITIAL DEVICE STATE (read / copy_to_user) ===="
sudo cat "$DEV"

echo
echo "==== 3. WRITE AGENT STATUS (copy_from_user), THEN READ BACK ===="
for message in "0:THINKING" "1:NEEDS_INPUT" "2:DONE" "3:ERROR"; do
	echo "$message" | sudo tee "$DEV" >/dev/null
done
sudo cat "$DEV"

echo
echo "==== 4. IOCTL INTERFACE (dial, mode, key press/remap/drain, reset) ===="
gcc -Wall -o Test/ioctl_test Test/ioctl_test.c
sudo ./Test/ioctl_test

echo
echo "==== 5. UNLOAD THE MODULE ===="
sudo rmmod codexmicro
sudo dmesg | grep codexmicro | tail -1

echo
echo "==== DEMO COMPLETE ===="
echo "For the live colour dashboard, run:"
echo "    cd Test && make && sudo ./Chand_Manisha_HW6_main"
