/**************************************************************
* File:: codexmicro_wire.h
*
* Description:: Shared contract between the Codex Micro bridge daemon and the
* programs that feed it status. It defines the agent-status vocabulary (the
* same words the virtual device already understands), the default
* status-to-color mapping, and the fixed-size report a QMK device expects over
* Raw HID. Keeping all of this in one header means the daemon, the notify
* client, and the firmware protocol can never drift apart.
*
**************************************************************/

#ifndef CODEXMICRO_WIRE_H
#define CODEXMICRO_WIRE_H

#include <string.h>
#include <strings.h>  /* strcasecmp */

/*
 * The status an agent can report, as the whole system understands it. The set
 * deliberately matches the words the virtual /dev/codexmicro device already
 * accepts, so the bridge and the existing driver speak one language.
 */
enum cm_status {
	CM_IDLE,
	CM_THINKING,
	CM_NEEDS_INPUT,
	CM_DONE,
	CM_ERROR,
	CM_STATUS_COUNT
};

/*
 * The canonical uppercase word for a status. This is also the exact token the
 * virtual device expects inside a "<id>:<WORD>" write, so the same function
 * serves both display and that backend. An unknown value reads as idle so the
 * output is always well-formed.
 */
static inline const char *cm_word(enum cm_status s)
{
	switch (s) {
	case CM_THINKING:    return "THINKING";
	case CM_NEEDS_INPUT: return "NEEDS_INPUT";
	case CM_DONE:        return "DONE";
	case CM_ERROR:       return "ERROR";
	default:             return "IDLE";
	}
}

/*
 * Accept a status word in any capitalization and deliver the matched code.
 * Rejecting an unknown word lets a typo surface as a clear error rather than
 * silently becoming the wrong status. Returns non-zero on a match.
 */
static inline int cm_parse(const char *word, enum cm_status *out)
{
	if      (!strcasecmp(word, "IDLE"))        *out = CM_IDLE;
	else if (!strcasecmp(word, "THINKING"))    *out = CM_THINKING;
	else if (!strcasecmp(word, "NEEDS_INPUT")) *out = CM_NEEDS_INPUT;
	else if (!strcasecmp(word, "DONE"))        *out = CM_DONE;
	else if (!strcasecmp(word, "ERROR"))       *out = CM_ERROR;
	else return 0;
	return 1;
}

/*
 * The default color for each status, matching the real device's status lights:
 * dim when idle, blue thinking, amber awaiting input, green done, red error.
 * Delivered through out-params so a caller can override a color before sending.
 */
static inline void cm_default_color(enum cm_status s,
				    unsigned char *r, unsigned char *g, unsigned char *b)
{
	switch (s) {
	case CM_THINKING:    *r = 0;   *g = 0;   *b = 255; break;  /* blue  */
	case CM_NEEDS_INPUT: *r = 255; *g = 170; *b = 0;   break;  /* amber */
	case CM_DONE:        *r = 0;   *g = 255; *b = 0;   break;  /* green */
	case CM_ERROR:       *r = 255; *g = 0;   *b = 0;   break;  /* red   */
	default:             *r = 8;   *g = 8;   *b = 8;   break;  /* dim   */
	}
}

/*
 * The Raw HID report shape shared with the QMK firmware. QMK delivers a fixed
 * 32-byte report to raw_hid_receive; a leading magic byte lets the firmware
 * ignore anything not meant for it, and the remaining fields name the target
 * key, its new status, and the color to show.
 */
#define CM_REPORT_LEN  32
#define CM_MAGIC       0xCE
#define CM_CMD_STATUS  0x01

enum {
	CM_OFF_MAGIC  = 0,
	CM_OFF_CMD    = 1,
	CM_OFF_AGENT  = 2,
	CM_OFF_STATUS = 3,
	CM_OFF_R      = 4,
	CM_OFF_G      = 5,
	CM_OFF_B      = 6
};

/*
 * Fill a report buffer meaning "agent <id> is now <status>, show <r,g,b>", and
 * return the number of bytes to send. The buffer must hold at least
 * CM_REPORT_LEN; the whole report is zeroed first so unused bytes are defined.
 */
static inline int cm_build_report(unsigned char *buf, int agent, enum cm_status s,
				  unsigned char r, unsigned char g, unsigned char b)
{
	memset(buf, 0, CM_REPORT_LEN);
	buf[CM_OFF_MAGIC]  = CM_MAGIC;
	buf[CM_OFF_CMD]    = CM_CMD_STATUS;
	buf[CM_OFF_AGENT]  = (unsigned char)agent;
	buf[CM_OFF_STATUS] = (unsigned char)s;
	buf[CM_OFF_R]      = r;
	buf[CM_OFF_G]      = g;
	buf[CM_OFF_B]      = b;
	return CM_REPORT_LEN;
}

#endif /* CODEXMICRO_WIRE_H */
