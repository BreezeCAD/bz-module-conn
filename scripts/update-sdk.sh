#!/usr/bin/env bash
# Обновить sdk/bz_module.h из BreezeCAD.
#   scripts/update-sdk.sh            — из ветки main
#   scripts/update-sdk.sh v0.2.6     — из выпуска (тега)
# После обновления проверьте BZ_MODULE_ABI_VERSION: если она выросла,
# модуль надо пересобрать и, возможно, поднять "abiVersion" в module.json.
set -euo pipefail
ref="${1:-main}"
cd "$(dirname "$0")/.."
curl -fsSL "https://raw.githubusercontent.com/BreezeCAD/breezecad/${ref}/src/modules/bz_module.h" -o sdk/bz_module.h
printf 'bz_module.h из BreezeCAD/breezecad, %s, %s\n' "$ref" "$(date +%Y-%m-%d)" > sdk/SOURCE
grep -m1 'BZ_MODULE_ABI_VERSION ' sdk/bz_module.h
