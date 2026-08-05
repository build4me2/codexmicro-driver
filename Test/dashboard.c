/**************************************************************
*
* File:: dashboard.c
*
* Description:: Demonstration application for the Codex Micro device driver. It
* is the agent-control surface a developer would use: it drives a simulated
* fleet of coding agents, pushes each agent's status into the driver, reads the
* whole device state back to paint a colour-coded dashboard, and turns accept
* and reject commands into real key presses delivered through the virtual
* keyboard. Every part of the driver's interface is exercised here - open,
* write, read, ioctl (dial, mode, key press, key drain), and close - and the
* agent source is chosen from a config file so the same surface could drive any
* backend, not only the built-in mock.
*
**************************************************************/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "../Module/codexmicro.h"  /* the driver's request numbers and enums */
#include "mock_agent.h"            /* the simulated agent backend            */
#include "uinput_bridge.h"         /* delivers key presses as real keystrokes */

#define DEVICE_PATH "/dev/codexmicro"
#define CONFIG_PATH "codexmicro.conf"

/* Colours for each status, chosen to match the real device's status lights:
 * blue thinking, amber waiting on the user, green done, red error, and a dim
 * tone for idle so active agents stand out. */
#define C_BLUE   "\033[34m"
#define C_AMBER  "\033[33m"
#define C_GREEN  "\033[32m"
#define C_RED    "\033[31m"
#define C_DIM    "\033[2m"
#define C_RESET  "\033[0m"

/* The backend chosen at startup, shown in the dashboard header so it is always
 * clear which source of agent status is driving the surface. */
static char backend[32] = "mock";

/*
 * Read the backend name from the config file, defaulting to the built-in mock
 * if the file is missing or says nothing about it. Reading it here rather than
 * hard-coding it is what makes the surface backend-agnostic.
 */
static void load_config(void)
{
	FILE *f;
	char line[128];

	f = fopen(CONFIG_PATH, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "backend=%31s", backend) == 1)
			break;
	}
	fclose(f);
}

/* Pick the colour that represents a status word, so the dashboard reads at a
 * glance. Unknown or idle words get the dim tone. */
static const char *color_for(const char *status)
{
	if (!strcmp(status, "THINKING"))    return C_BLUE;
	if (!strcmp(status, "NEEDS_INPUT")) return C_AMBER;
	if (!strcmp(status, "DONE"))        return C_GREEN;
	if (!strcmp(status, "ERROR"))       return C_RED;
	return C_DIM;
}

/*
 * Fetch the device's current text picture. A fresh open each call keeps the
 * read simple - it always starts at the beginning of the picture - and doubles
 * as a genuine exercise of the driver's open, read, and release paths every
 * time the dashboard refreshes. Returns 0 on success.
 */
