/**************************************************************
* File:: codexmicrod.c
*
* Description:: The Codex Micro bridge daemon. It listens on a Unix socket for
* short "<agent-id> <status>" messages from LLM adapters, turns each status
* into a color, and drives a device backend: "loopback" prints what would be
* sent, "sim" renders the keys as colored blocks in the terminal (both need no
* hardware), and "hidraw" sends a 32-byte Raw HID report to a real QMK device.
* This is what lets any LLM's live agent status reach the keys, independent of
* which LLM produced it.
*
**************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "codexmicro_wire.h"

#define DEFAULT_SOCKET "/tmp/codexmicrod.sock"

enum backend { BK_LOOPBACK, BK_HIDRAW, BK_SIM };

/* Remembered at file scope so the signal handler can remove the socket file it
 * created; a stale file would otherwise block the next run from binding. */
static const char *g_socket_path = DEFAULT_SOCKET;

/*
 * Remove the socket file and exit on Ctrl-C or termination. Only
 * async-signal-safe calls are used here because it runs from a signal handler.
 */
static void on_signal(int sig)
{
	(void)sig;
	unlink(g_socket_path);
	_exit(0);
}

/* The human-readable name of a backend, for the startup message. */
static const char *backend_name(enum backend bk)
{
	switch (bk) {
	case BK_HIDRAW: return "hidraw";
	case BK_SIM:    return "sim";
	default:        return "loopback";
	}
}

/*
 * The "sim" backend renders the agent keys as colored blocks in this terminal,
 * so the whole pipeline can be watched visually with no hardware. It keeps each
 * key's latest status and repaints the panel on every change. The count mirrors
 * the real device's six status keys; statuses for higher ids are ignored, and
 * the all-idle starting state is the array's zero value.
 */
#define SIM_AGENTS 6
static enum cm_status g_sim[SIM_AGENTS];

/* Optional display name per key, set with "--label N=name"; a NULL entry falls
 * back to "agent N" so the panel is readable with or without names. */
static const char *g_label[SIM_AGENTS];

/*
 * Print a colored block `width` cells wide for a status, then its word. Shared
 * by the agent rows (wide blocks) and the legend (small blocks) so the colors
 * stay identical between them.
 */
static void sim_swatch(enum cm_status s, int width, const char *text)
{
	unsigned char r, g, b;

	cm_default_color(s, &r, &g, &b);
	printf("\033[48;2;%u;%u;%um%*s\033[0m  %s", r, g, b, width, "", text);
}

static void sim_render(void)
{
	static const enum cm_status legend[] = {
		CM_THINKING, CM_NEEDS_INPUT, CM_DONE, CM_ERROR, CM_IDLE
	};
	char fallback[16];
	const char *label;
	size_t k;
	int i;

	printf("\033[2J\033[H");  /* clear the screen and move to the top */
	printf("Codex Micro - agent status (sim; Ctrl-C to quit)\n\n");

	/* One row per key: its name, a colored block, and the status word. */
	for (i = 0; i < SIM_AGENTS; i++) {
		label = g_label[i];
		if (!label) {
			snprintf(fallback, sizeof(fallback), "agent %d", i);
			label = fallback;
		}
		printf("  %-12s ", label);
		sim_swatch(g_sim[i], 6, cm_word(g_sim[i]));
		printf("\n");
	}

	/* A compact legend so the colors are self-explanatory. */
	printf("\n  legend: ");
	for (k = 0; k < sizeof(legend) / sizeof(legend[0]); k++) {
		sim_swatch(legend[k], 2, cm_word(legend[k]));
		printf("   ");
	}
	printf("\n");
	fflush(stdout);
}

/*
 * Deliver one status change to the chosen backend. The color comes from the
 * shared default map, so every backend shows the same meaning for a status.
 */
