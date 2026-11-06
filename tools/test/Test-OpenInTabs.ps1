<#
.SYNOPSIS
    Cartelle aperte da altri programmi in una scheda (come Chrome con i link): prova dal vivo.

.DESCRIPTION
    Modalità prova (dev-filter.txt su %TEMP%\xpx-test: la tray considera solo le finestre di prova) con dev-merge.txt
    (senza, in modalità prova la funzione è spenta: le altre prove aprono finestre che devono restare finestre). Con
    "external" nel file ogni finestra nuova conta come aperta da un altro programma (la prova non decide chi è in
    primo piano).
    Le cartelle si aprono come farebbe un programma:
    - explorer.exe <cartella> e explorer.exe /select,<file> (riga di comando, es. IntelliJ);
    - ShellExecute sulla cartella (es. OBS "Mostra le registrazioni");
    - SHOpenFolderAndSelectItems su un file (es. "Mostra nella cartella" di molti programmi).
    Controlla che:
    - con una finestra di prova aperta la cartella arrivi in una sua scheda nuova, attiva, e nessuna finestra in più
      resti aperta o velata; con un file da mostrare l'elemento è selezionato anche nella scheda;
    - la cartella iniziale (in modalità prova la cartella della prova) resti una finestra, come Win+E;
    - con più finestre vada in quella aperta o usata per ultima;
    - con la funzione spenta resti una finestra;
    - senza "external" e con una finestra di prova in primo piano (se Windows lo concede), una finestra nuova resti
      finestra: come Ctrl+N in Esplora file.
    Alla fine chiude le finestre di prova (l'ultima con la posizione della finestra dell'utente), rimette impostazioni e
    tray com'erano. Le finestre dell'utente non vengono toccate. Nessun input reale.
#>
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\TestKit.ps1"

if (-not ('XpxShowIn' -as [type])) {
    Add-Type -Namespace '' -Name XpxShowIn -MemberDefinition @'
[DllImport("shell32.dll", CharSet = CharSet.Unicode)] static extern int SHParseDisplayName(string name, IntPtr bindCtx, out IntPtr pidl, uint sfgaoIn, out uint sfgaoOut);
[DllImport("shell32.dll")] static extern int SHOpenFolderAndSelectItems(IntPtr pidlFolder, uint cidl, IntPtr[] apidl, uint flags);
[DllImport("shell32.dll")] static extern void ILFree(IntPtr pidl);
// Apre la cartella del file con il file selezionato (o seleziona il file in una finestra che mostra già la cartella).
public static int Show(string path) {
  IntPtr pidl; uint attributes;
  int hr = SHParseDisplayName(path, IntPtr.Zero, out pidl, 0, out attributes);
  if (hr != 0) return hr;
  try { return SHOpenFolderAndSelectItems(pidl, 0, null, 0); } finally { ILFree(pidl); }
}
'@
}

