/**************************************************************
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

#include "../Module/codexmicro.h"  /* the driver's request numbers and key slots */
#include "uinput_bridge.h"         /* shared virtual-keyboard helper             */

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
		uinput_inject(ufd, keycode);
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

	ufd = uinput_open();
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

	uinput_close(ufd);
	close(devfd);
	return 0;
}
