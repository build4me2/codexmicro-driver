/**************************************************************
* Class::  CSC-415-01 Summer 2026
* Name:: Manisha Chand
* Student ID:: 924844476
* GitHub-Name:: build4me2
* Project:: Assignment 6 – Device Driver
*
* File:: uinput_bridge.h
*
* Description:: Small user-space helper for delivering keystrokes into the
* system input layer through a virtual keyboard. Shared by every program that
* needs a Codex Micro key press to become a real key event, so the virtual
* keyboard is created and driven in exactly one place.
*
**************************************************************/

#ifndef UINPUT_BRIDGE_H
#define UINPUT_BRIDGE_H

/* Create a virtual keyboard the OS treats as real hardware. Returns an open
 * handle to drive it, or -1 on failure. */
int uinput_open(void);

/* Replay one keystroke (press then release) so the OS delivers it as a real
 * key event to the focused window or console. */
void uinput_inject(int handle, int keycode);

/* Remove the virtual keyboard, leaving no lingering input device. */
void uinput_close(int handle);

#endif /* UINPUT_BRIDGE_H */
