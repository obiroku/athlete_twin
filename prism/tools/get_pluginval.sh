#!/usr/bin/env bash
# Downloads pluginval (Tracktion, GPLv3 tool - used only as an external test
# runner, never linked) from its GitHub releases into tools/.
set -euo pipefail
cd "$(dirname "$0")"
case "$(uname -s)" in
  Darwin) asset=pluginval_macOS.zip ;;
  Linux)  asset=pluginval_Linux.zip ;;
  *) echo "unsupported OS"; exit 1 ;;
esac
url="https://github.com/Tracktion/pluginval/releases/latest/download/${asset}"
echo "Downloading ${url}"
curl -fsSL -o "${asset}" "${url}"
rm -rf pluginval pluginval.app
unzip -o -q "${asset}"
rm -f "${asset}"
if [ -d pluginval.app ]; then
  echo "pluginval: $(pwd)/pluginval.app/Contents/MacOS/pluginval"
else
  chmod +x pluginval
  echo "pluginval: $(pwd)/pluginval"
fi
