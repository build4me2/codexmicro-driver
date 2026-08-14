#!/bin/bash
#**************************************************************
#* File:: install.sh
#*
#* Description:: Installs the Codex Micro bridge for the current Linux user:
#* builds the binaries, copies them to the user's bin directory, and installs
#* the systemd user service. The steps that need root or your input (the udev
#* rule, the device/backend choice, and the LLM hook) are printed at the end
#* rather than done automatically. Install locations can be overridden with the
#* BINDIR and UNITDIR environment variables.
#*
#**************************************************************

set -e
cd "$(dirname "$0")"

BINDIR="${BINDIR:-$HOME/.local/bin}"
UNITDIR="${UNITDIR:-$HOME/.config/systemd/user}"

echo "Building..."
make >/dev/null

echo "Installing programs      -> $BINDIR"
mkdir -p "$BINDIR"
install -m 0755 codexmicrod codexmicro-notify codexmicro-doctor codexmicro-flash codexmicro-run "$BINDIR/"

echo "Installing systemd unit  -> $UNITDIR/codexmicrod.service"
mkdir -p "$UNITDIR"
install -m 0644 systemd/codexmicrod.service "$UNITDIR/"

cat <<EOF

Installed. Remaining steps:

  1. Flash your pad (needs the QMK CLI + the device):
       codexmicro-flash                 # or: codexmicro-flash <board>

  2. Give the pad access (root, once):
       sudo codexmicro-doctor           # auto-detects it and writes the udev rule
       then replug the pad (it appears as /dev/codexmicro-pad)

  3. Start the daemon:
       systemctl --user daemon-reload
       systemctl --user enable --now codexmicrod
       (to try with no hardware, set "--backend loopback" in
        $UNITDIR/codexmicrod.service first)

  4. Connect your LLM:
       merge adapters/claude-code/hooks.json into ~/.claude/settings.json
       (ensure $BINDIR is on your PATH so codexmicro-notify is found)

EOF
