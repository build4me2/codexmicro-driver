# Implementation Plan / Roadmap

## Objective

An **open, Linux, any-LLM** bridge that shows a coding agent's live status on
the RGB keys of a QMK macropad — a real Codex Micro, a Work Louder Micro, a
DOIO, or a 3D-printed/reverse-engineered build — driven from the **terminal /
CLI**, over SSH, or headless. Provider-neutral: the same system works with
whatever LLM the user runs.

## Research & positioning (why this shape)

Grounded in the research (sources at the end):

1. **The keys already work everywhere.** Accept/reject/push-to-talk/etc. are
   standard USB-HID keystrokes — they reach any editor, terminal, or app on any
   OS. No work needed there.
2. **The locked part is the RGB status.** On the official device the lights only
   update through the **ChatGPT desktop app**. Critically, the existing open
   clones copy that exact limitation: the notable one (M5Stack Core2 firmware)
   *masquerades as a Codex Micro so the desktop app drives it* — **desktop-app
   only, macOS/Windows only, Codex only, no CLI, no Linux, no open protocol.**
3. **The pieces to do better are all open.** The hardware is QMK/VIA
   (DIY-buildable), QMK exposes **Raw HID** for host↔device data, `hidraw` gives
   Linux-native device access, and agent runners (Codex CLI, Claude Code) expose
   **lifecycle hooks** from which status can be read.

**Conclusion — our path:** ship our **own open QMK firmware** (our Raw HID
protocol) + a **Linux daemon** + **CLI adapters** that source status from any
agent's hooks. We deliberately do **not** reverse-engineer the closed desktop
RGB protocol — it is undocumented and would still be Codex-only. Users flash our
firmware, which *replaces* the desktop-masquerade firmware.

## Architecture — three components, one contract

```
 agent runner ──hook──► codexmicro-notify ──uds──► codexmicrod ──32B hidraw──► QMK raw_hid_receive() ──► RGB
   (any LLM)              (adapter shim)             (daemon)                     (our firmware)
```

- **Firmware** (QMK C, on the device): `firmware/codexmicro_agent_keys.c`.
- **Daemon** (user-space C, on the host): `codexmicrod` + `codexmicro_notify`.
- **Adapters** (per-LLM config/scripts): `adapters/`.
- **Contract**: `codexmicro_wire.h` — the status vocabulary
  (`idle/thinking/needs_input/done/error`), default colors, and the 32-byte
  report. Every component compiles/agrees against this one file.

## Design principles

- **Assemble, don't invent** — reuse QMK, VIA, `hidraw`, and the runners' own
  hook systems; the custom surface stays tiny.
- **Provider-neutral core** — an LLM is named only inside an adapter.
- **Testable without hardware** — the `loopback` backend exercises the full
  software path with no device.

## Done

| Item | Where |
| --- | --- |
| Wire protocol | `codexmicro_wire.h` |
| Daemon + notify (`loopback`, `hidraw`) | `codexmicrod.c`, `codexmicro_notify.c` |
| QMK firmware handler + guide | `firmware/` |
| Install + device setup | `install.sh`, `codexmicro-doctor`, `codexmicro-flash`, `systemd/`, `udev/` |
| Adapters: Claude Code, Codex CLI, generic + wrapper | `adapters/claude-code/`, `adapters/codex-cli/`, `adapters/generic/`, `codexmicro-run` |

## Remaining work

### Phase A — Any-LLM adapter layer
- ✅ **A1. Codex CLI adapter** — `adapters/codex-cli/` (UserPromptSubmit/PreToolUse→thinking, PermissionRequest→needs_input, Stop→done). Verified against loopback.
- ✅ **A2. Generic adapter** — `adapters/generic/`: the one-liner pattern any tool/script can use.
- ✅ **A3. Wrapper** — `codexmicro-run <cmd…>` reports thinking→done/error for a hook-less tool and passes the command's exit code through. Verified.
- **A4. More first-class adapters** as demand shows (Ollama, aider, Gemini CLI…).
- **A5. "Add your LLM in 2 minutes" guide.**

### Phase B — Hardware-free visual testing
- **B1. `sim` backend** — render the six keys as colored ANSI blocks in the
  terminal, so the daemon can be watched visually with no device (replaces the
  removed dashboard's role, without a kernel module). *Test: statuses paint the
  expected colors.*

### Phase C — Real-hardware bring-up (needs a pad, ~$60–80)
- **C1.** Flash + confirm colors on Work Louder Micro / DOIO; verify/adjust
  `agent_led[]` indices.
- **C2.** Prebuilt firmware (`.hex`/`.uf2`) for popular boards so users skip QMK.

### Phase D — Richer mapping
- **D1.** Multiple agents → multiple keys (id → key mapping + config).
- **D2.** Optional key→action: read `raw_hid_send` key events → adapter actions
  (accept → approve a tool call) for runners whose actions aren't a keystroke.
- **D3.** Dial/joystick → LLM parameters where the board has them.

### Phase E — Packaging & release
- **E1.** Versioned releases, a LICENSE, and CI (build the daemon, `-Wall
  -Wextra`; syntax-check firmware against QMK stubs; `shellcheck` the scripts).
- **E2.** Consider renaming the repo (`…-driver` → `…-bridge`) now that it is not
  a kernel driver.

## Testing strategy

- **Software pipeline:** the `loopback` backend end-to-end (and `sim` once B1
  lands) — no hardware.
- **C:** `-Wall -Wextra`. **Firmware:** syntax-check against QMK stub headers.
  **Scripts:** `bash -n` + `shellcheck`.
- Each phase ships with its own test; only Phase C needs a physical pad.

## Non-goals

- **No kernel driver** — a USB-HID device is handled by the generic HID driver;
  our logic is user-space (`hidraw`).
- **No reverse-engineering the closed desktop RGB protocol** — undocumented and
  Codex-only.
- **No Windows/macOS** — Linux-only, per the objective.

## Risks / unknowns

- Per-board `agent_led[]` indices — confirmed by eye on first flash (Phase C).
- Transport: USB (`hidraw`) first; Bluetooth HID output is a later maybe.
- Opaque tools with no status signal — the generic path and wrapper (A2/A3)
  cover all practical cases; a truly signal-less black box would need a one-line
  `notify` added by the user.

## Immediate next step

The any-LLM core (A1–A3) is done and verified. Next is **A5** (the "add your LLM
in 2 minutes" guide) and **B1** (a `sim` terminal backend for visual
hardware-free testing), then **Phase C** once a pad is on hand.

## Sources

- CNX Software — *M5Stack Core2 open-source Codex Micro clone* (desktop-app
  dependent, no Linux/CLI) —
  https://www.cnx-software.com/2026/07/18/m5stack-core2-gets-open-source-firmware-to-reproduce-openais-codex-micro-features/
- Tech Times — *Agent Keys only work with ChatGPT Desktop* —
  https://www.techtimes.com/articles/320670/20260716/openai-codex-micro-ships-today-agent-keys-only-work-chatgpt-desktop.htm
- QMK Firmware — *Raw HID* — https://docs.qmk.fm/features/rawhid
- libusb — *hidapi* — https://github.com/libusb/hidapi
- Claude Code hooks — https://claudefa.st/blog/tools/hooks/hooks-guide
- Codex CLI hooks — https://developers.openai.com/codex/hooks
