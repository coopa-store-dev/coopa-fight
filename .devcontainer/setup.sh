#!/usr/bin/env bash
# Codespaces / Dev Containers 第一次打开时跑:装最新的 Coopa SDK,先编好 SDL2(第一次 coopa run 就快了)。
set -euo pipefail
url="https://github.com/coopa-store-dev/coopa-sdk/releases/latest/download"
tmp="$(mktemp -d)"
curl -fsSL -o "${tmp}/coopa-sdk.tar.gz" "${url}/coopa-sdk.tar.gz"
curl -fsSL -o "${tmp}/coopa-sdk.tar.gz.sha256" "${url}/coopa-sdk.tar.gz.sha256"
(cd "${tmp}" && sha256sum -c coopa-sdk.tar.gz.sha256)
rm -rf /opt/coopa-sdk
tar -xzf "${tmp}/coopa-sdk.tar.gz" -C /opt
ln -sf /opt/coopa-sdk/bin/coopa /usr/local/bin/coopa
embuilder build sdl2 zlib > /dev/null
coopa version
