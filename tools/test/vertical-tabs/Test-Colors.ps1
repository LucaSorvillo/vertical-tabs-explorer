# Colori delle schede (schede verticali): menu "Colore scheda", aspetto della riga, file pubblicati dalla
# mod, colori seguiti nel riordino, colori rimessi (pending), sessione della tray e riapertura.
# Finestra di prova in un explorer.exe SEPARATO (/separate); la tray non entra nel processo principale
# (SkipShellProcess) e le finestre dell'utente restano come sono (Enter-VtIsolation).
# -Restore: anche "Riapri ora" con la finestra di prova chiusa (si riapre nel processo PRINCIPALE: lì
# la colonna e i colori compaiono solo se vi è caricata una mod con i colori).
# -ExpectRestoredColors: la finestra riaperta deve mostrare i colori (mod con i colori nel processo principale).
param([switch]$Restore, [switch]$ExpectRestoredColors, [switch]$NoShots)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'TestKit.ps1')
Add-Type -AssemblyName System.Drawing
if (-not ('ColorKit' -as [type])) {
    Add-Type -Namespace '' -Name ColorKit -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, System.IntPtr t);
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr dc, uint flags);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
public delegate bool EnumProc(System.IntPtr h, System.IntPtr p);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr p);
public static System.Collections.Generic.List<System.IntPtr> WindowsOf(uint pid) {
  var list = new System.Collections.Generic.List<System.IntPtr>();
  EnumWindows((h, p) => { uint q; GetWindowThreadProcessId(h, out q); if (q == pid) list.Add(h); return true; }, System.IntPtr.Zero);
  return list; }
