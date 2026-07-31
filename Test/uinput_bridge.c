/**************************************************************
* Class::  CSC-415-01 Summer 2026
* Name:: Manisha Chand
* Student ID:: 924844476
* GitHub-Name:: build4me2
* Project:: Assignment 6 – Device Driver
*
* File:: uinput_bridge.c
*
* Description:: Implementation of the shared virtual-keyboard helper. It builds
* a keyboard the operating system accepts as real hardware and replays
* keystrokes through it, which is what lets a Codex Micro key press reach any
* terminal, editor, or console exactly as a physical key would.
*
**************************************************************/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>

#include "uinput_bridge.h"

/*
 * Send one input event down the virtual keyboard. Every keystroke is expressed
 * as a small sequence of these, so isolating the single-event write keeps the
 * higher-level steps readable.
 */
static void send_event(int handle, int type, int code, int value)
{
	struct input_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;
	if (write(handle, &ev, sizeof(ev)) < 0)
		perror("write to uinput");
}

int uinput_open(void)
{
	struct uinput_setup usetup;
	int handle;
	int code;

	handle = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (handle < 0) {
		perror("open /dev/uinput");
		return -1;
	}

	/*
	 * Announce that this device emits key events, then enable the whole common
	 * keycode range so no later remap can ask for a key that was never turned
	 * on. Declaring capabilities must happen before the device is created.
	 */
	ioctl(handle, UI_SET_EVBIT, EV_KEY);
	for (code = 0; code < 256; code++)
		ioctl(handle, UI_SET_KEYBIT, code);

	memset(&usetup, 0, sizeof(usetup));
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor  = 0x1234;
	usetup.id.product = 0x5678;
	strcpy(usetup.name, "Codex Micro Virtual Keys");

	ioctl(handle, UI_DEV_SETUP, &usetup);
	ioctl(handle, UI_DEV_CREATE);

	/*
	 * The device node is created asynchronously; a brief pause lets the input
	 * layer finish wiring it up before the first keystroke is sent, otherwise
	 * the earliest event can be delivered before any listener is attached.
	 */
	sleep(1);
	return handle;
}

void uinput_inject(int handle, int keycode)
{
	/*
	 * A real keypress is press, publish, release, publish. The two publish
	 * markers tell the input layer that a complete key event has been
	 * assembled, so the receiver sees one clean keypress rather than a dangling
	 * half-event.
	 */
	send_event(handle, EV_KEY, keycode, 1);
	send_event(handle, EV_SYN, SYN_REPORT, 0);
	send_event(handle, EV_KEY, keycode, 0);
	send_event(handle, EV_SYN, SYN_REPORT, 0);
}

void uinput_close(int handle)
{
	ioctl(handle, UI_DEV_DESTROY);
	close(handle);
}
