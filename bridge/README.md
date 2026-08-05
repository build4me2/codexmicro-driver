# Codex Micro Bridge

Drive a real QMK macropad's RGB "agent keys" from **any** LLM's live agent
status, on Linux. The device's keys already work with any app (standard HID);
this closes the remaining gap — the status lights — without tying them to one
vendor.

```
agent runner ──hook──► codexmicro-notify <agent> <status>
                             │ (unix socket)
                             ▼
                    codexmicrod ──32-byte report──► /dev/hidraw* ──► QMK raw_hid_receive() ──► RGB
```

## Build

```bash
make          # builds codexmicrod and codexmicro-notify
```

## Run the daemon

```bash
# no hardware — just print what would be sent (great for trying it out):
./codexmicrod --backend loopback

# drive the existing virtual device (/dev/codexmicro) so the dashboard shows it:
sudo ./codexmicrod --backend virtual

# drive a real QMK device:
./codexmicrod --backend hidraw --device /dev/hidraw3
```

## Report status

From any script or hook:

```bash
codexmicro-notify 0 thinking      # agent 0 is working
codexmicro-notify 0 needs_input   # waiting on the user
codexmicro-notify 0 done          # finished
codexmicro-notify 1 error         # a second agent failed
```

Statuses: `idle`, `thinking`, `needs_input`, `done`, `error` (case-insensitive).

## Connect an LLM

Adapters wire an agent runner's lifecycle events to `codexmicro-notify` — no
per-LLM code, just its own hook system. See [`adapters/claude-code/`](adapters/claude-code/).

## Protocol

The wire contract (status vocabulary, default colors, and the 32-byte Raw HID
report the firmware expects) lives in [`codexmicro_wire.h`](codexmicro_wire.h),
shared by the daemon, the client, and the firmware side.

| Component | Role |
| --- | --- |
| `codexmicro_wire.h` | Shared contract: status vocabulary, colors, report format |
| `codexmicrod.c` | Daemon: socket intake → color → device backend |
| `codexmicro_notify.c` | One-shot client adapters call to report status |
| `adapters/` | Per-LLM hook configs (e.g. Claude Code) |
