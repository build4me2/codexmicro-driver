/**************************************************************
* Class::  CSC-415-01 Summer 2026
* Name:: Manisha Chand
* Student ID:: 924844476
* GitHub-Name:: build4me2
* Project:: Assignment 6 – Device Driver
*
* File:: key_bridge.c
*
* Description:: User-space bridge that turns a Codex Micro key press into a
* real keystroke in whatever window currently has focus. It asks the driver to
* press a chosen key, drains the resulting keystroke from the driver's queue,
* and replays it through the kernel's user-level input facility so the OS
* delivers it exactly as if it came from a physical keyboard. This is what lets
* the virtual device drive any terminal or editor, independent of which
* program (or language model) is on the receiving end.
*
**************************************************************/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>

#include "../Module/codexmicro.h"  /* the driver's request numbers and key slots */

/*
 * Translating a friendly name to a key slot keeps the command line readable
 * ("accept" rather than a bare number) and keeps the mapping in one place. A
 * negative return signals a name the surface does not offer.
 */
static int slot_from_name(const char *name)
{
	if (!strcmp(name, "accept"))   return CODEX_KEY_ACCEPT;
	if (!strcmp(name, "reject"))   return CODEX_KEY_REJECT;
	if (!strcmp(name, "talk"))     return CODEX_KEY_PUSH_TALK;
	if (!strcmp(name, "newchat"))  return CODEX_KEY_NEW_CHAT;
	if (!strcmp(name, "custom"))   return CODEX_KEY_CUSTOM;
	return -1;
}

/*
 * Send one input event down the virtual keyboard. Every keystroke is expressed
 * as a small sequence of these, so isolating the single-event write keeps the
 * higher-level steps readable.
 */
static void send_event(int ufd, int type, int code, int value)
{
	struct input_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;
	if (write(ufd, &ev, sizeof(ev)) < 0)
		perror("write to uinput");
}

/*
 * Replay one keystroke as a real key: press, publish, release, publish. The
 * two publish markers are what tell the input layer that a complete key event
 * has been assembled, so the receiving program sees one clean keypress rather
 * than a dangling half-event.
 */
static void inject_keystroke(int ufd, int keycode)
{
	send_event(ufd, EV_KEY, keycode, 1);      /* key down                */
	send_event(ufd, EV_SYN, SYN_REPORT, 0);   /* commit the down event   */
	send_event(ufd, EV_KEY, keycode, 0);      /* key up                  */
	send_event(ufd, EV_SYN, SYN_REPORT, 0);   /* commit the up event     */
}

/*
 * Create a virtual keyboard the OS treats as real hardware. It must declare
 * that it produces key events and which keys it may send before it is created;
 * a generous range is enabled up front so any default or remapped keystroke can
 * be replayed without re-declaring. Returns an open handle, or -1 on failure.
 */
static int open_virtual_keyboard(void)
{
	struct uinput_setup usetup;
	int ufd;
	int code;

	ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (ufd < 0) {
		perror("open /dev/uinput");
		return -1;
	}

	/* Announce that this device emits key events, then enable the whole
	 * common keycode range so no later remap can ask for a key we forgot to
	 * turn on. */
	ioctl(ufd, UI_SET_EVBIT, EV_KEY);
	for (code = 0; code < 256; code++)
		ioctl(ufd, UI_SET_KEYBIT, code);

	memset(&usetup, 0, sizeof(usetup));
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor  = 0x1234;
	usetup.id.product = 0x5678;
	strcpy(usetup.name, "Codex Micro Virtual Keys");

	ioctl(ufd, UI_DEV_SETUP, &usetup);
	ioctl(ufd, UI_DEV_CREATE);

	/* The device node is created asynchronously; a brief pause lets the input
	 * layer finish wiring it up before the first keystroke is sent, otherwise
	 * the earliest event can be delivered before any listener is attached. */
	sleep(1);
	return ufd;
}

/* Tear down the virtual keyboard so it leaves no lingering input device. */
static void close_virtual_keyboard(int ufd)
{
	ioctl(ufd, UI_DEV_DESTROY);
	close(ufd);
}

/*
 * Perform one full press: ask the driver to press the slot, then drain and
 * replay every keystroke it queues. Draining in a loop rather than once handles
 * several presses having piled up, and the empty sentinel is the stop signal.
 * Kept separate so the main loop reads as a plain "on each request, deliver a
 * press."
 */
static void deliver_press(int devfd, int ufd, int slot)
{
	int keycode;

	if (ioctl(devfd, CODEX_PRESS_KEY, &slot) < 0) {
		perror("CODEX_PRESS_KEY");
		return;
	}
	while (ioctl(devfd, CODEX_GET_KEY, &keycode) == 0 && keycode >= 0) {
		printf("injected keystroke code %d\n", keycode);
		inject_keystroke(ufd, keycode);
	}
}

int main(int argc, char **argv)
{
	int slot;
	int devfd;
	int ufd;
	char line[16];

	if (argc != 2 || (slot = slot_from_name(argv[1])) < 0) {
		fprintf(stderr,
			"usage: %s <accept|reject|talk|newchat|custom>\n"
			"Presses the named key on the virtual device and replays the\n"
			"resulting keystroke into the system input layer.\n",
			argv[0]);
		return 1;
	}

	/* Reaching the driver confirms the module is loaded; failing here almost
	 * always means it is not. */
	devfd = open("/dev/codexmicro", O_RDWR);
	if (devfd < 0) {
		perror("open /dev/codexmicro");
		return 1;
	}

	ufd = open_virtual_keyboard();
	if (ufd < 0) {
		close(devfd);
		return 1;
	}

	/*
	 * The virtual keyboard is kept alive for the whole session rather than
	 * destroyed after a single press. That lets an observer such as evtest
	 * attach to the device first and then watch each injected key arrive, which
	 * is the only reliable way to confirm delivery on a text-only or remote
	 * session where there is no focused window to type into.
	 */
	printf("Virtual keyboard 'Codex Micro Virtual Keys' is ready.\n");
	printf("Injected keys go to the system input layer, not this terminal.\n");
	printf("To watch them (works over SSH or console): run  sudo evtest  in\n");
	printf("another terminal and choose 'Codex Micro Virtual Keys'.\n");
	printf("Press Enter to press '%s'; type q then Enter to quit.\n", argv[1]);

	while (fgets(line, sizeof(line), stdin)) {
		if (line[0] == 'q')
			break;
		deliver_press(devfd, ufd, slot);
	}

	close_virtual_keyboard(ufd);
	close(devfd);
	return 0;
}
