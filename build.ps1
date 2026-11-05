param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$BuildRoot = (Join-Path $env:PUBLIC 'CodexDisplayBuild'),
    [string]$ArduinoData = (Join-Path $env:LOCALAPPDATA 'Arduino15')
)
$ErrorActionPreference = 'Stop'
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
if ($BuildRoot -match '[^\x20-\x7E]') { throw 'Choose an ASCII-only -BuildRoot.' }
$Stage = Join-Path $BuildRoot 'codex-lan-sketch'
$Output = Join-Path $BuildRoot 'output'
$BootApp = Join-Path $ArduinoData 'packages/esp32/hardware/esp32/3.3.8/tools/partitions/boot_app0.bin'
if (!(Test-Path -LiteralPath $BootApp)) { throw 'Install Arduino ESP32 core 3.3.8, or specify -ArduinoData.' }
New-Item -ItemType Directory -Force $Stage,$Output | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/src" -Destination $Stage -Recurse -Force
Copy-Item -LiteralPath "$PSScriptRoot/lv_conf.h","$PSScriptRoot/partitions.csv" -Destination $Stage -Force
Set-Content -LiteralPath "$Stage/codex-lan-sketch.ino" -Value '// setup and loop are in src/main.cpp' -Encoding ascii
$Flags = '-I"' + $Stage.Replace('\','/') + '" -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE'
$Fqbn = 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB'
& $ArduinoCli compile --fqbn $Fqbn --build-path $Output --build-property "compiler.c.extra_flags=$Flags" --build-property "compiler.cpp.extra_flags=$Flags" $Stage
if ($LASTEXITCODE) { throw 'Firmware build failed.' }
Copy-Item -LiteralPath $BootApp -Destination $Output -Force
Write-Output "Firmware: $Output"
