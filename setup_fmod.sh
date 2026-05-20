#!/usr/bin/env bash
# scripts/setup_fmod.sh
#
# ForgeEngine FMOD SDK setup helper — Linux and macOS
#
# FMOD cannot be downloaded automatically (requires a free fmod.com account).
# This script validates that the SDK is correctly placed and tells you exactly
# what's missing if it isn't.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/" && pwd)"
FMOD_DIR="${FMOD_ROOT:-${REPO_ROOT}/third_party/fmod}"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo ""
echo "ForgeEngine — FMOD SDK Validator"
echo "================================="
echo "Looking for SDK at: ${FMOD_DIR}"
echo ""

MISSING=0
check() {
    local path="$1"
    echo "${FMOD_DIR}/${path}"
    local label="$2"
    if [ -e "${FMOD_DIR}/${path}" ]; then
        echo -e "  ${GREEN}✔${NC}  ${label}"
    else
        echo -e "  ${RED}✘${NC}  ${label}  →  ${FMOD_DIR}/${path}"
        MISSING=$((MISSING + 1))
    fi
}

echo "Core API headers:"
check "api/core/inc/fmod.hpp"        "fmod.hpp"
check "api/core/inc/fmod_common.h"   "fmod_common.h"

echo ""
echo "Studio API headers:"
check "api/studio/inc/fmod_studio.hpp"    "fmod_studio.hpp"
check "api/studio/inc/fmod_studio.h"      "fmod_studio.h"

echo ""
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "macOS libraries:"
    check "api/core/lib/libfmod.dylib"         "libfmod.dylib"
    check "api/studio/lib/libfmodstudio.dylib" "libfmodstudio.dylib"
else
    echo "Linux libraries (x86_64):"
    check "api/core/lib/x86_64/libfmod.so"         "libfmod.so"
    check "api/studio/lib/x86_64/libfmodstudio.so" "libfmodstudio.so"
fi

echo ""

if [ "${MISSING}" -eq 0 ]; then
    echo -e "${GREEN}All good! FMOD SDK is correctly placed.${NC}"
    echo "Configure CMake normally and FMOD will be found automatically."
    echo ""
else
    echo -e "${RED}${MISSING} item(s) missing.${NC}"
    echo ""
    echo -e "${YELLOW}How to fix:${NC}"
    echo ""
    echo "  1. Create a free account at https://www.fmod.com/"
    echo "  2. Go to https://www.fmod.com/download"
    echo "  3. Download the 'FMOD Engine' SDK for your platform"
    echo "     (NOT just FMOD Studio — you need the Engine/API package)"
    echo "  4. Extract the archive so the structure looks like:"
    echo ""
    echo "       ${FMOD_DIR}/"
    echo "         api/"
    echo "           core/"
    echo "             inc/   ← headers go here"
    echo "             lib/   ← libraries go here"
    echo "           studio/"
    echo "             inc/   ← headers go here"
    echo "             lib/   ← libraries go here"
    echo ""
    echo "  5. Re-run this script to verify, then run CMake."
    echo ""
    echo "  Tip: If your SDK is installed elsewhere, set the FMOD_ROOT"
    echo "  environment variable before running CMake:"
    echo "    export FMOD_ROOT=/path/to/your/fmod && cmake .."
    echo ""
    echo "  To build ForgeEngine without audio, configure with:"
    echo "    cmake .. -DFORGE_AUDIO_FMOD=OFF"
    echo ""
    exit 1
fi
