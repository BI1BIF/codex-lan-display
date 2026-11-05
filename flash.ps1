param(
    [Parameter(Mandatory)][ValidatePattern('^COM[0-9]+$')][string]$Port,
    [string]$BuildRoot = (Join-Path $env:PUBLIC 'CodexDisplayBuild'),
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$Output = Join-Path $BuildRoot 'output'
$Files = @('codex-lan-sketch.ino.bootloader.bin','codex-lan-sketch.ino.partitions.bin','boot_app0.bin','codex-lan-sketch.ino.bin')
foreach ($File in $Files) {
    if (!(Test-Path -LiteralPath (Join-Path $Output $File))) { throw "Missing $File. Run build.ps1 first." }
}
# This board's custom layout puts OTA metadata at 0x810000, not 0xe000.
& $Python -m esptool --chip esp32s3 --port $Port --baud 460800 write-flash --flash-mode keep --flash-freq keep --flash-size 16MB `
    0x0 "$Output/$($Files[0])" 0x8000 "$Output/$($Files[1])" `
    0x810000 "$Output/$($Files[2])" 0x10000 "$Output/$($Files[3])"
if ($LASTEXITCODE) { throw 'Flash failed.' }
