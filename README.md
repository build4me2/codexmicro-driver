# Codex Micro Bridge

Show **any** LLM coding agent's live status on the RGB keys of a QMK macropad —
a real Codex Micro, a Work Louder Micro, a DOIO, or a 3D-printed DIY build — on
**Linux**, from the **terminal/CLI**.

The device's keys (accept, reject, push-to-talk, …) already work with any app,
because they are ordinary USB-HID keystrokes. The one thing that is locked down
is the **status lights**: on the real device they only light up via the ChatGPT
desktop app, and every open clone so far copies that same limitation — desktop
app only, macOS/Windows only, Codex only. This project is the missing piece: an
**open, Linux, any-LLM** path to those lights.

> Not a reverse-engineering of the closed desktop protocol, and not a kernel
> driver. It's a small user-space bridge plus open QMK firmware you flash
> yourself — so it works over SSH, in a headless box, and with whatever agent
> you run.

## How it works

```
 agent runner ──hook──► codexmicro-notify <agent> <status>
                              │ (unix socket)
                              ▼
                     codexmicrod ──32-byte report──► /dev/hidraw* ──► QMK raw_hid_receive() ──► RGB
```

Three independent components, joined by one shared contract
([`codexmicro_wire.h`](codexmicro_wire.h) — the status vocabulary + report
format):

1. **Firmware** — a ~30-line QMK handler you flash onto the pad.
2. **Daemon** — `codexmicrod`, a user-space C program that turns status into
   device reports over `hidraw`.
3. **Adapters** — per-LLM shims that report status by running `codexmicro-notify`.

## Status

| Piece | State |
| --- | --- |
| Wire protocol (`codexmicro_wire.h`) | ✅ done |
| Daemon + notify client | ✅ done (`loopback` + `hidraw` backends) |
| QMK firmware handler + flashing guide | ✅ done |
| Install / device-setup tooling | ✅ done (`install.sh`, `codexmicro-doctor`, `codexmicro-flash`) |
| Adapters: Claude Code, Codex CLI, generic (`codexmicro-run`) | ✅ done |
| More first-class adapters, `sim` backend, hardware bring-up | ⏳ see [ROADMAP](ROADMAP.md) |

The whole pipeline is testable with **no hardware** via the `loopback` backend.

## Quick start

```bash
# 1. build + install the daemon and tools (systemd user service)
./install.sh

# 2. flash your pad, then grant it access
codexmicro-flash                 # generates a keymap from the board default + flashes
sudo codexmicro-doctor           # auto-detects the pad and writes its udev rule

# 3. connect your LLM (example: Claude Code)
#    merge adapters/claude-code/hooks.json into ~/.claude/settings.json

# 4. start it
systemctl --user enable --now codexmicrod
```

Try it with no hardware first:

```bash
make
./codexmicrod --backend loopback &
codexmicro-notify 0 thinking     # prints the report it would send
codexmicro-notify 0 done
```

## Any LLM — how

The daemon and protocol are provider-neutral; the only place an LLM is named is
the adapter. Three tiers cover everything:

1. **First-class adapters** for runners with a hook system (Claude Code ✅,
   Codex CLI, …) — a tiny config maps lifecycle events to `codexmicro-notify`.
2. **Generic path** — *any* tool or script calls `codexmicro-notify 0 thinking`
   directly. One line makes any LLM work.
3. **Wrapper** — for tools with no hooks, a launcher infers status from the
   command's lifecycle.

See [`adapters/`](adapters/).

## Components

| Path | Role |
| --- | --- |
| `codexmicro_wire.h` | Shared contract: status vocabulary, colors, report format |
| `codexmicrod.c` | Daemon: socket intake → color → device backend |
| `codexmicro_notify.c` | One-shot client adapters call to report status |
| `firmware/` | QMK `raw_hid_receive` handler + flashing guide |
| `adapters/claude-code`, `adapters/codex-cli` | First-class hook adapters |
| `adapters/generic` | The universal path for any other LLM/tool |
| `codexmicro-run` | Wrapper that reports status for a hook-less tool |
| `codexmicro-flash` | Generate a keymap from the board default and flash |
| `codexmicro-doctor` | Auto-detect the pad and write its udev rule |
| `install.sh`, `systemd/`, `udev/` | User-service install + device access |

## License

TBD.
