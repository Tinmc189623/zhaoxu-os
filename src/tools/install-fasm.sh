#!/usr/bin/env bash
# 把 Ubuntu 仓库的 fasm 解压到本脚本所在目录，无需 root
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here"
apt-get download fasm >/dev/null 2>&1
deb="$(ls fasm_*.deb | head -1)"
dpkg -x "$deb" root
install -m 755 root/usr/bin/fasm "$here/fasm"
rm -rf root "$deb"
"$here/fasm" | head -2 || true
