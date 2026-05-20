# scripts/setup_fmod.ps1
#
# ForgeEngine FMOD SDK setup helper — Windows
#
# FMOD cannot be downloaded automatically (requires a free fmod.com account).
# This script validates that the SDK is correctly placed and tells you exactly
# what's missing if it isn't.

$RepoRoot  = Split-Path -Parent $PSScriptRoot
$FmodDir   = if ($env:FMOD_ROOT) { $env:FMOD_ROOT } else { Join-Path $RepoRoot "third_party\fmod" }

Write-Host ""
Write-Host "ForgeEngine — FMOD SDK Validator"
Write-Host "================================="
Write-Host "Looking for SDK at: $FmodDir"
Write-Host ""

$Missing = 0
function Check-Path($RelPath, $Label) {
    $Full = Join-Path $FmodDir $RelPath
    if (Test-Path $Full) {
        Write-Host ("  [OK]  " + $Label) -ForegroundColor Green
    } else {
        Write-Host ("  [!!]  " + $Label + "  ->  " + $Full) -ForegroundColor Red
        $script:Missing++
    }
}

Write-Host "Core API headers:"
Check-Path "api\core\inc\fmod.hpp"       "fmod.hpp"
Check-Path "api\core\inc\fmod_common.h"  "fmod_common.h"

Write-Host ""
Write-Host "Studio API headers:"
Check-Path "api\studio\inc\fmod_studio.hpp"  "fmod_studio.hpp"
Check-Path "api\studio\inc\fmod_studio.h"    "fmod_studio.h"

Write-Host ""
Write-Host "Windows x64 libraries:"
Check-Path "api\core\lib\x64\fmod_vc.lib"         "fmod_vc.lib"
Check-Path "api\core\lib\x64\fmod.dll"             "fmod.dll"
Check-Path "api\studio\lib\x64\fmodstudio_vc.lib"  "fmodstudio_vc.lib"
Check-Path "api\studio\lib\x64\fmodstudio.dll"     "fmodstudio.dll"

Write-Host ""

if ($Missing -eq 0) {
    Write-Host "All good! FMOD SDK is correctly placed." -ForegroundColor Green
    Write-Host "Configure CMake normally and FMOD will be found automatically."
    Write-Host ""
} else {
    Write-Host "$Missing item(s) missing." -ForegroundColor Red
    Write-Host ""
    Write-Host "How to fix:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  1. Create a free account at https://www.fmod.com/"
    Write-Host "  2. Go to https://www.fmod.com/download"
    Write-Host "  3. Download the 'FMOD Engine' SDK for Windows"
    Write-Host "     (NOT just FMOD Studio — you need the Engine/API package)"
    Write-Host "  4. Extract the archive so the structure looks like:"
    Write-Host ""
    Write-Host "       $FmodDir\"
    Write-Host "         api\"
    Write-Host "           core\"
    Write-Host "             inc\   <- headers go here"
    Write-Host "             lib\   <- libraries go here"
    Write-Host "           studio\"
    Write-Host "             inc\   <- headers go here"
    Write-Host "             lib\   <- libraries go here"
    Write-Host ""
    Write-Host "  5. Re-run this script to verify, then run CMake."
    Write-Host ""
    Write-Host "  Tip: If your SDK is installed elsewhere, set the FMOD_ROOT"
    Write-Host "  environment variable before running CMake:"
    Write-Host "    `$env:FMOD_ROOT = 'C:\path\to\fmod'; cmake .."
    Write-Host ""
    Write-Host "  To build ForgeEngine without audio, configure with:"
    Write-Host "    cmake .. -DFORGE_AUDIO_FMOD=OFF"
    Write-Host ""
    exit 1
}
