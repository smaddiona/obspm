#!/bin/bash
# Builds and installs obspm into the user's OBS plugins folder.
set -euo pipefail
cd "$(dirname "$0")/.."
if pgrep -x OBS >/dev/null; then echo "Close OBS first."; exit 1; fi
cmake --build --preset macos
D="$HOME/Library/Application Support/obs-studio/plugins"
mkdir -p "$D" && rm -rf "$D/obspm.plugin" && cp -R build_macos/RelWithDebInfo/obspm.plugin "$D/"
echo "Installed to $D/obspm.plugin"
