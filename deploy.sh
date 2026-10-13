#!/usr/bin/env bash
# Sync this project to the Raspberry Pi, then optionally run a command there.
#
#   ./deploy.sh                                   # sync only
#   ./deploy.sh venv/bin/python tests/test_button.py
#   ./deploy.sh venv/bin/python pdd_inference.py --benchmark 20
#
# The Pi is reached through the "pdd" entry in ~/.ssh/config (override with PDD_HOST=...).
# venv/, captures/, logs/ and tests/out/ on the Pi are never touched by the sync.
set -euo pipefail

HOST="${PDD_HOST:-pdd}"
DEST="poultry_pi"
cd "$(dirname "$0")"

rsync -az --delete \
  --exclude '.git/' --exclude '.vscode/' --exclude '__pycache__/' \
  --exclude 'venv/' --exclude 'captures/' --exclude 'logs/' --exclude 'tests/out/' \
  ./ "$HOST:$DEST/"
echo "Synced to $HOST:~/$DEST"

if [ "$#" -gt 0 ]; then
  # -t gives the remote command a terminal so Ctrl+C stops it on the Pi too
  ssh -t "$HOST" "cd ~/$DEST && $*"
fi
