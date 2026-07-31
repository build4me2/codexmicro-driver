/**************************************************************
* Class::  CSC-415-01 Summer 2026
* Name:: Manisha Chand
* Student ID:: 924844476
* GitHub-Name:: build4me2
* Project:: Assignment 6 – Device Driver
*
* File:: mock_agent.h
*
* Description:: A stand-in for a real coding agent. It models the small
* lifecycle a supervised agent moves through so the harness has something
* believable to display and steer without needing a live language model. It is
* deliberately isolated behind this interface: replacing it with a real backend
* would not touch the driver or the harness's control logic.
*
**************************************************************/

#ifndef MOCK_AGENT_H
#define MOCK_AGENT_H

/* How many agents the mock tracks. Matched to the driver's agent capacity so
 * every displayed agent has a real slot to report into. */
#define MOCK_AGENTS 6

/*
 * One agent's status as the user perceives it, ordered from no work through
 * active work to the two terminal outcomes. Kept separate from the driver's own
 * status type so the backend and the device remain independently replaceable.
 */
enum mock_status {
	MOCK_IDLE,
	MOCK_THINKING,
	MOCK_NEEDS_INPUT,
	MOCK_DONE,
	MOCK_ERROR
};

/* Put every agent back to idle. */
void mock_init(void);

/* Report an agent's current status. */
enum mock_status mock_get(int id);

/* Let an agent make progress on its own: idle begins thinking, thinking reaches
 * the point of needing a decision. Terminal states do not move. */
enum mock_status mock_advance(int id);

/* Resolve a waiting agent: accept finishes it successfully, reject abandons it
 * back to idle. Both only act on an agent that is actually awaiting input. */
enum mock_status mock_accept(int id);
enum mock_status mock_reject(int id);

/* Force an agent into the failed state, to demonstrate the error posture. */
enum mock_status mock_fail(int id);

/* The word the driver expects for a status, e.g. "THINKING". */
const char *mock_word(enum mock_status s);

#endif /* MOCK_AGENT_H */