static void send_status(enum backend bk, int devfd, int agent, enum cm_status s)
{
	unsigned char r, g, b;
	unsigned char report[CM_REPORT_LEN];
	unsigned char framed[CM_REPORT_LEN + 1];
	int i;

	cm_default_color(s, &r, &g, &b);

	switch (bk) {
	case BK_LOOPBACK:
		/* No hardware: print the resolved color and the leading report
		 * bytes so the end-to-end path can be verified by eye. */
		cm_build_report(report, agent, s, r, g, b);
		printf("agent %d -> %-11s  color(%3u,%3u,%3u)  report:",
		       agent, cm_word(s), r, g, b);
		for (i = 0; i < 7; i++)
			printf(" %02x", report[i]);
		printf(" ...\n");
		fflush(stdout);
		break;

	case BK_HIDRAW:
		/* A QMK device receives a fixed 32-byte report; hidraw prepends a
		 * report-number byte (0) ahead of that payload. */
		cm_build_report(report, agent, s, r, g, b);
		framed[0] = 0x00;
		memcpy(framed + 1, report, CM_REPORT_LEN);
		if (write(devfd, framed, CM_REPORT_LEN + 1) < 0)
			perror("write hidraw");
		break;

	case BK_SIM:
		/* Record this key's status and repaint the whole panel. */
		if (agent < SIM_AGENTS)
			g_sim[agent] = s;
		sim_render();
		break;
	}
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [--backend loopback|sim|hidraw] [--device PATH] [--socket PATH]\n"
		"       [--label N=name ...]\n"
		"  loopback  print each status change (default; no hardware)\n"
		"  sim       show the keys as colored blocks in this terminal (no hardware)\n"
		"  hidraw    drive a real QMK device at /dev/hidrawN (needs --device)\n"
		"  --label   name a key in the sim panel, e.g. --label 0=frontend\n",
		prog);
}

int main(int argc, char **argv)
{
	enum backend bk = BK_LOOPBACK;
	const char *device = NULL;
	int devfd = -1;
	int sock;
	struct sockaddr_un addr;
	char buf[128];
	int i;

	/* --- parse arguments --- */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--backend") && i + 1 < argc) {
			const char *b = argv[++i];
			if      (!strcmp(b, "loopback")) bk = BK_LOOPBACK;
			else if (!strcmp(b, "sim"))      bk = BK_SIM;
			else if (!strcmp(b, "hidraw"))   bk = BK_HIDRAW;
			else { usage(argv[0]); return 1; }
		} else if (!strcmp(argv[i], "--device") && i + 1 < argc) {
			device = argv[++i];
		} else if (!strcmp(argv[i], "--socket") && i + 1 < argc) {
			g_socket_path = argv[++i];
		} else if (!strcmp(argv[i], "--label") && i + 1 < argc) {
			/* "--label N=name" gives key N a display name in the sim panel. */
			char *spec = argv[++i];
			char *eq = strchr(spec, '=');
			if (eq) {
				int id;
				*eq = '\0';
				id = atoi(spec);
				if (id >= 0 && id < SIM_AGENTS)
					g_label[id] = eq + 1;
			}
		} else {
			usage(argv[0]);
			return 1;
		}
	}

	/* --- open the device backend (loopback needs none) --- */
	if (bk == BK_HIDRAW) {
		if (!device) {
			fprintf(stderr, "hidraw backend needs --device /dev/hidrawN\n");
			return 1;
		}
		devfd = open(device, O_WRONLY);
		if (devfd < 0) {
			perror(device);
			return 1;
		}
	}

	/* --- bind the status-intake socket --- */
	sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		return 1;
	}
	unlink(g_socket_path);  /* clear any stale socket from a prior run */
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, g_socket_path, sizeof(addr.sun_path) - 1);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	printf("codexmicrod: listening on %s (backend: %s)\n",
	       g_socket_path, backend_name(bk));
	fflush(stdout);

	/* Show the initial all-idle panel right away in sim mode. */
	if (bk == BK_SIM)
		sim_render();

	/* --- receive "<agent> <status>" messages and drive the device --- */
	for (;;) {
		ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
		enum cm_status s;
		char word[32];
		int agent;

		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("recvfrom");
			break;
		}
		buf[n] = '\0';

		/* Every field is validated before it drives the device, so a
		 * malformed message is reported and dropped rather than acted on. */
		if (sscanf(buf, "%d %31s", &agent, word) != 2) {
			fprintf(stderr, "ignoring malformed message: %s\n", buf);
			continue;
		}
		if (agent < 0) {
			fprintf(stderr, "ignoring bad agent id: %d\n", agent);
			continue;
		}
		if (!cm_parse(word, &s)) {
			fprintf(stderr, "ignoring unknown status: %s\n", word);
			continue;
		}
		send_status(bk, devfd, agent, s);
	}

	if (devfd >= 0)
		close(devfd);
	close(sock);
	unlink(g_socket_path);
	return 0;
}