'@
}
$script:fails = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { "  OK   $Name" } else { $script:fails++; "  FAIL $Name $Detail" }
}
function PidOf([int64]$H) { $p = 0; [void][ColorKit]::GetWindowThreadProcessId([IntPtr]$H, [ref]$p); $p }
# Get-PanelIsland: TestKit (contrassegno della mod)
function Get-PanelRows([int64]$Main) {
    $panel = Get-PanelIsland $Main
    if (-not $panel) { return @() }
    @($XAE::FromHandle([IntPtr]$panel).FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::ListItem))) |
            Sort-Object { $_.Current.BoundingRectangle.Y })
}
# Righe del pannello come "Nome:colore" (colore dallo stato per l'accessibilità, "Colore: Rosso")
function Format-PanelColors([int64]$Main) {
    (@(Get-PanelRows $Main | ForEach-Object {
                $status = $_.Current.ItemStatus
                $_.Current.Name + $(if ($status -match '^Colore: (.+)$') { ':' + $Matches[1] } else { '' })
            }) -join ',')
}
# Sessione di prova salvata dalla tray: "A:blue|B|C:green" (cartella e colore di ogni scheda, in ordine)
function Get-SessionColors {
    $parts = @()
    foreach ($line in $(if (Test-Path $XpxDevSession) { Read-DevSessionLines })) {
        if ($line -match '^tab (.+)$') { $parts += (Split-Path $Matches[1] -Leaf) }
        elseif ($line -match '^color (.+)$' -and $parts.Count) { $parts[-1] += ':' + $Matches[1] }
    }
    $parts -join '|'
}
function Send-ModCommand([string]$Text) {
    $file = Join-Path $XpxData 'mod-command.txt'
    [IO.File]::WriteAllText($file, $Text, [Text.Encoding]::ASCII)
    Wait-Until { -not (Test-Path $file) } 3000 | Out-Null
    Start-Sleep -Milliseconds 400
}
function Get-ColorFile([string]$Kind, [int64]$Main) { Join-Path (Join-Path $XpxData 'colors') ('{0}-{1:x}.txt' -f $Kind, $Main) }
function Read-ColorFile([string]$Kind, [int64]$Main) {
    $f = Get-ColorFile $Kind $Main
    if (Test-Path $f) { ((Get-Content $f -Encoding ASCII) | Where-Object { $_ }) -join ' ' } else { '-' }
}
function Get-Popup([uint32]$ProcessId, $Before) {
    @([ColorKit]::WindowsOf($ProcessId) | Where-Object { [XpxKit]::IsWindowVisible($_) -and $Before -notcontains [int64]$_ })
}
function Find-MenuItem($Roots, [string]$Name) {
    foreach ($r in $Roots) {
        $found = $XAE::FromHandle($r).FindFirst($XTS::Descendants, (New-Object Windows.Automation.AndCondition(
                    (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::MenuItem)),
                    (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, $Name)))))
        if ($found) { return $found }
    }
    $null
}
# Il menu contestuale nativo aperto dal pannello sulla riga indicata ha "Colore scheda" come ultima voce?
# (il menu si chiude con il comando di prova closemenu). Il sottomenu dei colori si prova con il mouse
# vero (Test-Mouse.ps1): l'"Espandi" di UI Automation su quella voce chiude tutto il menu.
function Test-ColorMenu([int64]$Main, [uint32]$ProcessId, [int]$Index) {
    $before = @([ColorKit]::WindowsOf($ProcessId) | Where-Object { [XpxKit]::IsWindowVisible($_) } | ForEach-Object { [int64]$_ })
    Send-ModCommand ('{0:x} menu {1}' -f $Main, $Index)
    Start-Sleep -Milliseconds 600
    $popups = Get-Popup $ProcessId $before
    $items = @()
    foreach ($p in $popups) {
        $items += @($XAE::FromHandle($p).FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::MenuItem))) | ForEach-Object { $_.Current.Name })
    }
    Send-ModCommand ('{0:x} closemenu' -f $Main)
    $closed = Wait-Until { @(Get-Popup $ProcessId $before).Count -eq 0 } 2000
    "    voci: " + ($items -join ' | ') + "; menu chiuso: $closed"
    $items.Count -ge 2 -and $items[-1] -eq 'Colore scheda' -and $items -contains 'Chiudi scheda' -and $closed
}
# "Colore scheda" -> colore, dal menu contestuale nativo aperto dal pannello sulla riga indicata
function Set-ColorFromMenu([int64]$Main, [uint32]$ProcessId, [int]$Index, [string]$ColorName) {
    $before = @([ColorKit]::WindowsOf($ProcessId) | Where-Object { [XpxKit]::IsWindowVisible($_) } | ForEach-Object { [int64]$_ })
    Send-ModCommand ('{0:x} menu {1}' -f $Main, $Index)
    Start-Sleep -Milliseconds 400
    $popups = Get-Popup $ProcessId $before
    $sub = Find-MenuItem $popups 'Colore scheda'
    if (-not $sub) { foreach ($p in $popups) { [void][ColorKit]::PostMessage($p, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) }; return $false }
    $sub.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
    Start-Sleep -Milliseconds 400
    $popups = Get-Popup $ProcessId $before
    $item = Find-MenuItem $popups $ColorName
    if (-not $item) { foreach ($p in $popups) { [void][ColorKit]::PostMessage($p, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) }; return $false }
    $patterns = $item.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName }
    if ($patterns -contains 'InvokePatternIdentifiers.Pattern') { $item.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
    else { $item.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select() }
    Start-Sleep -Milliseconds 400
    $true
}
function Save-MaskedShot([int64]$Main, [string]$Path) {
    $r = New-Object ColorKit+RECT; [void][ColorKit]::GetWindowRect([IntPtr]$Main, [ref]$r)
    $bmp = New-Object Drawing.Bitmap ($r.Right - $r.Left), ($r.Bottom - $r.Top)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc(); [void][ColorKit]::PrintWindow([IntPtr]$Main, $dc, 2); $g.ReleaseHdc($dc)
    $t = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
    while ($t -ne [IntPtr]::Zero) {
        $tr = New-Object ColorKit+RECT; [void][ColorKit]::GetWindowRect($t, [ref]$tr)
        $g.FillRectangle([Drawing.Brushes]::DimGray, $tr.Left - $r.Left, $tr.Top - $r.Top, $tr.Right - $tr.Left, $tr.Bottom - $tr.Top)
        $t = [XpxKit]::FindWindowEx([IntPtr]$Main, $t, 'ShellTabWindowClass', [IntPtr]::Zero)
    }
    $g.Dispose(); New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null; $bmp.Save($Path); $bmp.Dispose()
}