$results = [System.Collections.Generic.List[object]]::new()
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
    $results.Add([pscustomobject]@{ Test = $Name; Ok = $Ok; Dettagli = $Detail })
    Write-Host ('{0} {1}  {2}' -f $(if ($Ok) { '[OK]  ' } else { '[FAIL]' }), $Name, $Detail)
}
function Names([int64]$Hwnd) { (Get-TabPaths $Hwnd | ForEach-Object { Short $_ }) -join ',' }
function Visible-TestWindows { @(Get-TestWindows | Where-Object { [XpxKit]::IsWindowVisible([IntPtr]$_) -and -not [XpxAppearance]::IsVeiled([IntPtr]$_) }) }
function Veiled-Count { $n = 0; for ($h = [XpxKit]::FindWindowEx([IntPtr]::Zero, [IntPtr]::Zero, 'CabinetWClass', [IntPtr]::Zero); $h -ne [IntPtr]::Zero; $h = [XpxKit]::FindWindowEx([IntPtr]::Zero, $h, 'CabinetWClass', [IntPtr]::Zero)) { if ([XpxAppearance]::IsVeiled($h)) { $n++ } }; $n }
# Come un programma che apre una cartella (o mostra un file nella cartella) con la riga di comando
function Open-External([string]$Argument) { Start-Process explorer.exe -ArgumentList $Argument }
function Get-SelectedNames([int64]$Hwnd, [string]$Folder) {
    $tab = @(Get-ShellTabs | Where-Object { $_.Hwnd -eq $Hwnd -and $_.Path -eq $Folder }) | Select-Object -First 1
    if ($tab) { (@($tab.Item.Document.SelectedItems()) | ForEach-Object { $_.Name }) -join ',' } else { '' }
}
$logPath = Join-Path $XpxData 'logs\vtabsexplorer.log'
function Get-LogCount { if (Test-Path $logPath) { @(Get-Content $logPath -Encoding UTF8).Count } else { 0 } }
function Get-LogSince([int]$Start) { if (Test-Path $logPath) { @(Get-Content $logPath -Encoding UTF8 | Select-Object -Skip $Start) } }
$devMerge = Join-Path $XpxData 'dev-merge.txt'
if (-not ('XpxForeground' -as [type])) {
    Add-Type -Namespace '' -Name XpxForeground -MemberDefinition @"
[DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
[DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
[DllImport("user32.dll")] static extern bool AttachThreadInput(uint from, uint to, bool attach);
[DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
// Da un altro processo Windows blocca il cambio di primo piano: agganciato per un attimo all'input del thread che ha il
// primo piano, il cambio passa (chi chiama si assicura che sia una finestra di prova).
public static bool Activate(IntPtr target) {
  uint thread = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero), me = GetCurrentThreadId();
  bool attached = thread != 0 && thread != me && AttachThreadInput(me, thread, true);
  try { BringWindowToTop(target); SetForegroundWindow(target); }
  finally { if (attached) AttachThreadInput(me, thread, false); }
  return GetForegroundWindow() == target;
}
"@
}
# Porta in primo piano una finestra di prova, solo se in primo piano c'è già una finestra di prova (mai toccare il
# primo piano di altri programmi).
function Set-TestForeground([int64]$Hwnd) {
    $fg = [int64][XpxKit]::GetForegroundWindow()
    if ($fg -eq $Hwnd) { return $true }
    if (@(Get-TestWindows) -notcontains $fg) { return $false }
    [XpxForeground]::Activate([IntPtr]$Hwnd) | Out-Null
    Wait-Until { [int64][XpxKit]::GetForegroundWindow() -eq $Hwnd } 1500 | Out-Null
    Start-Sleep -Milliseconds 400
    [int64][XpxKit]::GetForegroundWindow() -eq $Hwnd
}

$T = $XpxTestRoot
$A, $B, $C, $D, $E, $F, $G, $H, $I = 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I' | ForEach-Object { Join-Path $T $_ }
$settingsFile = Join-Path $XpxData 'settings.ini'
if (Test-Path $XpxDevFilter) { throw 'C''è già un dev-filter.txt: un''altra prova è in corso.' }
$settingsBefore = if (Test-Path $settingsFile) { [IO.File]::ReadAllBytes($settingsFile) } else { $null }
$userWindows = Get-UserWindowCount
$userTabs = Get-UserTabCount
$userPlacement = Get-UserPlacement
if (-not $userPlacement) { $userPlacement = New-Placement 1 200 150 1200 800 }
$explorersBefore = @(Get-Process explorer | ForEach-Object { $_.Id })
$logStart = Get-LogCount

try {
    Close-TestWindows
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    $A, $B, $C, $D, $E, $F, $G, $H, $I | ForEach-Object { New-Item -ItemType Directory -Force $_ | Out-Null }
    Set-Content -LiteralPath (Join-Path $C 'Mostrami.txt') -Value 'prova' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $C 'Altro.txt') -Value 'prova' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $I 'Trovami.txt') -Value 'prova' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $I 'Altro.txt') -Value 'prova' -Encoding ASCII
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm -ErrorAction SilentlyContinue
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'Tabs' 'OpenInTabs' '1'
    Set-XpxOption 'Debug' 'VerboseLog' '1'
    Set-Content -LiteralPath $devMerge -Value 'external' -Encoding ASCII
    $trayLogStart = Get-LogCount
    Start-Tray
    Check 'Tray in modalità prova' (Wait-Until { (Get-LogSince $trayLogStart) -match 'Modalità prova' } 6000)

    # 1) Prima finestra di prova: nessuna finestra di prova in cui metterla, resta una finestra
    $w1 = Open-TestWindow $A
    Check 'Senza finestre (di prova) aperte: resta una finestra' (Wait-Until { @(Visible-TestWindows).Count -eq 1 } 6000) ("finestre di prova visibili: {0}" -f @(Visible-TestWindows).Count)
    Move-TestWindow $w1 80 80 1300 700
    Start-Sleep -Milliseconds 1500

    # 2) Cartella aperta da un altro programma (riga di comando): scheda nuova in fondo, attiva, nessuna finestra in più
    $t0 = Get-Date
    Open-External "`"$B`""
    $ok = Wait-Until { (Names $w1) -eq 'A,B' -and @(Get-TestWindows).Count -eq 1 } 8000
    $ms = [int]((Get-Date) - $t0).TotalMilliseconds
    Check 'explorer.exe <cartella>: in una scheda della finestra aperta' $ok ("schede [{0}] dopo {1} ms" -f (Names $w1), $ms)
    Start-Sleep -Milliseconds 800
    Check '  è la scheda attiva' ((Get-ActiveTabName $w1) -eq 'B') ("attiva: {0}" -f (Get-ActiveTabName $w1))
    Check '  nessuna finestra in più, nessuna velata' (@(Get-TestWindows).Count -eq 1 -and (Veiled-Count) -eq 0) ("finestre di prova {0}, velate {1}" -f @(Get-TestWindows).Count, (Veiled-Count))
    $fg = [int64][XpxKit]::GetForegroundWindow()
    Write-Host ("  (in primo piano la finestra con la scheda: {0})" -f ($fg -eq $w1))

    # 3) Mostra un file nella cartella (explorer /select): scheda sulla cartella, con il file selezionato
    Open-External "/select,`"$(Join-Path $C 'Mostrami.txt')`""
    $ok = Wait-Until { (Names $w1) -eq 'A,B,C' -and @(Get-TestWindows).Count -eq 1 } 8000
    Check 'explorer.exe /select,<file>: scheda sulla sua cartella' $ok ("schede [{0}]" -f (Names $w1))
    Wait-Until { (Get-SelectedNames $w1 $C) -eq 'Mostrami.txt' } 4000 | Out-Null
    Check '  con il file selezionato' ((Get-SelectedNames $w1 $C) -eq 'Mostrami.txt') ("selezionati: '{0}'" -f (Get-SelectedNames $w1 $C))
    Check '  nessuna finestra in più' (@(Get-TestWindows).Count -eq 1) ("finestre di prova {0}" -f @(Get-TestWindows).Count)

    # 4) ShellExecute sulla cartella, come OBS "Mostra le registrazioni"
    Start-Process -FilePath $H
    $ok = Wait-Until { (Names $w1) -eq 'A,B,C,H' -and @(Get-TestWindows).Count -eq 1 } 8000
    Check 'ShellExecute sulla cartella (come OBS): in una scheda' ($ok -and @(Get-TestWindows).Count -eq 1) ("schede [{0}], finestre di prova {1}" -f (Names $w1), @(Get-TestWindows).Count)
    Check '  è la scheda attiva' ((Get-ActiveTabName $w1) -eq 'H') ("attiva: {0}" -f (Get-ActiveTabName $w1))

    # 5) SHOpenFolderAndSelectItems su un file ("Mostra nella cartella" di molti programmi)
    $hr = [XpxShowIn]::Show((Join-Path $I 'Trovami.txt'))
    $ok = Wait-Until { (Names $w1) -eq 'A,B,C,H,I' -and @(Get-TestWindows).Count -eq 1 } 8000
    Check 'SHOpenFolderAndSelectItems: in una scheda' ($ok -and @(Get-TestWindows).Count -eq 1) ("hr 0x{0:X8}, schede [{1}], finestre di prova {2}" -f $hr, (Names $w1), @(Get-TestWindows).Count)
    Wait-Until { (Get-SelectedNames $w1 $I) -eq 'Trovami.txt' } 4000 | Out-Null
    Check '  con il file selezionato' ((Get-SelectedNames $w1 $I) -eq 'Trovami.txt') ("selezionati: '{0}'" -f (Get-SelectedNames $w1 $I))

    # 6) Cartella iniziale (qui la cartella della prova, come Home per Win+E): resta una finestra
    $beforeWindows = @(Get-TestWindows)
    Open-External "`"$T`""
    Wait-Until { @(Get-TestWindows | Where-Object { $beforeWindows -notcontains $_ }).Count -ge 1 } 8000 | Out-Null
    Start-Sleep -Milliseconds 1500
    $w2 = @(Get-TestWindows | Where-Object { $beforeWindows -notcontains $_ }) | Select-Object -First 1
    Check 'Cartella iniziale (come Win+E): resta una finestra' ($w2 -and [XpxKit]::IsWindowVisible([IntPtr]$w2) -and -not [XpxAppearance]::IsVeiled([IntPtr]$w2) -and (Names $w1) -eq 'A,B,C,H,I') ("finestra nuova: {0}; prima finestra [{1}]" -f [bool]$w2, (Names $w1))
    if ($w2) { Move-TestWindow $w2 140 160 1250 650; Start-Sleep -Milliseconds 800 }

    # 7) Con più finestre: in quella aperta per ultima
    Open-External "`"$D`""
    $ok = Wait-Until { (Names $w2) -match ',D$' -and @(Get-TestWindows).Count -eq 2 } 8000
    Check 'Più finestre: nella finestra aperta per ultima' ($ok -and (Names $w1) -eq 'A,B,C,H,I' -and @(Get-TestWindows).Count -eq 2) ("ultima [{0}], prima [{1}], finestre {2}" -f (Names $w2), (Names $w1), @(Get-TestWindows).Count)

    # 8) ...o in quella usata per ultima (se Windows lascia portare in primo piano la prima finestra)
    if (Set-TestForeground $w1) {
        Open-External "`"$E`""
        $ok = Wait-Until { (Names $w1) -eq 'A,B,C,H,I,E' } 8000
        Check 'Più finestre: nella finestra usata per ultima' ($ok -and (Names $w2) -notmatch 'E') ("prima [{0}], ultima aperta [{1}]" -f (Names $w1), (Names $w2))
    }
    else { Write-Host '  (prima finestra non portata in primo piano: caso "usata per ultima" saltato)' }

    # 9) Funzione spenta: resta una finestra
    Set-XpxOption 'Tabs' 'OpenInTabs' '0'
    Start-Sleep -Milliseconds 1200 # la tray rilegge le impostazioni
    $beforeWindows = @(Get-TestWindows)
    Open-External "`"$F`""
    $ok = Wait-Until { @(Get-TestWindows | Where-Object { $beforeWindows -notcontains $_ }).Count -ge 1 } 8000
    Start-Sleep -Milliseconds 1200
    Check 'Funzione spenta: resta una finestra' ($ok -and @(Get-TestWindows).Count -eq 3 -and (Veiled-Count) -eq 0) ("finestre di prova {0}, velate {1}" -f @(Get-TestWindows).Count, (Veiled-Count))
    Set-XpxOption 'Tabs' 'OpenInTabs' '1'

    # 10) Senza "external", con una finestra di prova in primo piano: una finestra nuova resta finestra (come Ctrl+N)
    Set-Content -LiteralPath $devMerge -Value 'on' -Encoding ASCII
    Start-Sleep -Milliseconds 1200
    if (Set-TestForeground $w1) {
        $beforeWindows = @(Get-TestWindows)
        Open-External "`"$G`""
        $ok = Wait-Until { @(Get-TestWindows | Where-Object { $beforeWindows -notcontains $_ }).Count -ge 1 } 8000
        Start-Sleep -Milliseconds 1200
        Check 'Con Esplora file in primo piano (come Ctrl+N): resta una finestra' ($ok -and (Names $w1) -notmatch 'G' -and (Veiled-Count) -eq 0) ("finestre di prova {0}, prima [{1}], velate {2}" -f @(Get-TestWindows).Count, (Names $w1), (Veiled-Count))
    }
    else { Write-Host '  (prima finestra non portata in primo piano: caso "Ctrl+N" saltato)' }

    "--- log della tray:"
    Get-LogSince $logStart | Where-Object { $_ -match 'Cartella aperta da|scheda|Velo:' } |
        ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '' -replace [regex]::Escape($T), '<prova>') }
}
catch {
    Check 'Esecuzione completa' $false ("{0} (riga {1})" -f $_.Exception.Message, $_.InvocationInfo.ScriptLineNumber)
}
finally {
    Remove-Item -LiteralPath $devMerge -ErrorAction SilentlyContinue
    Close-TestWindowsAs $userPlacement
    if ($settingsBefore) { [IO.File]::WriteAllBytes($settingsFile, $settingsBefore) }
    Remove-Item -LiteralPath $XpxDevFilter -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    Stop-Tray; Start-Tray
    Check 'Finestre dell''utente non toccate' ((Get-UserWindowCount) -eq $userWindows -and (Get-UserTabCount) -eq $userTabs) ("finestre {0}, schede {1}" -f (Get-UserWindowCount), (Get-UserTabCount))
    Check 'Nessuna finestra resta invisibile' ((Veiled-Count) -eq 0) ("velate: {0}" -f (Veiled-Count))
    $extra = @(Get-Process explorer | Where-Object { $explorersBefore -notcontains $_.Id })
    Write-Host ("  (explorer.exe nati durante la prova e ancora attivi: {0})" -f $extra.Count)
}

$failed = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ''
Write-Host ("Risultato: {0} test superati, {1} falliti." -f ($results.Count - $failed), $failed) -ForegroundColor $(if ($failed) { 'Red' } else { 'Green' })
exit $failed
