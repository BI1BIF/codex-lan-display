$ErrorActionPreference = 'Stop'
$TaskName = 'Codex LAN Display'
$Python = Join-Path $PSScriptRoot 'companion\.venv\Scripts\pythonw.exe'
if (!(Test-Path -LiteralPath $Python)) {throw 'Install companion dependencies first'}
$Previous = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($Previous) {
    $BackupDir = Join-Path $PSScriptRoot 'local-backup'
    New-Item -ItemType Directory -Force $BackupDir | Out-Null
    $TaskBackup = Join-Path $BackupDir 'before-tray.xml'
    if (!(Test-Path -LiteralPath $TaskBackup)) {
        Export-ScheduledTask -TaskName $TaskName | Set-Content -LiteralPath $TaskBackup -Encoding unicode
    }
    Stop-ScheduledTask -TaskName $TaskName
    for ($Attempt=0; $Attempt -lt 20; $Attempt++) {
        if ((Get-ScheduledTask -TaskName $TaskName).State -ne 'Running') {break}
        Start-Sleep -Milliseconds 250
    }
}
$Listener = Get-NetTCPConnection -LocalPort 8738 -State Listen -ErrorAction SilentlyContinue
foreach ($ProcessIdValue in ($Listener.OwningProcess | Sort-Object -Unique)) {
    $ProcessInfo = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessIdValue"
    if ($ProcessInfo.CommandLine -notmatch '-m companion\.codex_display\.lan(?:\s|$)') {
        throw "Port 8738 belongs to another application; installation stopped."
    }
    & taskkill.exe /PID $ProcessIdValue /T /F | Out-Null
}
$BasePrefix = & (Join-Path $PSScriptRoot 'companion\.venv\Scripts\python.exe') -c 'import sys; print(sys.base_prefix)'
$GuiPython = Join-Path $BasePrefix 'pythonw.exe'
if (!(Test-Path -LiteralPath $GuiPython)) {throw 'GUI Python interpreter not found'}
$Launcher = Join-Path $PSScriptRoot 'companion\tray_launcher.pyw'
$Action = New-ScheduledTaskAction -Execute $GuiPython -Argument ('"' + $Launcher + '"') -WorkingDirectory $PSScriptRoot
$User = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$Trigger = New-ScheduledTaskTrigger -AtLogOn -User $User
$RecoveryTrigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) -RepetitionInterval (New-TimeSpan -Minutes 1)
$Principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive -RunLevel Limited
$Settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger @($Trigger, $RecoveryTrigger) -Principal $Principal -Settings $Settings -Description 'Codex 状态屏托盘服务；登录后启动，每分钟检查退出恢复，无命令行窗口。' -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName
Write-Output 'Installed and started Codex status display tray (windowless, starts at logon).'
