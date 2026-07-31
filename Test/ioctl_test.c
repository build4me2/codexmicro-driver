/**************************************************************
* Class::  CSC-415-01 Summer 2026
* Name:: Manisha Chand
* Student ID:: 924844476
* GitHub-Name:: build4me2
* Project:: Assignment 6 – Device Driver
*
* File:: ioctl_test.c
*
* Description:: Focused verification tool for the Codex Micro driver's ioctl
* control interface. It drives each request in turn and reports whether the
* observed result matches what the driver promises, so a single run confirms
* the dial round-trip, mode change, key press and drain, key remap, input
* validation, and reset all behave. This is a development check separate from
* the full demonstration application.
*
**************************************************************/

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input-event-codes.h>  /* KEY_ENTER, KEY_A for expected values */

#include "../Module/codexmicro.h"     /* the same request numbers the driver uses */

/* Print a labelled pass/fail line so the whole run can be scanned at a glance. */
static void check(const char *what, int pass)
{
	printf("[%s] %s\n", pass ? "PASS" : "FAIL", what);
}

int main(void)
{
	int fd;
	int value;
	int got;
	int key;
	int rc;
	struct codex_remap remap;

	/*
	 * The device must be open before any request can be sent; failing here
	 * usually means the module is not loaded, so it is reported plainly.
	 */
	fd = open("/dev/codexmicro", O_RDWR);
	if (fd < 0) {
		perror("open /dev/codexmicro");
		return 1;
	}

	/* Setting the dial then reading it back proves a value survives a full
	 * round trip across the boundary in both directions. */
	value = 70;
	ioctl(fd, CODEX_SET_DIAL, &value);
	ioctl(fd, CODEX_GET_DIAL, &got);
	check("dial set to 70 reads back as 70", got == 70);

	/* Mode is write-only over ioctl; its effect is observed by reading the
	 * device text, so here we only confirm the request is accepted. */
	value = CODEX_MODE_STEERING;
	rc = ioctl(fd, CODEX_SET_MODE, &value);
	check("mode set to STEERING accepted", rc == 0);

	/* A press should resolve through the default map to Enter and wait in the
	 * queue until drained. */
	value = CODEX_KEY_ACCEPT;
	ioctl(fd, CODEX_PRESS_KEY, &value);
	ioctl(fd, CODEX_GET_KEY, &key);
	check("press ACCEPT drains to KEY_ENTER", key == KEY_ENTER);

	/* With the single queued press consumed, the next drain must report the
	 * empty sentinel rather than a stale key. */
	ioctl(fd, CODEX_GET_KEY, &key);
	check("second drain reports empty (-1)", key == -1);

	/* Remapping a slot must change what a later press of that slot emits. */
	remap.slot = CODEX_KEY_ACCEPT;
	remap.keycode = KEY_A;
	ioctl(fd, CODEX_REMAP_KEY, &remap);
	value = CODEX_KEY_ACCEPT;
	ioctl(fd, CODEX_PRESS_KEY, &value);
	ioctl(fd, CODEX_GET_KEY, &key);
	check("after remap, ACCEPT drains to KEY_A", key == KEY_A);

	/* An out-of-range dial must be refused with EINVAL, proving validation
	 * happens before any state changes. */
	value = 999;
	errno = 0;
	rc = ioctl(fd, CODEX_SET_DIAL, &value);
	check("dial 999 rejected with EINVAL", rc < 0 && errno == EINVAL);

	/* An unknown key slot must be refused the same way. */
	value = 99;
	errno = 0;
	rc = ioctl(fd, CODEX_PRESS_KEY, &value);
	check("key slot 99 rejected with EINVAL", rc < 0 && errno == EINVAL);

	/* Reset must return the dial to its documented default, standing in for a
	 * full return to the load-time state. */
	ioctl(fd, CODEX_RESET, 0);
	ioctl(fd, CODEX_GET_DIAL, &got);
	check("reset restores dial to 50", got == 50);

	close(fd);
	return 0;
}
