/**************************************************************
* File:: codexmicrod.c
*
* Description:: The Codex Micro bridge daemon. It listens on a Unix socket for
* short "<agent-id> <status>" messages from LLM adapters, turns each status
* into a color, and drives a device backend: "loopback" prints what would be
* sent (so the whole pipeline can be watched with no hardware), "virtual"
* writes to the existing /dev/codexmicro device (reusing the current driver and
* dashboard), and "hidraw" sends a 32-byte Raw HID report to a real QMK device.
* This is what lets any LLM's live agent status reach the keys, independent of
* which LLM produced it.
*
**************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "codexmicro_wire.h"

#define DEFAULT_SOCKET "/tmp/codexmicrod.sock"

enum backend { BK_LOOPBACK, BK_VIRTUAL, BK_HIDRAW };

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
	case BK_VIRTUAL: return "virtual";
	case BK_HIDRAW:  return "hidraw";
	default:         return "loopback";
	}
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
	char line[32];
	int i, len;

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

	case BK_VIRTUAL:
		/* The existing virtual device accepts a "<id>:<WORD>" status write,
		 * so pointing the bridge at it makes the current dashboard show the
		 * exact status the daemon would light on real hardware. */
		len = snprintf(line, sizeof(line), "%d:%s", agent, cm_word(s));
		if (write(devfd, line, len) < 0)
			perror("write /dev/codexmicro");
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
	}
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [--backend loopback|virtual|hidraw] [--device PATH] [--socket PATH]\n"
		"  loopback  print each status change (default; no hardware)\n"
		"  virtual   drive the existing /dev/codexmicro device\n"
		"  hidraw    drive a real QMK device at /dev/hidrawN (needs --device)\n",
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
			else if (!strcmp(b, "virtual"))  bk = BK_VIRTUAL;
			else if (!strcmp(b, "hidraw"))   bk = BK_HIDRAW;
			else { usage(argv[0]); return 1; }
		} else if (!strcmp(argv[i], "--device") && i + 1 < argc) {
			device = argv[++i];
		} else if (!strcmp(argv[i], "--socket") && i + 1 < argc) {
			g_socket_path = argv[++i];
		} else {
			usage(argv[0]);
			return 1;
		}
	}

	/* --- open the device backend (loopback needs none) --- */
	if (bk == BK_VIRTUAL) {
		if (!device)
			device = "/dev/codexmicro";
		devfd = open(device, O_WRONLY);
		if (devfd < 0) {
			perror(device);
			return 1;
		}
	} else if (bk == BK_HIDRAW) {
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
