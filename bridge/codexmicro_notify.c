/**************************************************************
* File:: codexmicro_notify.c
*
* Description:: One-shot client that tells the running bridge daemon an agent's
* new status. LLM adapters (editor hooks, scripts) run this on each lifecycle
* event, e.g. "codexmicro-notify 0 thinking". It validates the status word
* locally so a typo fails immediately, then sends "<agent> <STATUS>" to the
* daemon's Unix socket and exits.
*
**************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "codexmicro_wire.h"

#define DEFAULT_SOCKET "/tmp/codexmicrod.sock"

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s <agent-id> <status> [--socket PATH]\n"
		"  status: idle | thinking | needs_input | done | error\n",
		prog);
}

int main(int argc, char **argv)
{
	const char *socket_path = DEFAULT_SOCKET;
	const char *agent_arg = NULL;
	const char *status_arg = NULL;
	struct sockaddr_un addr;
	enum cm_status s;
	char msg[64];
	int agent, sock, len, i;

	/* Two positional arguments (agent, status) plus an optional --socket. */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--socket") && i + 1 < argc)
			socket_path = argv[++i];
		else if (!agent_arg)
			agent_arg = argv[i];
		else if (!status_arg)
			status_arg = argv[i];
		else {
			usage(argv[0]);
			return 1;
		}
	}

	if (!agent_arg || !status_arg) {
		usage(argv[0]);
		return 1;
	}

	agent = atoi(agent_arg);
	if (agent < 0) {
		fprintf(stderr, "agent id must be >= 0\n");
		return 1;
	}

	/* Validate the status here too, so a bad word is caught before anything is
	 * sent and the caller gets an immediate, clear error. */
	if (!cm_parse(status_arg, &s)) {
		fprintf(stderr, "unknown status '%s' "
			"(use idle|thinking|needs_input|done|error)\n", status_arg);
		return 1;
	}

	sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

	/* Send the canonical uppercase word so the daemon always receives a clean
	 * token regardless of how the caller capitalized it. */
	len = snprintf(msg, sizeof(msg), "%d %s", agent, cm_word(s));
	if (sendto(sock, msg, len, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("sendto");
		fprintf(stderr, "is codexmicrod running? (socket: %s)\n", socket_path);
		close(sock);
		return 1;
	}

	close(sock);
	return 0;
}
