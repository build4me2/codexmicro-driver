/**************************************************************
*
* File:: mock_agent.c
*
* Description:: Implementation of the simulated agent lifecycle. Each agent
* holds a single status that only ever changes through the defined transitions,
* which keeps the simulated behaviour predictable and makes an out-of-range
* request harmless rather than a source of surprise.
*
**************************************************************/

#include "mock_agent.h"

/* Each agent's current status; the whole state of the mock backend. */
static enum mock_status agents[MOCK_AGENTS];

/* Guard so a bad id from the caller is ignored instead of reaching past the
 * array. Centralized here so every entry point checks it the same way. */
static int valid(int id)
{
	return id >= 0 && id < MOCK_AGENTS;
}

void mock_init(void)
{
	int i;

	for (i = 0; i < MOCK_AGENTS; i++)
		agents[i] = MOCK_IDLE;
}

enum mock_status mock_get(int id)
{
	return valid(id) ? agents[id] : MOCK_IDLE;
}

enum mock_status mock_advance(int id)
{
	if (!valid(id))
		return MOCK_IDLE;

	/*
	 * Progress follows the natural order of doing work: an idle agent picks up
	 * a task and thinks, and a thinking agent eventually reaches a decision it
	 * cannot make alone. Finished and failed agents stay put because they have
	 * nothing left to advance to.
	 */
	switch (agents[id]) {
	case MOCK_IDLE:     agents[id] = MOCK_THINKING;    break;
	case MOCK_THINKING: agents[id] = MOCK_NEEDS_INPUT; break;
	default:                                           break;
	}
	return agents[id];
}

enum mock_status mock_accept(int id)
{
	/* Accepting only means something for an agent that is actually waiting on
	 * the user; applied at any other time it is a no-op so the display cannot
	 * jump to a nonsensical state. */
	if (valid(id) && agents[id] == MOCK_NEEDS_INPUT)
		agents[id] = MOCK_DONE;
	return mock_get(id);
}

enum mock_status mock_reject(int id)
{
	if (valid(id) && agents[id] == MOCK_NEEDS_INPUT)
		agents[id] = MOCK_IDLE;
	return mock_get(id);
}

enum mock_status mock_fail(int id)
{
	if (valid(id))
		agents[id] = MOCK_ERROR;
	return mock_get(id);
}

const char *mock_word(enum mock_status s)
{
	switch (s) {
	case MOCK_THINKING:    return "THINKING";
	case MOCK_NEEDS_INPUT: return "NEEDS_INPUT";
	case MOCK_DONE:        return "DONE";
	case MOCK_ERROR:       return "ERROR";
	default:               return "IDLE";
	}
}
