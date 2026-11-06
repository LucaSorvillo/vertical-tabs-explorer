# Esplora file terminato a forza (come un crash): la finestra va ricordata con TUTTE le sue schede.
# Solo un explorer.exe di prova (/separate) con una finestra di prova di 3 schede.
param([int]$Rounds = 3)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'TestKit.ps1')
if (-not ('KillKit' -as [type])) {
    Add-Type -Namespace '' -Name KillKit -MemberDefinition '[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid); [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, System.IntPtr t);'
}
function PidOf([int64]$H) { $p = 0; [void][KillKit]::GetWindowThreadProcessId([IntPtr]$H, [ref]$p); $p }
$shellPid = PidOf ([int64][KillKit]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero))
$settingsFile = Join-Path $XpxData 'settings.ini'
# Prima del try: con un'altra prova aperta (dev-filter.txt) la pulizia del finally rimetterebbe le SUE impostazioni.
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente: prima chiudere l''altra prova' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userWindows = Get-UserWindowCount
$T = $XpxTestRoot
$ok = 0
try {
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    'A', 'B', 'C' | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $T $_) | Out-Null }
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1'
    Set-XpxOption 'Debug' 'VerboseLog' '1'
    Start-Tray
    Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000 | Out-Null
    for ($round = 1; $round -le $Rounds; $round++) {
        Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
        Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
        $w = @(Get-TestWindows)[0]
        $sepPid = PidOf $w
        if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "non è un processo separato ($sepPid)" }
        Add-TestTab $w (Join-Path $T 'B')
        Add-TestTab $w (Join-Path $T 'C')
        Wait-Until { (Format-DevSession) -eq '[A,B,*C]' } 8000 | Out-Null
        Start-Sleep -Milliseconds 1500
        $before = (Get-Content (Join-Path $XpxData 'logs\vtabsexplorer.log') -Encoding UTF8).Count
        Stop-Process -Id $sepPid -Force
        Wait-Until { (Get-XpxLog 6) -match 'Esplora file chiuso' } 8000 | Out-Null
        Start-Sleep -Milliseconds 500
        $lines = Get-Content (Join-Path $XpxData 'logs\vtabsexplorer.log') -Encoding UTF8 | Select-Object -Skip $before
        $closed = $lines | Where-Object { $_ -match 'Esplora file chiuso' } | Select-Object -Last 1
        $session = Format-DevSession
        $good = $session -eq '[A,B,*C]'
        if ($good) { $ok++ }
        "giro {0}: sessione dopo la chiusura forzata {1} -> {2}" -f $round, $session, $(if ($good) { 'OK' } else { 'SCHEDE PERSE' })
        $lines | Where-Object { $_ -match 'congelato|chiusa|chiuso|ricordat' } | ForEach-Object { '    ' + ($_ -replace '^2026-\d\d-\d\d ', '') -replace [regex]::Escape($T + '\'), '' }
    }
    "risultato: $ok su $Rounds"
}
finally {
    Close-TestWindows
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $XpxDevFilter -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Stop-Tray; Start-Tray
    "processi explorer: " + ((Get-Process explorer | ForEach-Object { "$($_.Id)" }) -join ', ') + " (barra: $shellPid); finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows) + ", schede dell'utente: $(Get-UserTabCount)"
}
