# Firmware: light the agent keys from the host

`codexmicro_agent_keys.c` is the QMK side of the bridge. It receives the host's
status reports over Raw HID and colors the "agent keys." It works on any
QMK/VIA board with per-key RGB (Work Louder Micro, DOIO, a DIY/3D-printed
build, …).

## Prerequisites

- A QMK/VIA macropad with RGB Matrix
- [QMK CLI](https://docs.qmk.fm/#/newbs_getting_started) (`qmk setup`)

## Add it to a keymap

1. Start from a keymap for your board (copy the `default` one):
   ```bash
   qmk new-keymap -kb work_louder/micro -km codexmicro   # example board
   ```
2. Copy `codexmicro_agent_keys.c` into that keymap folder.
3. Add to the keymap's `rules.mk`:
   ```make
   RAW_ENABLE = yes
   RGB_MATRIX_ENABLE = yes
   SRC += codexmicro_agent_keys.c
   ```
4. **Edit `agent_led[]`** in `codexmicro_agent_keys.c` to the RGB Matrix LED
   indices of your six status keys. (Cycle colors in VIA, or see your board's
   `g_led_config` to find indices.)

## Build and flash

```bash
qmk flash -kb work_louder/micro -km codexmicro
```

## Verify it end to end

1. Find the device's Raw HID node:
   ```bash
   ls /dev/hidraw*        # and: udevadm info -q property /dev/hidrawN | grep -i id_
   ```
2. Run the daemon against it and send a status:
   ```bash
   ./codexmicrod --backend hidraw --device /dev/hidrawN &
   codexmicro-notify 0 thinking     # the first agent key turns blue
   codexmicro-notify 0 done         # turns green
   ```

If the keys respond, connect an LLM adapter (see `../adapters/`) and the keys
will track your agent automatically.

## Notes

- The protocol constants at the top of `codexmicro_agent_keys.c` must match
  `../codexmicro_wire.h`. They rarely change; if you edit one, edit both.
- The keys already send normal keystrokes; this only adds the status colors, so
  your existing keymap and VIA remapping keep working.
