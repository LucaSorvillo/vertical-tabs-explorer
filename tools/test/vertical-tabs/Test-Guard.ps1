# Fase 3 (senza il processo principale): la tray inietta da sola la mod nell'explorer.exe di prova
# (/separate), la sorveglia e, se termina con un codice d'errore, sospende le schede verticali.
# [VerticalTabs] SkipShellProcess=1: la tray non entra mai nel processo della barra delle applicazioni.
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'TestKit.ps1')
if (-not ('VtKit3' -as [type])) {
    Add-Type -Namespace '' -Name VtKit3 -MemberDefinition @'
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, System.IntPtr t);
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, uint pid);
[DllImport("kernel32.dll")] public static extern bool TerminateProcess(System.IntPtr h, uint code);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
'@
}
$script:fails = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { "  OK   $Name" } else { $script:fails++; "  FAIL $Name $Detail" }
}
function Get-Ini([string]$Section, [string]$Key) {
    # Stessa API di VTabsExplorer (gestisce da sé le scritture concorrenti della tray e della mod)
    if (-not ('IniKit' -as [type])) {
        Add-Type -Namespace '' -Name IniKit -MemberDefinition '[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern int GetPrivateProfileString(string s, string k, string d, System.Text.StringBuilder r, int n, string f);'
    }
    $sb = New-Object Text.StringBuilder 256
    [void][IniKit]::GetPrivateProfileString($Section, $Key, '', $sb, 256, (Join-Path $XpxData 'settings.ini'))
    if ($sb.Length -eq 0) { $null } else { $sb.ToString() }
}
function Test-ModIn([int]$ProcessId) {
    $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    $p -and [bool](@($p.Modules | Where-Object { $_.ModuleName -eq 'VTabsExplorerMod.dll' }).Count)
}
function Get-ContentX([int64]$Main) {
    $t = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
    $r = New-Object XpxKit+RECT; [void][XpxKit]::GetWindowRect($t, [ref]$r)
    $m = New-Object XpxKit+RECT; [void][XpxKit]::GetWindowRect([IntPtr]$Main, [ref]$m)
    $r.Left - $m.Left
}