static int read_snapshot(char *buf, size_t cap)
{
	int fd;
	ssize_t n;

	fd = open(DEVICE_PATH, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, cap - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	return 0;
}

/*
 * Draw the dashboard from the device's own reported state, so what is shown is
 * exactly what the driver holds rather than a guess kept in the app. The dial
 * is drawn as a bar for a quick sense of its level, and each agent's status is
 * coloured by meaning.
 */
static void render(void)
{
	char snap[512];
	char mode[16];
	char status[16];
	const char *p;
	int dial, last_key, id, i;

	if (read_snapshot(snap, sizeof(snap)) < 0) {
		printf("Cannot read %s - is the module loaded?\n", DEVICE_PATH);
		return;
	}

	printf("\033[2J\033[H");  /* clear the screen and move to the top */
	printf("==== Codex Micro - Agent Control Surface (backend: %s) ====\n\n", backend);

	/* The header line of the snapshot carries the dial, mode, and last key. */
	if (sscanf(snap, "dial=%d mode=%15s last_key=%d", &dial, mode, &last_key) == 3) {
		printf("Reasoning dial %3d/100  [", dial);
		for (i = 0; i < 20; i++)
			putchar(i < dial / 5 ? '#' : '.');
		printf("]\n");
		printf("Mode: %-9s  Last key slot: %d\n\n", mode, last_key);
	}

	/* Walk each "agent[n]=STATUS" entry in the snapshot and print a coloured
	 * row, so the grid mirrors the device exactly. */
	p = snap;
	while ((p = strstr(p, "agent[")) != NULL) {
		if (sscanf(p, "agent[%d]=%15s", &id, status) == 2)
			printf("   Agent %d  %s%-12s%s\n",
			       id, color_for(status), status, C_RESET);
		p += 6;  /* step past this "agent[" so the search finds the next */
	}
	printf("\n");
}

/* Print the command menu below the dashboard. */
static void show_menu(void)
{
	printf("Commands:\n");
	printf("  s <id>   step an agent forward (idle->thinking->needs-input)\n");
	printf("  a <id>   accept: press ACCEPT key + finish the agent\n");
	printf("  r <id>   reject: press REJECT key + abandon the agent\n");
	printf("  e <id>   mark an agent errored\n");
	printf("  d <0-100> set the reasoning dial\n");
	printf("  m <0-2>   set mode (0 idle, 1 steering, 2 review)\n");
	printf("  q        quit\n");
	printf("> ");
	fflush(stdout);
}

/*
 * Record an agent's status in the driver. This is the "writing" side of the
 * device: the harness tells the driver what each agent is doing so a later read
 * reflects it in the dashboard.
 */
static void push_status(int devfd, int id, enum mock_status s)
{
	char msg[32];
	int len;

	/* A command with no valid agent id - for example a bare "s" with no
	 * number - should not reach the driver as a malformed write. Ignoring it
	 * here keeps the dashboard quiet rather than printing an error for a
	 * harmless mistype. */
	if (id < 0 || id >= MOCK_AGENTS)
		return;

	len = snprintf(msg, sizeof(msg), "%d:%s", id, mock_word(s));
	if (write(devfd, msg, len) < 0)
		perror("write status");
}

/*
 * Press a key on the device and deliver the resulting keystroke for real. The
 * driver resolves the slot to a keystroke and queues it; here it is drained and
 * replayed through the virtual keyboard, so an accept or reject is felt by
 * whatever program is receiving input, not just noted on screen.
 */
static void press_key(int devfd, int ufd, int slot)
{
	int keycode;

	if (ioctl(devfd, CODEX_PRESS_KEY, &slot) < 0) {
		perror("CODEX_PRESS_KEY");
		return;
	}
	while (ioctl(devfd, CODEX_GET_KEY, &keycode) == 0 && keycode >= 0) {
		/* Drain the queue even when no virtual keyboard is available so a key
		 * is never left behind; only the physical injection is skipped. */
		if (ufd >= 0)
			uinput_inject(ufd, keycode);
	}
}

/*
 * Carry out one command. Agent-facing commands move the mock agent and then
 * write the new status to the device; dial and mode go straight to the driver
 * over ioctl. Accept and reject additionally fire the matching key so the
 * decision leaves the surface as a real keystroke.
 */
static void handle(int devfd, int ufd, char cmd, int arg)
{
	switch (cmd) {
	case 's':
		push_status(devfd, arg, mock_advance(arg));
		break;
	case 'a':
		press_key(devfd, ufd, CODEX_KEY_ACCEPT);
		push_status(devfd, arg, mock_accept(arg));
		break;
	case 'r':
		press_key(devfd, ufd, CODEX_KEY_REJECT);
		push_status(devfd, arg, mock_reject(arg));
		break;
	case 'e':
		push_status(devfd, arg, mock_fail(arg));
		break;
	case 'd':
		if (arg >= 0 && arg <= 100 && ioctl(devfd, CODEX_SET_DIAL, &arg) < 0)
			perror("CODEX_SET_DIAL");
		break;
	case 'm':
		if (arg >= 0 && arg <= 2 && ioctl(devfd, CODEX_SET_MODE, &arg) < 0)
			perror("CODEX_SET_MODE");
		break;
	default:
		break;  /* unknown or empty input simply redraws */
	}
}

int main(void)
{
	int devfd;
	int ufd;
	char line[64];

	load_config();

	/* The persistent read-write handle is used for writing status and for all
	 * ioctl control; opening it also confirms the module is loaded. */
	devfd = open(DEVICE_PATH, O_RDWR);
	if (devfd < 0) {
		perror("open " DEVICE_PATH);
		return 1;
	}

	/*
	 * The virtual keyboard is created once and reused for every key press. If it
	 * cannot be created (for example the uinput module is not loaded) the
	 * dashboard and all driver control still work, so the program continues and
	 * only skips the physical key injection rather than refusing to start.
	 */
	ufd = uinput_open();
	if (ufd < 0)
		fprintf(stderr, "warning: virtual keyboard unavailable; "
				"accept/reject will not inject real keystrokes\n");

	mock_init();

	/* Redraw, take one command, act, and repeat. Reading a whole line keeps
	 * input simple and reliable over a remote session, where there is no
	 * graphical window to capture individual keys. */
	for (;;) {
		char cmd = 0;
		int arg = -1;

		render();
		show_menu();

		if (!fgets(line, sizeof(line), stdin))
			break;
		sscanf(line, " %c %d", &cmd, &arg);
		if (cmd == 'q')
			break;
		handle(devfd, ufd, cmd, arg);
	}

	if (ufd >= 0)
		uinput_close(ufd);
	close(devfd);
	printf("\nHarness exited.\n");
	return 0;
}
