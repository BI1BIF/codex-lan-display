param(
    [string]$Output = (Join-Path $env:PUBLIC 'CodexDisplayPackage'),
    [string]$Python = (Join-Path $PSScriptRoot 'companion/.venv/Scripts/python.exe')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Output | Out-Null
$Output = (Resolve-Path $Output).Path
Push-Location $PSScriptRoot
try {
    $Settings = (Resolve-Path 'companion/codex_display/settings.html').Path
    & $Python -m PyInstaller --noconfirm --onefile --windowed --noupx `
        --name Codex-LAN-Display-Windows-x64 --hidden-import pystray._win32 `
        --add-data "${Settings}:companion/codex_display" --paths . `
        --distpath $Output --workpath "$Output/work" --specpath $Output package-windows.py
    if ($LASTEXITCODE) { throw 'Windows packaging failed.' }
} finally {
    Pop-Location
}
