#!/bin/bash
# Downloads the latest pluginval release (macOS) into tools/.
set -euo pipefail
cd "$(dirname "$0")"
URL="https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip"
echo "Downloading $URL"
curl -fL --retry 3 -o pluginval_macOS.zip "$URL"
rm -rf pluginval.app
unzip -q -o pluginval_macOS.zip
rm pluginval_macOS.zip
echo "Installed: $(pwd)/pluginval.app"
echo "Run: tools/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/Components/LENS.component"
