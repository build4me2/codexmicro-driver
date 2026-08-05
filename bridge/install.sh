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

echo "Installing binaries      -> $BINDIR"
mkdir -p "$BINDIR"
install -m 0755 codexmicrod codexmicro-notify "$BINDIR/"

echo "Installing systemd unit  -> $UNITDIR/codexmicrod.service"
mkdir -p "$UNITDIR"
install -m 0644 systemd/codexmicrod.service "$UNITDIR/"

cat <<EOF

Installed. Remaining steps (these need root or your input):

  1. Device access (root, once):
       - edit udev/99-codexmicro.rules with your device's idVendor/idProduct
         (find via: udevadm info -q property /dev/hidrawN | grep -i id_)
       - sudo install -m 0644 udev/99-codexmicro.rules /etc/udev/rules.d/
       - sudo udevadm control --reload && sudo udevadm trigger
       - replug the device (this creates /dev/codexmicro-pad)

  2. Choose backend/device:
       - edit ExecStart in $UNITDIR/codexmicrod.service
         (default drives a real pad; use "--backend loopback" to try with none)

  3. Start the daemon:
       systemctl --user daemon-reload
       systemctl --user enable --now codexmicrod

  4. Connect your LLM:
       - merge adapters/claude-code/hooks.json into ~/.claude/settings.json
       - make sure $BINDIR is on your PATH so codexmicro-notify is found

EOF
