#!/usr/bin/env bash
# Run idf.py with the ESP-IDF v6.0.3 environment used by avocatOS.
#   ./tools/idf.sh build | flash | monitor ...
set -euo pipefail
export IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf-v6.0.3}"
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf6.0_py3.12_env}"
# shellcheck disable=SC1091
source "$IDF_PATH/export.sh" >/dev/null 2>&1
cd "$(dirname "$0")/.."
exec idf.py "$@"
