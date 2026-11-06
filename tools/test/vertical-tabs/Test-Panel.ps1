# Fase 2: schede verticali nella SOLA finestra di prova di un explorer.exe separato (/separate).
# La barra delle applicazioni e le finestre dell'utente non vengono toccate. Nessun input reale.
param([switch]$Keep, [switch]$NoShots)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'TestKit.ps1')
Add-Type -AssemblyName System.Drawing
if (-not ('VtKit2' -as [type])) {
    Add-Type -Namespace '' -Name VtKit2 -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, System.IntPtr t);
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr dc, uint flags);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ScreenToClient(System.IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern uint GetDpiForWindow(System.IntPtr h);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(System.IntPtr h, System.Text.StringBuilder s, int n);
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
function Get-ChildRect([int64]$Parent, [int64]$Child) {
    $r = New-Object VtKit2+RECT; [void][VtKit2]::GetWindowRect([IntPtr]$Child, [ref]$r)
    $p = New-Object VtKit2+POINT; $p.X = $r.Left; $p.Y = $r.Top; [void][VtKit2]::ScreenToClient([IntPtr]$Parent, [ref]$p)
    [pscustomobject]@{ X = $p.X; Y = $p.Y; W = $r.Right - $r.Left; H = $r.Bottom - $r.Top }
}
function Get-Children([int64]$Main, [string]$Class) {
    $list = @()
    $h = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, $Class, [IntPtr]::Zero)
    while ($h -ne [IntPtr]::Zero) { $list += [int64]$h; $h = [XpxKit]::FindWindowEx([IntPtr]$Main, $h, $Class, [IntPtr]::Zero) }
    $list
}
function Get-ClassName([IntPtr]$H) { $sb = New-Object Text.StringBuilder 256; [void][XpxKit]::GetClassName($H, $sb, 256); $sb.ToString() }
function Get-WindowTitle([int64]$H) { $sb = New-Object Text.StringBuilder 256; [void][VtKit2]::GetWindowText([IntPtr]$H, $sb, 256); $sb.ToString() }
# Get-PanelIsland e Get-HeaderIsland: TestKit (contrassegno della mod)
function Get-NativeTabs([int64]$Main) {
    $root = $XAE::FromHandle([IntPtr](Get-HeaderIsland $Main))
    @($root.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::TabItem))) |
            Sort-Object { $_.Current.BoundingRectangle.X })
}
function Get-NativeNames([int64]$Main) { @(Get-NativeTabs $Main | ForEach-Object { $_.Current.Name }) }
function Get-NativeActive([int64]$Main) {
    Get-NativeTabs $Main | Where-Object { $_.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected } |
        Select-Object -First 1 | ForEach-Object { $_.Current.Name }
}
function Get-PanelRoot([int64]$Main) { $XAE::FromHandle([IntPtr](Get-PanelIsland $Main)) }
function Get-PanelRows([int64]$Main) {
    $panel = Get-PanelIsland $Main
    if (-not $panel) { return @() }
    @((Get-PanelRoot $Main).FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::ListItem))) |
            Sort-Object { $_.Current.BoundingRectangle.Y })
}
function Get-PanelNames([int64]$Main) { @(Get-PanelRows $Main | ForEach-Object { $_.Current.Name }) }
function Get-PanelActive([int64]$Main) {
    Get-PanelRows $Main | Where-Object { $_.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected } |
        Select-Object -First 1 | ForEach-Object { $_.Current.Name }
}
function Select-PanelRow([int64]$Main, [string]$Name) {
    $row = Get-PanelRows $Main | Where-Object { $_.Current.Name -eq $Name } | Select-Object -First 1
    $row.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
}
function Find-ByName($Scope, [string]$Name) { $Scope.FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, $Name))) }
function Invoke-PanelButton([int64]$Main, [string]$Name, $Scope = $null) {
    if (-not $Scope) { $Scope = Get-PanelRoot $Main }
    $b = Find-ByName $Scope $Name
    if (-not $b) { throw "pulsante '$Name' non trovato" }
    $b.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
}
function Get-ContentX([int64]$Main) { (Get-ChildRect $Main (Get-Children $Main 'ShellTabWindowClass' | Select-Object -First 1)).X }
function Test-AllContentAt([int64]$Main, [int]$X) {
    $ok = $true
    foreach ($t in Get-Children $Main 'ShellTabWindowClass') { if ((Get-ChildRect $Main $t).X -ne $X) { $ok = $false } }
    $ok
}
function Format-Rects([int64]$Main) {
    $tabs = (Get-Children $Main 'ShellTabWindowClass' | ForEach-Object { $r = Get-ChildRect $Main $_; "[$($r.X),$($r.Y) $($r.W)x$($r.H)]" }) -join ' '
    $p = Get-PanelIsland $Main
    $ps = if ($p) { $r = Get-ChildRect $Main $p; "[$($r.X),$($r.Y) $($r.W)x$($r.H)]" } else { '-' }
    "contenuto $tabs | pannello $ps"
}
function Save-MaskedShot([int64]$Main, [string]$Path) {
    $r = New-Object VtKit2+RECT; [void][VtKit2]::GetWindowRect([IntPtr]$Main, [ref]$r)
    $bmp = New-Object Drawing.Bitmap ($r.Right - $r.Left), ($r.Bottom - $r.Top)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc(); [void][VtKit2]::PrintWindow([IntPtr]$Main, $dc, 2); $g.ReleaseHdc($dc)
    foreach ($t in Get-Children $Main 'ShellTabWindowClass') {
        $tr = New-Object VtKit2+RECT; [void][VtKit2]::GetWindowRect([IntPtr]$t, [ref]$tr)
        $g.FillRectangle([Drawing.Brushes]::DimGray, $tr.Left - $r.Left, $tr.Top - $r.Top, $tr.Right - $tr.Left, $tr.Bottom - $tr.Top)
    }
    $g.Dispose(); $bmp.Save($Path); $bmp.Dispose()
}
function Send-ModCommand([string]$Text) {
    $file = Join-Path $XpxData 'mod-command.txt'
    [IO.File]::WriteAllText($file, $Text, [Text.Encoding]::ASCII)
    Wait-Until { -not (Test-Path $file) } 3000 | Out-Null
    Start-Sleep -Milliseconds 400
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

$shot = Join-Path $env:TEMP 'xpx-shots' # screenshot con il contenuto delle schede coperto
New-Item -ItemType Directory -Force $shot | Out-Null
$trayWnd = [VtKit2]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero)
$shellPid = 0; [void][VtKit2]::GetWindowThreadProcessId($trayWnd, [ref]$shellPid)
"processo della barra delle applicazioni: $shellPid; primo piano presente: " + ([XpxKit]::GetForegroundWindow() -ne [IntPtr]::Zero)
$settingsFile = Join-Path $XpxData 'settings.ini'
# Prima del try: con un'altra prova aperta (dev-filter.txt) la pulizia del finally rimetterebbe le SUE impostazioni.
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente: prima chiudere l''altra prova' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userWindows = Get-UserWindowCount
$T = $XpxTestRoot
$modLog = Join-Path $XpxData 'logs\mod.log'
$sepPid = 0
try {
    Enter-VtIsolation # le finestre dell'utente restano come sono per tutta la prova
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    'A', 'B', 'C' | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $T $_) | Out-Null }
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm, (Join-Path $XpxData 'mod-command.txt') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1' # la tray non deve entrare nel processo principale
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Set-XpxOption 'NavBar' 'Enabled' '0' # la barra delle cartelle ha la sua prova (Test-NavBar): qui senza
    Set-XpxOption 'VerticalTabs' 'Width' '240'
    Set-XpxOption 'VerticalTabs' 'Collapsed' '0'
    Start-Tray
    Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000 | Out-Null

    # Il log della mod si legge da qui: la tray può iniettarla da sé nel processo di prova appena la sua
    # finestra è ferma, anche prima di --inject-pid.
    $logBefore = if (Test-Path $modLog) { (Get-Content $modLog -Encoding UTF8).Count } else { 0 }
    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
    $w = @(Get-TestWindows)[0]
    Add-VtTestWindow $w
    [void][VtKit2]::GetWindowThreadProcessId([IntPtr]$w, [ref]$sepPid)
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "la finestra di prova non è in un processo separato ($sepPid): mi fermo" }
    "finestra di prova {0:x} nel processo separato $sepPid" -f $w
    Add-TestTab $w (Join-Path $T 'B')
    Add-TestTab $w (Join-Path $T 'C')
    Start-Sleep -Milliseconds 800
    $scale = [VtKit2]::GetDpiForWindow([IntPtr]$w) / 96.0

    & $XpxExe --inject-pid $sepPid
    Wait-Until { (Test-Path $modLog) -and ((Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $logBefore) -match "\[\s*$sepPid\:.*(schede verticali attive|inattiva|fallita)") } 20000 | Out-Null
    Start-Sleep -Milliseconds 1500
    "--- log della mod:"
    Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $logBefore | ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
    "dopo:   " + (Format-Rects $w)

    "--- controlli (scala $scale)"
    $expanded = [Math]::Round(240 * $scale)
    $compact = [Math]::Round(56 * $scale)
    Check 'pannello attivo, contenuto a destra' (Test-AllContentAt $w $expanded) (Format-Rects $w)
    $nat = Get-NativeNames $w; $pan = Get-PanelNames $w
    Check 'righe = schede native' (($nat -join ',') -eq ($pan -join ',')) "native [$($nat -join ',')] pannello [$($pan -join ',')]"
    Check 'riga attiva = scheda attiva' ((Get-NativeActive $w) -eq (Get-PanelActive $w)) "native $(Get-NativeActive $w) pannello $(Get-PanelActive $w)"
    Check 'stile dei titoli letto dalle schede native' ([bool]((Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $logBefore) -match "\[\s*$sepPid\:.*titoli come le schede native"))
    if (-not $NoShots) { Save-MaskedShot $w (Join-Path $shot 'vt2-espanso.png') }

    # Comprimi / espandi
    Invoke-PanelButton $w 'Comprimi schede'
    Check 'comprimi: colonna stretta' (Wait-Until { Test-AllContentAt $w $compact } 3000) (Format-Rects $w)
    Check '  stato salvato' (Wait-Until { (Get-Ini 'VerticalTabs' 'Collapsed') -eq '1' } 2000)
    Check '  righe ancora tutte' (@(Get-PanelNames $w).Count -eq 3) "pannello [$((Get-PanelNames $w) -join ',')]"
    if (-not $NoShots) { Start-Sleep -Milliseconds 300; Save-MaskedShot $w (Join-Path $shot 'vt2-compatto.png') }
    Invoke-PanelButton $w 'Espandi schede'
    Check 'espandi: colonna larga' (Wait-Until { Test-AllContentAt $w $expanded } 3000) (Format-Rects $w)
    Check '  stato salvato' (Wait-Until { (Get-Ini 'VerticalTabs' 'Collapsed') -eq '0' } 2000)

    # Ricerca nelle schede tolta (2026-11-05): né lente né casella, anche da compresso e di nuovo espanso
    $panelSearch = (Get-PanelRoot $w).FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, 'Cerca nelle schede')))
    Check 'niente ricerca nel pannello' (-not $panelSearch)

    # Riordino (lo stesso spostamento del trascinamento, senza mouse)
    Select-PanelRow $w 'B'
    Wait-Until { (Get-NativeActive $w) -eq 'B' } 2000 | Out-Null
    Send-ModCommand ('{0:x} move 0 2' -f $w)
    Check 'riordino: ordine nativo B,C,A' (Wait-Until { ((Get-NativeNames $w) -join ',') -eq 'B,C,A' } 3000) "native [$((Get-NativeNames $w) -join ',')]"
    Check '  il pannello segue' (Wait-Until { ((Get-PanelNames $w) -join ',') -eq 'B,C,A' } 2000) "pannello [$((Get-PanelNames $w) -join ',')]"
    Check '  la scheda attiva resta B' ((Get-NativeActive $w) -eq 'B' -and (Get-PanelActive $w) -eq 'B') "native $(Get-NativeActive $w) pannello $(Get-PanelActive $w)"
    Check '  tre schede vere' (@(Get-Children $w 'ShellTabWindowClass').Count -eq 3)
    foreach ($n in 'A', 'C', 'B') {
        Select-PanelRow $w $n
        Check "  scheda $n selezionata dal pannello: mostra la cartella $n" (Wait-Until { (Get-WindowTitle $w) -like "$n*" -and (Get-NativeActive $w) -eq $n } 3000) "titolo '$(Get-WindowTitle $w)', attiva $(Get-NativeActive $w)"
    }
    Check '  contenuto sempre a destra' (Test-AllContentAt $w $expanded) (Format-Rects $w)

    # Menu contestuale nativo della riga 1 (C): si apre dove si è cliccato (comando di prova: 24,18 DIP dalla riga),
    # anche se la scheda nativa sta nell'intestazione, ora a destra della colonna
    $rowC = (Get-PanelRows $w)[1].Current.BoundingRectangle
    $clickX = [int]($rowC.X + 24 * $scale); $clickY = [int]($rowC.Y + 18 * $scale)
    $before = @([VtKit2]::WindowsOf([uint32]$sepPid) | Where-Object { [XpxKit]::IsWindowVisible($_) } | ForEach-Object { [int64]$_ })
    Send-ModCommand ('{0:x} menu 1' -f $w)
    Start-Sleep -Milliseconds 500
    $popups = @([VtKit2]::WindowsOf([uint32]$sepPid) | Where-Object { [XpxKit]::IsWindowVisible($_) -and $before -notcontains [int64]$_ })
    "  finestre comparse: " + (($popups | ForEach-Object { "$(Get-ClassName $_)" }) -join ', ')
    $items = @()
    foreach ($p in $popups) {
        $items += @($XAE::FromHandle($p).FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::MenuItem))))
    }
    "  voci del menu: " + (($items | ForEach-Object { $_.Current.Name }) -join ' | ')
    Check 'menu contestuale nativo aperto' ($items.Count -gt 0)
    if ($items.Count -gt 0) {
        $first = $items[0].Current.BoundingRectangle
        Check '  sotto il punto del clic, nella colonna' ([Math]::Abs($first.X - $clickX) -le 40 -and $first.Y -ge $clickY - 10 -and $first.Y -le $clickY + 60) "clic $clickX,$clickY prima voce $([int]$first.X),$([int]$first.Y)"
    }
    $closeItem = $items | Where-Object { $_.Current.Name -match '^Chiudi scheda' } | Select-Object -First 1
    if ($closeItem) {
        $closeItem.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
        Check '  "Chiudi scheda" chiude proprio C' (Wait-Until { ((Get-NativeNames $w) -join ',') -eq 'B,A' } 3000) "native [$((Get-NativeNames $w) -join ',')]"
        Check '  e la riga sparisce' (Wait-Until { ((Get-PanelNames $w) -join ',') -eq 'B,A' } 2000) "pannello [$((Get-PanelNames $w) -join ',')]"
    }
    elseif ($popups) {
        foreach ($p in $popups) { [void][XpxKit]::PostMessage($p, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) } # Esc
    }

    # Larghezza (come il divisore)
    Send-ModCommand ('{0:x} width 300' -f $w)
    Check 'larghezza 300: contenuto spostato' (Wait-Until { Test-AllContentAt $w ([Math]::Round(300 * $scale)) } 3000) (Format-Rects $w)
    Check '  salvata' ((Get-Ini 'VerticalTabs' 'Width') -eq '300')
    Send-ModCommand ('{0:x} width 240' -f $w)
    Check '  e di nuovo 240' (Wait-Until { Test-AllContentAt $w $expanded } 3000) (Format-Rects $w)

    "--- log della mod (errori e note):"
    Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $logBefore | Where-Object { $_ -match 'errore|impossibile|non riuscit|non caricata|senza preavviso|rifiutat|comando|riordino|menu|spostata' } | ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
    "--- processo della barra intatto: " + [bool](Get-Process -Id $shellPid -ErrorAction SilentlyContinue) + "; errori: $script:fails"
}
finally {
    if (-not $Keep) {
        Close-TestWindows
        Start-Sleep -Milliseconds 800
        if ($sepPid -and $sepPid -ne $shellPid) {
            $p = Get-Process -Id $sepPid -ErrorAction SilentlyContinue
            if ($p -and $p.MainWindowHandle -eq [IntPtr]::Zero) { Stop-Process -Id $sepPid -Force -ErrorAction SilentlyContinue }
        }
        Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $XpxDevFilter, (Join-Path $XpxData 'mod-command.txt') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Exit-VtIsolation
    Stop-Tray; Start-Tray
    "processi explorer: " + ((Get-Process explorer | ForEach-Object { "$($_.Id)" }) -join ', ') + " (barra: $shellPid); finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows)
}
