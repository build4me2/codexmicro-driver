/**************************************************************
*
* File:: codexmicro.h
*
* Description:: Shared control-interface contract between the Codex Micro
* kernel driver and any user-space program that drives it. Placing the
* request numbers, the argument structure, and the shared enumerations in a
* single header means the two sides can never drift out of agreement on the
* protocol: both compile against exactly the same definitions.
*
**************************************************************/

#ifndef CODEXMICRO_H
#define CODEXMICRO_H

#include <linux/ioctl.h>  /* provides the _IOW/_IOR request-number builders */

/*
 * Every request for this device is tagged with one magic byte. The tag lets
 * the kernel notice immediately if a request meant for a different driver is
 * ever aimed here, so a mismatched call fails cleanly instead of being
 * misinterpreted.
 */
#define CODEX_IOC_MAGIC 'C'

/*
 * Argument for reprogramming a key: aim one key slot at a different keystroke.
 * Defined before the request numbers below because those encode this type's
 * size into the request that carries it.
 */
struct codex_remap {
	int slot;     /* which key to reprogram (a value from enum codex_key) */
	int keycode;  /* the keystroke it should emit from then on            */
};

/*
 * The key slots the surface exposes. These values double as the index into the
 * driver's remappable key table, and COUNT both closes the set and sizes that
 * table, so adding a slot here automatically grows the table with no other
 * edit.
 */
enum codex_key {
	CODEX_KEY_ACCEPT,
	CODEX_KEY_REJECT,
	CODEX_KEY_PUSH_TALK,
	CODEX_KEY_NEW_CHAT,
	CODEX_KEY_CUSTOM,
	CODEX_KEY_COUNT
};

/*
 * Overall posture of the control surface, mirroring how the real device
 * distinguishes idly watching from actively steering or reviewing a change.
 * This is the argument to the set-mode request.
 */
enum codex_mode {
	CODEX_MODE_IDLE,
	CODEX_MODE_STEERING,
	CODEX_MODE_REVIEW
};

/*
 * The request numbers. The transfer direction baked into each one (_IOW means
 * the app hands data to the driver, _IOR means the driver hands data back)
 * lets the kernel validate the argument copy in the correct direction.
 */
#define CODEX_SET_DIAL   _IOW(CODEX_IOC_MAGIC, 1, int)                 /* set reasoning level 0..100      */
#define CODEX_GET_DIAL   _IOR(CODEX_IOC_MAGIC, 2, int)                 /* read the reasoning level back    */
#define CODEX_SET_MODE   _IOW(CODEX_IOC_MAGIC, 3, int)                 /* set control-surface posture      */
#define CODEX_PRESS_KEY  _IOW(CODEX_IOC_MAGIC, 4, int)                 /* press one key slot               */
#define CODEX_REMAP_KEY  _IOW(CODEX_IOC_MAGIC, 5, struct codex_remap)  /* point a key at a new keystroke   */
#define CODEX_GET_KEY    _IOR(CODEX_IOC_MAGIC, 6, int)                 /* drain the next queued keystroke  */
#define CODEX_RESET      _IO(CODEX_IOC_MAGIC, 7)                       /* return every field to default    */

#endif /* CODEXMICRO_H */
