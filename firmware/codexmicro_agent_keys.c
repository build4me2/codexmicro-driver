/**************************************************************
* File:: codexmicro_agent_keys.c
*
* Description:: QMK firmware side of the Codex Micro bridge. Drop this into a
* keymap so a QMK macropad lights its "agent keys" from status the host bridge
* sends over Raw HID. It receives a fixed report (magic, command, agent id, and
* an RGB color computed by the host), remembers the color for that key, and
* repaints the keys every frame. Anything not matching our magic is ignored so
* this coexists with other Raw HID users.
*
* Add to the keymap's rules.mk:
*     RAW_ENABLE = yes
*     RGB_MATRIX_ENABLE = yes
*     SRC += codexmicro_agent_keys.c
*
**************************************************************/

#include QMK_KEYBOARD_H
#include "raw_hid.h"

/*
 * Wire protocol — these MUST match bridge/codexmicro_wire.h on the host. Only
 * the fields the firmware needs are declared: the magic and command identify
 * our reports, and the color is precomputed by the host so the firmware stays
 * a simple, board-independent painter.
 */
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
 * Which RGB Matrix LED index each of the "agent keys" is on THIS board. This is
 * the one thing to edit per keyboard; find the indices with QMK's RGB Matrix
 * docs or by cycling colors in VIA. The array length also sets how many agents
 * the device can show, so extra host agents are simply ignored.
 */
static const uint8_t agent_led[] = { 0, 1, 2, 3, 4, 5 };
#define AGENT_COUNT (sizeof(agent_led) / sizeof(agent_led[0]))

/* The latest color for each agent key, repainted every frame so a status set
 * once keeps showing until it changes. */
static uint8_t agent_rgb[AGENT_COUNT][3];

/*
 * Receive a status report from the host bridge and store the color for the
 * named agent key. Reports that are too short, not ours, or name a key this
 * board does not have are ignored rather than acted on.
 */
void raw_hid_receive(uint8_t *data, uint8_t length)
{
	uint8_t agent;

	if (length < 7)
		return;
	if (data[CM_OFF_MAGIC] != CM_MAGIC || data[CM_OFF_CMD] != CM_CMD_STATUS)
		return;

	agent = data[CM_OFF_AGENT];
	if (agent >= AGENT_COUNT)
		return;

	agent_rgb[agent][0] = data[CM_OFF_R];
	agent_rgb[agent][1] = data[CM_OFF_G];
	agent_rgb[agent][2] = data[CM_OFF_B];
}

/*
 * Paint each agent key from its remembered color every frame. Returning false
 * lets the rest of the RGB matrix animate normally around the status keys.
 */
bool rgb_matrix_indicators_user(void)
{
	uint8_t a;

	for (a = 0; a < AGENT_COUNT; a++)
		rgb_matrix_set_color(agent_led[a],
				     agent_rgb[a][0], agent_rgb[a][1], agent_rgb[a][2]);
	return false;
}