$shellPid = PidOf ([int64][ColorKit]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero))
$settingsFile = Join-Path $XpxData 'settings.ini'
# Prima del try: con un'altra prova aperta (dev-filter.txt) la pulizia del finally rimetterebbe le SUE impostazioni.
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente: prima chiudere l''altra prova' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userWindows = Get-UserWindowCount
$T = $XpxTestRoot
$modLog = Join-Path $XpxData 'logs\mod.log'
$shot = Join-Path $env:TEMP 'xpx-shots'
$sepPid = 0
$restored = 0
try {
    Enter-VtIsolation # le finestre dell'utente restano come sono per tutta la prova
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    'A', 'B', 'C' | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $T $_) | Out-Null }
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm, (Join-Path $XpxData 'mod-command.txt') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1' # prima di Enabled: la tray non entra mai nel processo principale
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Set-XpxOption 'NavBar' 'Enabled' '0' # la barra delle cartelle ha la sua prova (Test-NavBar): qui senza
    Set-XpxOption 'VerticalTabs' 'Collapsed' '0'
    Start-Tray
    Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000 | Out-Null

    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
    $w = @(Get-TestWindows)[0]
    Add-VtTestWindow $w
    $sepPid = PidOf $w
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "la finestra di prova non è in un processo separato ($sepPid): mi fermo" }
    "finestra di prova {0:x} nel processo separato $sepPid" -f $w
    Add-TestTab $w (Join-Path $T 'B')
    Add-TestTab $w (Join-Path $T 'C')
    if (-not (Wait-Until { ((@(Get-PanelRows $w) | ForEach-Object { $_.Current.Name }) -join ',') -eq 'A,B,C' } 20000)) { throw "pannello non pronto: $(Format-PanelColors $w)" }
    "--- controlli"

    # Il menu nativo aperto dal pannello ha "Colore scheda" in fondo; il colore si dà con il comando di prova
    # (stesso codice della voce del sottomenu)
    $menuOk = Test-ColorMenu $w $sepPid 1
    $menuOk | Where-Object { $_ -is [string] }
    Check 'menu contestuale: "Colore scheda" in fondo al menu di Esplora file' ([bool]($menuOk | Where-Object { $_ -is [bool] } | Select-Object -Last 1))
    Send-ModCommand ('{0:x} color 1 red' -f $w)
    Check '  la riga B è rossa' (Wait-Until { (Format-PanelColors $w) -eq 'A,B:Rosso,C' } 3000) (Format-PanelColors $w)
    Check '  colori pubblicati per la tray (con il titolo della scheda)' (Wait-Until { (Read-ColorFile 'live' $w) -eq 'count=3 1=red|B' } 3000) (Read-ColorFile 'live' $w)
    Check '  e nella sessione salvata dalla tray, sulla scheda giusta' (Wait-Until { (Get-SessionColors) -eq 'A|B:red|C' } 4000) (Get-SessionColors)

    # Comando di prova (come la voce del menu) e riordino: il colore segue la scheda
    Send-ModCommand ('{0:x} color 0 blue' -f $w)
    Check 'A blu' (Wait-Until { (Format-PanelColors $w) -eq 'A:Blu,B:Rosso,C' } 3000) (Format-PanelColors $w)
    Check '  sessione: A blu, B rosso' (Wait-Until { (Get-SessionColors) -eq 'A:blue|B:red|C' } 4000) (Get-SessionColors)
    if (-not $NoShots) { Start-Sleep -Milliseconds 400; Save-MaskedShot $w (Join-Path $shot 'colori.png') }
    Send-ModCommand ('{0:x} move 0 2' -f $w)
    Check 'riordino: i colori seguono le schede' (Wait-Until { (Format-PanelColors $w) -eq 'B:Rosso,C,A:Blu' } 3000) (Format-PanelColors $w)
    Check '  pubblicati nel nuovo ordine (con il titolo)' (Wait-Until { (Read-ColorFile 'live' $w) -eq 'count=3 0=red|B 2=blue|A' } 3000) (Read-ColorFile 'live' $w)
    Check '  la sessione segue subito il nuovo ordine, colori sulle schede giuste' (Wait-Until { (Get-SessionColors) -eq 'B:red|C|A:blue' } 5000) (Get-SessionColors)

    # "Nessuno" toglie il colore
    Send-ModCommand ('{0:x} color 0 none' -f $w)
    Check '  B senza colore' (Wait-Until { (Format-PanelColors $w) -eq 'B,C,A:Blu' } 3000) (Format-PanelColors $w)
    Check '  pubblicato' (Wait-Until { (Read-ColorFile 'live' $w) -eq 'count=3 2=blue|A' } 3000) (Read-ColorFile 'live' $w)
    Check '  sessione: B senza colore' (Wait-Until { (Get-SessionColors) -eq 'B|C|A:blue' } 4000) (Get-SessionColors)

    # Colori da rimettere (come dopo una riapertura): la finestra li applica e cancella il file
    [IO.File]::WriteAllText((Get-ColorFile 'pending' $w), "count=3`r`n1=green`r`n", [Text.Encoding]::ASCII)
    Check 'colori rimessi: C verde' (Wait-Until { (Format-PanelColors $w) -eq 'B,C:Verde,A:Blu' } 6000) (Format-PanelColors $w)
    Check '  file da rimettere consumato' (Wait-Until { -not (Test-Path (Get-ColorFile 'pending' $w)) } 3000)
    Check '  e pubblicato' (Wait-Until { (Read-ColorFile 'live' $w) -eq 'count=3 1=green|C 2=blue|A' } 3000) (Read-ColorFile 'live' $w)
    Check 'sessione della tray: C verde, A blu' (Wait-Until { (Get-SessionColors) -eq 'B|C:green|A:blue' } 4000) (Get-SessionColors)

    if ($Restore) {
        # Finestra chiusa: la sessione la ricorda con i colori; "Riapri ora" la riapre (nel processo principale)
        $before = (Read-DevSessionLines) -join "`n"
        Close-Window $w
        Wait-Until { @(Get-TestWindows).Count -eq 0 } 8000 | Out-Null
        Start-Sleep -Milliseconds 1500
        Check 'chiusa: la sessione ricorda ordine e colori' ((Get-SessionColors) -eq 'B|C:green|A:blue') (Get-SessionColors)
        $tray = [ColorKit]::FindWindow('VTabsExplorer.Tray', [IntPtr]::Zero)
        [void][ColorKit]::PostMessage($tray, 0x0111, [IntPtr]101, [IntPtr]::Zero) # "Riapri ora l'ultima sessione"
        Wait-Until { @(Get-TestWindows).Count -ge 1 } 20000 | Out-Null
        $restored = @(Get-TestWindows)[0]
        Add-VtTestWindow $restored
        "  finestra riaperta {0:x} (processo {1})" -f $restored, (PidOf $restored)
        $pendingText = Read-ColorFile 'pending' $restored
        Check 'riaperta: la tray rimette i colori (o la mod li ha già presi)' (Wait-Until { (Read-ColorFile 'pending' $restored) -eq 'count=3 1=green 2=blue' -or (Format-PanelColors $restored) -eq 'B,C:Verde,A:Blu' } 8000) "pending: $pendingText; pannello: $(Format-PanelColors $restored)"
        Start-Sleep -Milliseconds 2500
        Check '  la sessione li conserva' ((Get-SessionColors) -eq 'B|C:green|A:blue') (Get-SessionColors)
        $panel = Format-PanelColors $restored
        Check '  la finestra riaperta ha le schede nell''ordine giusto' (($panel -replace ':[^,]+', '') -eq 'B,C,A') $panel
        if ($ExpectRestoredColors) {
            Check '  e i colori: C verde, A blu' (Wait-Until { (Format-PanelColors $restored) -eq 'B,C:Verde,A:Blu' } 8000) (Format-PanelColors $restored)
            Check '  file da rimettere consumato dalla mod' (Wait-Until { -not (Test-Path (Get-ColorFile 'pending' $restored)) } 3000)
        }
        else {
            "  pannello della finestra riaperta: $panel (i colori compaiono se nel processo principale c'è la mod con i colori)"
        }
    }

    "--- log della mod (colori):"
    Get-Content $modLog -Encoding UTF8 -Tail 60 | Where-Object { $_ -match 'colore|Colori|colori' } | Select-Object -Last 8 | ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
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
    Remove-Item -LiteralPath $XpxDevFilter, (Join-Path $XpxData 'mod-command.txt') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Exit-VtIsolation
    Stop-Tray; Start-Tray
    "processi explorer: " + ((Get-Process explorer | ForEach-Object { "$($_.Id)" }) -join ', ') + " (barra: $shellPid); finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows)
}