$trayWnd = [VtKit3]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero)
$shellPid = 0; [void][VtKit3]::GetWindowThreadProcessId($trayWnd, [ref]$shellPid)
"processo della barra delle applicazioni: $shellPid"
$settingsFile = Join-Path $XpxData 'settings.ini'
# Prima del try: con un'altra prova aperta (dev-filter.txt) la pulizia del finally rimetterebbe le SUE impostazioni.
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente: prima chiudere l''altra prova' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userWindows = Get-UserWindowCount
$T = $XpxTestRoot
$logFile = Join-Path $XpxData 'logs\vtabsexplorer.log'
$sepPid = 0
try {
    Enter-VtIsolation # le finestre dell'utente restano come sono per tutta la prova
    # Il processo principale ha la mod solo se l'utente ha già attivato le schede verticali: la prova non deve cambiarlo.
    $shellHadMod = Test-ModIn $shellPid
    "mod nel processo principale prima della prova: $shellHadMod"
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    'A', 'B', 'C' | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $T $_) | Out-Null }
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1' # prima di Enabled: mai il processo principale
    Set-XpxOption 'VerticalTabs' 'Suspended' '0'
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Set-XpxOption 'NavBar' 'Enabled' '0' # la barra delle cartelle ha la sua prova (Test-NavBar): qui senza
    $logBefore = (Get-Content $logFile -Encoding UTF8).Count
    Start-Tray
    Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000 | Out-Null

    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
    $w = @(Get-TestWindows)[0]
    Add-VtTestWindow $w
    [void][VtKit3]::GetWindowThreadProcessId([IntPtr]$w, [ref]$sepPid)
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "la finestra di prova non è in un processo separato ($sepPid): mi fermo" }
    "finestra di prova {0:x} nel processo separato $sepPid" -f $w
    Add-TestTab $w (Join-Path $T 'B')

    Check 'la tray inietta da sola la mod nel processo di prova' (Wait-Until { Test-ModIn $sepPid } 8000)
    Check '  e le schede verticali compaiono' (Wait-Until { (Get-ContentX $w) -gt 100 } 8000) "x contenuto $(Get-ContentX $w)"
    Start-Sleep -Milliseconds 2000
    Check 'il processo principale resta com''era (nessuna iniezione dalla prova)' ((Test-ModIn $shellPid) -eq $shellHadMod)

    # Crash simulato: il processo di prova termina con 0xC0000005 (accesso non valido)
    $h = [VtKit3]::OpenProcess(0x0001, $false, [uint32]$sepPid) # PROCESS_TERMINATE
    [void][VtKit3]::TerminateProcess($h, [uint32]3221225477)    # 0xC0000005
    [void][VtKit3]::CloseHandle($h)
    Check 'crash rilevato: schede verticali sospese' (Wait-Until { (Get-Ini 'VerticalTabs' 'Suspended') -eq '1' -and (Get-Ini 'VerticalTabs' 'Enabled') -eq '0' } 5000) "Enabled=$(Get-Ini 'VerticalTabs' 'Enabled') Suspended=$(Get-Ini 'VerticalTabs' 'Suspended')"
    $sepPid = 0
    $log = Get-Content $logFile -Encoding UTF8 | Select-Object -Skip $logBefore
    Check '  nel log: chiusura anomala e sospensione' ([bool]($log -match 'chiusura anomala') -and [bool]($log -match 'sospese per sicurezza'))
    Check '  in modalità prova nessun fumetto' ([bool]($log -match 'avviso all''utente non mostrato'))

    # Riaperta una finestra di prova: sospese, la tray NON inietta
    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'C')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
    $w2 = @(Get-TestWindows)[0]
    Add-VtTestWindow $w2
    $pid2 = 0; [void][VtKit3]::GetWindowThreadProcessId([IntPtr]$w2, [ref]$pid2)
    if ($pid2 -eq $shellPid -or $pid2 -eq 0) { throw "la seconda finestra di prova non è in un processo separato ($pid2): mi fermo" }
    $sepPid = $pid2
    Start-Sleep -Milliseconds 4000
    Check 'da sospese la tray non inietta' (-not (Test-ModIn $pid2))

    # Riattivazione dal menu della tray (stesso comando del clic sulla voce)
    $tray = [VtKit3]::FindWindow('VTabsExplorer.Tray', [IntPtr]::Zero)
    [void][XpxKit]::PostMessage($tray, 0x0111, [IntPtr]104, [IntPtr]::Zero) # WM_COMMAND, IdVerticalTabs
    Check 'riattivate dal menu: Enabled=1, Suspended=0' (Wait-Until { (Get-Ini 'VerticalTabs' 'Enabled') -eq '1' -and (Get-Ini 'VerticalTabs' 'Suspended') -eq '0' } 3000)
    Check '  e la tray inietta di nuovo' (Wait-Until { Test-ModIn $pid2 } 8000)
    Check '  schede verticali di nuovo attive' (Wait-Until { (Get-ContentX $w2) -gt 100 } 8000) "x contenuto $(Get-ContentX $w2)"

    # Chiusura normale (finestre chiuse, processo terminato da uno strumento: -1) = nessuna sospensione
    Close-TestWindows
    Start-Sleep -Milliseconds 800
    Stop-Process -Id $pid2 -Force -ErrorAction SilentlyContinue
    $sepPid = 0
    Start-Sleep -Milliseconds 2500
    Check 'chiusura forzata da uno strumento: non conta come crash' ((Get-Ini 'VerticalTabs' 'Suspended') -eq '0' -and (Get-Ini 'VerticalTabs' 'Enabled') -eq '1')
    Check 'alla fine il processo principale è ancora com''era' ((Test-ModIn $shellPid) -eq $shellHadMod)
    "--- log della tray:"
    Get-Content $logFile -Encoding UTF8 | Select-Object -Skip $logBefore | Where-Object { $_ -match 'Schede verticali|schede verticali|mod ' } | ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
    "--- processo della barra intatto: " + [bool](Get-Process -Id $shellPid -ErrorAction SilentlyContinue) + "; errori: $script:fails"
}
finally {
    Close-TestWindows
    Start-Sleep -Milliseconds 800
    if ($sepPid -and $sepPid -ne $shellPid) {
        $p = Get-Process -Id $sepPid -ErrorAction SilentlyContinue
        if ($p -and $p.MainWindowHandle -eq [IntPtr]::Zero) { Stop-Process -Id $sepPid -Force -ErrorAction SilentlyContinue }
    }
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $XpxDevFilter -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Exit-VtIsolation
    Stop-Tray; Start-Tray
    "processi explorer: " + ((Get-Process explorer | ForEach-Object { "$($_.Id)" }) -join ', ') + " (barra: $shellPid); finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows) + "; mod nel principale: " + (Test-ModIn $shellPid)
}
