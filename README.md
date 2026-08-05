# Codex Micro — Virtual Linux Device Driver

A loadable Linux **character device driver** that simulates the OpenAI Codex
Micro — a desk controller for supervising AI coding agents — entirely in
software, and a user-space application that turns it into a live, color-coded
agent-control dashboard. There is no physical hardware: the driver *is* the
device. Unlike the real product, which is tied to one vendor's tooling, this
version is deliberately generic — it emits neutral control events and shows
status, so it can drive any program.

> An educational operating-systems project: a character device driver spanning
> the char-device and input subsystems, exercised by a real user-space app.

## Features

- **Character device** at `/dev/codexmicro` implementing `open`, `release`,
  `read`, `write`, and `ioctl`, with safe user/kernel data transfer.
- **Reads as device state, writes as agent status** — reading returns a text
  snapshot of the whole device; writing pushes a per-agent status update.
- **`ioctl` control interface** — set/get the reasoning dial, set the mode,
  press a key, remap a key, drain a queued keystroke, and reset.
- **Real keystroke injection** — a key press is delivered to the OS input layer
  through `uinput`, so the virtual device can type into any terminal.
- **Single-seam design** — all key presses funnel through one function, so the
  keystroke can later be emitted from the kernel instead of user space with no
  other change to the driver.
- **Color dashboard** — a user-space app that drives a simulated agent fleet and
  renders each agent's status in color, backend chosen from a config file.
- **Concurrency-safe** — shared device state is guarded by a lock, never held
  across a copy to or from user space.

## Architecture

```mermaid
flowchart LR
    A[dashboard / tools] -- ioctl, read, write --> B[/dev/codexmicro]
    B --> C[char device: state, dial, mode, agent status, key queue]
    A -- drains queued key --> D[uinput bridge]
    D --> E[OS input layer]
    E --> F[focused terminal / console]
    C -. emit_key seam .-> D
```

The driver holds all device state and a queue of pressed keys. The user-space
bridge drains that queue and replays each keystroke through `uinput`, which the
operating system delivers like a real keyboard.

## Quick start

### Prerequisites

- Linux with kernel headers for the running kernel
  (`sudo apt install build-essential linux-headers-$(uname -r)`)
- The `uinput` kernel module (`sudo modprobe uinput`) for keystroke injection
- Root privileges to load the module and access the device

### Build and load the driver

```bash
git clone https://github.com/build4me2/codexmicro-driver.git
cd codexmicro-driver/Module
make                          # produces codexmicro.ko
sudo insmod codexmicro.ko     # load the driver
sudo dmesg | tail             # "codexmicro: loaded and ready at /dev/codexmicro"
```

### Build and run the dashboard

```bash
cd ../Test
make                          # builds dashboard, ioctl_test, key_bridge
sudo ./dashboard              # root: the device node and uinput require it
```

Unload the driver when finished:

```bash
sudo rmmod codexmicro         # "codexmicro: unloaded"
```

## Usage

### Dashboard commands

At the `>` prompt:

| Command | Action |
| --- | --- |
| `s <id>` | Step an agent forward (idle → thinking → needs-input) |
| `a <id>` | Accept: press the ACCEPT key and finish the agent |
| `r <id>` | Reject: press the REJECT key and abandon the agent |
| `e <id>` | Mark an agent as errored |
| `d <0-100>` | Set the reasoning dial |
| `m <0-2>` | Set the mode (idle / steering / review) |
| `q` | Quit |

### Verifying keystroke injection

On a headless or SSH session there is no focused window, so inject events are
best observed directly on the virtual input device. In one terminal run the
dashboard (or `./key_bridge accept`); in another run `sudo evtest`, choose
**Codex Micro Virtual Keys**, then trigger a press — `evtest` shows the key
event (e.g. `KEY_ENTER`) arriving.

## ioctl interface

Defined in [`Module/codexmicro.h`](Module/codexmicro.h) and shared by the driver
and user programs:

| Request | Direction | Purpose |
| --- | --- | --- |
| `CODEX_SET_DIAL` / `CODEX_GET_DIAL` | write / read | Set or read the reasoning level (0–100) |
| `CODEX_SET_MODE` | write | Set the control-surface mode |
| `CODEX_PRESS_KEY` | write | Press a key slot |
| `CODEX_REMAP_KEY` | write | Point a key slot at a different keystroke |
| `CODEX_GET_KEY` | read | Drain the next queued keystroke |
| `CODEX_RESET` | — | Return every field to its default |

## Project structure

| Path | Responsibility |
| --- | --- |
| `Module/codexmicro.c` | The character device driver. |
| `Module/codexmicro.h` | Shared user/kernel control interface (ioctl numbers, enums). |
| `Module/Makefile` | Kernel-module build. |
| `Test/dashboard.c` | The agent-control dashboard application. |
| `Test/mock_agent.[ch]` | A simulated agent lifecycle backend. |
| `Test/uinput_bridge.[ch]` | Shared helper that replays keystrokes via `uinput`. |
| `Test/key_bridge.c` | Standalone tool: press a key and inject it. |
| `Test/ioctl_test.c` | Exercises and verifies the ioctl interface. |
| `Test/codexmicro.conf` | Backend selector for the dashboard. |
| `demo.sh` | Scripted end-to-end demonstration. |

## Notes and limitations

- The driver is a **virtual** device for learning; it models a control surface
  rather than talking to real hardware.
- Keystroke injection reaches the OS input layer, which feeds the focused window
  on a desktop or the active console on a text session — not a specific remote
  terminal. Use `evtest` to observe events on a headless machine.
- Loading a kernel module requires root and taints the kernel (expected for an
  out-of-tree, unsigned module).
- The device is intended for single-machine, single-user experimentation.
