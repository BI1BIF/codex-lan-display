$ErrorActionPreference = 'Stop'
Enable-ScheduledTask -TaskName 'Codex LAN Display' | Out-Null
Start-ScheduledTask -TaskName 'Codex LAN Display'
