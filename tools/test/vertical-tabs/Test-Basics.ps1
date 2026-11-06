# Fase 1: schede verticali nella SOLA finestra di prova di un explorer.exe separato (/separate).
# La barra delle applicazioni e le finestre dell'utente non vengono toccate. Nessun input reale.
param([switch]$Keep, [switch]$NoShots)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'TestKit.ps1')
Add-Type -AssemblyName System.Drawing
if (-not ('VtKit' -as [type])) {
    Add-Type -Namespace '' -Name VtKit -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, System.IntPtr t);
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr dc, uint flags);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ScreenToClient(System.IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern System.IntPtr GetWindow(System.IntPtr h, uint c);
[DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(System.IntPtr h, int attribute, out RECT value, int size);
'@
}
# Pulsanti della finestra di DWM: larghezza della zona dei pulsanti (DWMWA_CAPTION_BUTTON_BOUNDS); 0 = nascosti (i nostri, vedi
# CaptionButtons nella mod)
function Get-DwmButtonsWidth([int64]$Main) { $r = New-Object VtKit+RECT; [void][VtKit]::DwmGetWindowAttribute([IntPtr]$Main, 5, [ref]$r, 16); $r.Right - $r.Left }
function Get-OurButtons([int64]$Main) { @(Get-Children $Main 'VTabsExplorer.CaptionButtons') | Select-Object -First 1 }
# Colore di un punto (coordinate client) della finestra, dalla finestra stessa (PrintWindow: vede anche le figlie layered)
function Get-ClientPixel([int64]$Main, [int]$X, [int]$Y) {
    $r = New-Object VtKit+RECT; [void][VtKit]::GetWindowRect([IntPtr]$Main, [ref]$r)
    $o = New-Object VtKit+POINT; [void][VtKit]::ScreenToClient([IntPtr]$Main, [ref]$o) # (meno) l'origine del client sullo schermo
    $bmp = New-Object Drawing.Bitmap ($r.Right - $r.Left), ($r.Bottom - $r.Top)
    $g = [Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc(); [void][VtKit]::PrintWindow([IntPtr]$Main, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
    try { $bmp.GetPixel($X - $o.X - $r.Left, $Y - $o.Y - $r.Top) } finally { $bmp.Dispose() }
}
function Format-Rgb($C) { '#{0:X2}{1:X2}{2:X2}' -f $C.R, $C.G, $C.B }
# Comando di prova alla mod della finestra (mod-command.txt) e attesa della sua eco nel log
function Send-ModCommandWait([int64]$Main, [string]$Command) {
    $before = (Get-Content $modLog -Encoding UTF8).Count
    [IO.File]::WriteAllText((Join-Path $XpxData 'mod-command.txt'), ('{0:x} {1}' -f $Main, $Command), [Text.Encoding]::ASCII)
    Wait-Until { (Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $before) -match 'comando di prova' } 4000 | Out-Null
    Start-Sleep -Milliseconds 300
}
function Get-ClassName([int64]$H) { $sb = New-Object Text.StringBuilder 256; [void][XpxKit]::GetClassName([IntPtr]$H, $sb, 256); $sb.ToString() -replace '^Microsoft\.UI\.Content\.', '' }
$script:fails = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { "  OK   $Name" } else { $script:fails++; "  FAIL $Name $Detail" }
}
function Get-ChildRect([int64]$Parent, [int64]$Child) {
    $r = New-Object VtKit+RECT; [void][VtKit]::GetWindowRect([IntPtr]$Child, [ref]$r)
    $p = New-Object VtKit+POINT; $p.X = $r.Left; $p.Y = $r.Top; [void][VtKit]::ScreenToClient([IntPtr]$Parent, [ref]$p)
    [pscustomobject]@{ X = $p.X; Y = $p.Y; W = $r.Right - $r.Left; H = $r.Bottom - $r.Top }
}
function Get-Children([int64]$Main, [string]$Class) {
    $list = @()
    $h = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, $Class, [IntPtr]::Zero)
    while ($h -ne [IntPtr]::Zero) { $list += [int64]$h; $h = [XpxKit]::FindWindowEx([IntPtr]$Main, $h, $Class, [IntPtr]::Zero) }
    $list
}
# Isole: Get-HeaderIsland e Get-PanelIsland (TestKit) le distinguono dal contrassegno della mod
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
function Get-PanelRows([int64]$Main) {
    $panel = Get-PanelIsland $Main
    if (-not $panel) { return @() }
    $root = $XAE::FromHandle([IntPtr]$panel)
    @($root.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::ListItem))) |
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
function Invoke-PanelButton([int64]$Main, [string]$Name, $Scope = $null) {
    if (-not $Scope) { $Scope = $XAE::FromHandle([IntPtr](Get-PanelIsland $Main)) }
    $b = $Scope.FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, $Name)))
    if (-not $b) { throw "pulsante '$Name' non trovato" }
    $b.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
}
function Format-Rects([int64]$Main) {
    $tabs = (Get-Children $Main 'ShellTabWindowClass' | ForEach-Object { $r = Get-ChildRect $Main $_; "[$($r.X),$($r.Y) $($r.W)x$($r.H)]" }) -join ' '
    $scaf = Get-Children $Main 'TITLE_BAR_SCAFFOLDING_WINDOW_CLASS' | Select-Object -First 1
    $s = if ($scaf) { $r = Get-ChildRect $Main $scaf; "[$($r.X),$($r.Y) $($r.W)x$($r.H)]" } else { '-' }
    $p = Get-PanelIsland $Main
    $ps = if ($p) { $r = Get-ChildRect $Main $p; "[$($r.X),$($r.Y) $($r.W)x$($r.H)]" + $(if ([XpxKit]::IsWindowVisible([IntPtr]$p)) { ' visibile' } else { ' nascosto' }) } else { '-' }
    "contenuto $tabs | barra $s | pannello $ps"
}
# Screenshot della finestra con il contenuto delle schede coperto (niente cartelle dell'utente nel
# riquadro di spostamento): restano intestazione e pannello
function Save-MaskedShot([int64]$Main, [string]$Path) {
    $r = New-Object VtKit+RECT; [void][VtKit]::GetWindowRect([IntPtr]$Main, [ref]$r)
    $bmp = New-Object Drawing.Bitmap ($r.Right - $r.Left), ($r.Bottom - $r.Top)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc(); [void][VtKit]::PrintWindow([IntPtr]$Main, $dc, 2); $g.ReleaseHdc($dc)
    $origin = New-Object VtKit+POINT; [void][VtKit]::ScreenToClient([IntPtr]$Main, [ref]$origin) # client rispetto allo schermo
    foreach ($t in Get-Children $Main 'ShellTabWindowClass') {
        $tr = New-Object VtKit+RECT; [void][VtKit]::GetWindowRect([IntPtr]$t, [ref]$tr)
        $g.FillRectangle([Drawing.Brushes]::DimGray, $tr.Left - $r.Left, $tr.Top - $r.Top, $tr.Right - $tr.Left, $tr.Bottom - $tr.Top)
    }
    $g.Dispose(); $bmp.Save($Path); $bmp.Dispose()
}

$shot = Join-Path $env:TEMP 'xpx-shots' # screenshot con il contenuto delle schede coperto
New-Item -ItemType Directory -Force $shot | Out-Null
$trayWnd = [VtKit]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero)
$shellPid = 0; [void][VtKit]::GetWindowThreadProcessId($trayWnd, [ref]$shellPid)
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
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1' # prima di Enabled: la tray non entra mai nel processo principale
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Set-XpxOption 'NavBar' 'Enabled' '0' # la barra delle cartelle ha la sua prova (Test-NavBar): qui senza
    Start-Tray
    Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000 | Out-Null

    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
    $w = @(Get-TestWindows)[0]
    Add-VtTestWindow $w
    [void][VtKit]::GetWindowThreadProcessId([IntPtr]$w, [ref]$sepPid)
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "la finestra di prova non è in un processo separato ($sepPid): mi fermo" }
    "finestra di prova {0:x} nel processo separato $sepPid" -f $w
    Add-TestTab $w (Join-Path $T 'B')
    Add-TestTab $w (Join-Path $T 'C')
    Start-Sleep -Milliseconds 800
    "prima:  " + (Format-Rects $w)

    $logBefore = if (Test-Path $modLog) { (Get-Content $modLog -Encoding UTF8).Count } else { 0 }
    & $XpxExe --inject-pid $sepPid
    $active = Wait-Until { (Test-Path $modLog) -and ((Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $logBefore) -match 'schede verticali attive|inattiva|fallita') } 20000
    Start-Sleep -Milliseconds 1200
    "--- log della mod:"
    Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $logBefore | ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
    "dopo:   " + (Format-Rects $w)

    "--- controlli"
    # (Dove conta chi riceve il mouse si guarda solo tra le figlie della finestra di prova, Get-ChildAt: una finestra
    # dell'utente in primo piano può coprirla, e da un processo in secondo piano non la si porta sopra.)
    $scale = [double](Get-ChildRect $w (Get-HeaderIsland $w)).W / 1536.0 # non serve esatto: solo per la larghezza attesa
    $panel = Get-PanelIsland $w
    Check 'pannello creato' ([bool]$panel)
    $pr = if ($panel) { Get-ChildRect $w $panel } else { $null }
    $tabsOk = $true
    foreach ($t in Get-Children $w 'ShellTabWindowClass') { if ((Get-ChildRect $w $t).X -ne $pr.W) { $tabsOk = $false } }
    Check 'contenuto delle schede a destra del pannello' ($pr -and $tabsOk) (Format-Rects $w)
    # Colonna in cima alla finestra, come le schede verticali di Chrome: intestazione e zona di trascinamento
    # della barra del titolo cominciano dal suo bordo destro
    $scaf = Get-Children $w 'TITLE_BAR_SCAFFOLDING_WINDOW_CLASS' | Select-Object -First 1
    $sr = Get-ChildRect $w $scaf
    $hr = Get-ChildRect $w (Get-HeaderIsland $w)
    $c0 = Get-ChildRect $w (Get-Children $w 'ShellTabWindowClass' | Select-Object -First 1)
    # Riga del titolo nascosta (rimasta vuota con le schede nella colonna): l'intestazione sale della sua altezza,
    # la barra degli indirizzi è in cima accanto ai pulsanti della finestra, il contenuto sale con lei
    $right = $c0.X + $c0.W
    Check 'colonna dalla cima della finestra al fondo del contenuto' ($pr.Y -ge 0 -and $pr.Y -le 8 -and $pr.Y + $pr.H -eq $c0.Y + $c0.H) (Format-Rects $w)
    Check '  intestazione dal bordo della colonna, salita: riga del titolo fuori dalla finestra, contenuto subito sotto' ($hr.X -eq $pr.W -and $hr.X + $hr.W -eq $right -and $hr.Y -lt 0 -and $c0.Y -eq $hr.Y + $hr.H) "intestazione [$($hr.X),$($hr.Y) $($hr.W)x$($hr.H)] | $(Format-Rects $w)"
    $headerRoot = $XAE::FromHandle([IntPtr](Get-HeaderIsland $w))
    $panelRoot = $XAE::FromHandle([IntPtr]$panel)
    $buttonOf = { param($Root, [string]$Pattern) @($Root.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::Button))) | Where-Object { $_.Current.Name -like $Pattern }) | Select-Object -First 1 }
    $back = & $buttonOf $headerRoot 'Indietro*'
    $toggleButton = & $buttonOf $panelRoot 'Comprimi schede'
    $origin = New-Object VtKit+POINT; [void][VtKit]::ScreenToClient([IntPtr]$w, [ref]$origin) # origine del client rispetto allo schermo (negata)
    $backY = if ($back) { $back.Current.BoundingRectangle.Y + $back.Current.BoundingRectangle.Height / 2 + $origin.Y } else { -1 }
    $toggleY = if ($toggleButton) { $toggleButton.Current.BoundingRectangle.Y + $toggleButton.Current.BoundingRectangle.Height / 2 + $origin.Y } else { -1 }
    Check '  barra degli indirizzi in cima, allineata ai pulsanti della colonna' ($back -and $toggleButton -and $backY -gt 0 -and $backY -lt 80 -and [math]::Abs($backY - $toggleY) -le 3) ("Indietro a y={0:N0}, comprimi a y={1:N0}" -f $backY, $toggleY)
    if (-not ('VtDpi' -as [type])) { Add-Type -Namespace '' -Name VtDpi -MemberDefinition '[DllImport("user32.dll")] public static extern uint GetDpiForWindow(System.IntPtr h);' }
    $reserve = [math]::Round(154 * [VtDpi]::GetDpiForWindow([IntPtr]$w) / 96.0) # 3 pulsanti da 46 DIP + 16 DIP liberi
    Check '  zona di trascinamento: solo i pulsanti della finestra più un tratto alla loro sinistra, dalla cima' ($sr.Y -eq 0 -and $sr.X + $sr.W -eq $right -and [math]::Abs($sr.W - $reserve) -le 2) "zona [$($sr.X),$($sr.Y) $($sr.W)x$($sr.H)], bordo destro $right, attesa larga $reserve"
    # Fascia del titolo in cima alla colonna (alta quanto la barra degli indirizzi): fuori dai pulsanti l'isola del
    # pannello non c'è (regione) e il mouse arriva alla finestra, che risponde come la sua barra del titolo; i pulsanti
    # restano del pannello
    $bandY = [int]$toggleY
    $bandX = [int]($pr.W / 2)
    $under = Get-ChildAt $w $bandX $bandY # (solo tra le figlie della finestra di prova: una finestra dell'utente può coprirla)
    $hitMain = Get-NcHit $w $w $bandX $bandY
    Check '  fascia del titolo della colonna: lo spazio vuoto è della finestra, che lo trascina' ($under -eq $w -and $hitMain -eq 2) ("sotto il punto {0:x} (finestra {1:x}, pannello {2:x}), risposta della finestra $hitMain (y=$bandY)" -f $under, $w, $panel)
    $belowY = $pr.Y + $sr.Y + $sr.H + 30
    $belowBand = Get-ChildAt $w $bandX $belowY
    Check '  sotto la fascia, il pannello' ($belowBand -eq $panel) ("a {0},{1}: {2} ({3:x}), pannello {4:x} [{5}]" -f $bandX, $belowY, (Get-ClassName $belowBand), $belowBand, $panel, (Format-Rects $w))
    $panelRoot = $XAE::FromHandle([IntPtr]$panel)
    $buttonHits = foreach ($name in @('Comprimi schede')) {
        $b = $panelRoot.FindFirst($XTS::Descendants, (New-Object Windows.Automation.AndCondition(
                    (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, $name)),
                    (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::Button)))))
        if ($b) { $bb = $b.Current.BoundingRectangle; "$name=" + $(if ((Get-ChildAtScreen $w ([int]($bb.X + $bb.Width / 2)) ([int]($bb.Y + $bb.Height / 2))) -eq $panel) { 'pannello' } else { 'altro' }) + "@y$([int]($bb.Y + $bb.Height / 2))" } else { "$name=assente" }
    }
    Check '  il suo pulsante (comprimi), nella fascia, resta del pannello' (@($buttonHits | Where-Object { $_ -match '=pannello@' }).Count -eq 1) ($buttonHits -join ', ')
    # La ricerca nelle schede è stata tolta (2026-11-05): nel pannello non c'è più né la lente né la casella
    $panelSearch = $panelRoot.FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, 'Cerca nelle schede')))
    Check '  niente ricerca nel pannello' (-not $panelSearch)
    # Pulsanti della finestra: i nostri (CaptionButtons), alti quanto la barra degli indirizzi come in Chrome, al posto di
    # quelli di DWM (più bassi); 3 da 46 DIP dal bordo destro, dalla cima al fondo della zona di trascinamento
    $ours = Get-OurButtons $w
    $br = if ($ours) { Get-ChildRect $w $ours } else { $null }
    $buttonsW = 3 * [math]::Round(46 * [VtDpi]::GetDpiForWindow([IntPtr]$w) / 96.0)
    Check '  pulsanti della finestra nostri, alti quanto la barra degli indirizzi (quelli di DWM nascosti)' ($br -and $br.Y -eq 0 -and $br.Y + $br.H -eq $sr.Y + $sr.H -and $br.X + $br.W -eq $right -and [math]::Abs($br.W - $buttonsW) -le 1 -and (Get-DwmButtonsWidth $w) -eq 0) ("pulsanti {0}, zona di trascinamento fino a y={1}, DWM largo {2}" -f $(if ($br) { "[$($br.X),$($br.Y) $($br.W)x$($br.H)]" } else { 'assenti' }), ($sr.Y + $sr.H), (Get-DwmButtonsWidth $w))
    Check '  sopra la zona di trascinamento e l''intestazione (primi nell''ordine Z)' ($ours -and [int64][VtKit]::GetWindow([IntPtr]$w, 5) -eq $ours) ("prima figlia {0:x}" -f [int64][VtKit]::GetWindow([IntPtr]$w, 5))
    # Riduci/ingrandisci/chiudi (dalla finestra che riceve il mouse in quel punto) a metà e in fondo alla riga, il bordo
    # superiore in cima (1 px: la nostra finestra lo lascia a chi sta sotto), tratto libero alla loro sinistra trascinabile
    # (hit test solo alle finestre della prova: i nostri pulsanti, primi nell'ordine Z, dove ci sono, altrimenti la zona di
    # trascinamento; HTTRANSPARENT passa a quella sotto, come fa il sistema)
    $script:hitTrace = @()
    $hitAt = {
        param($X, $Y)
        $h = if ($ours -and $X -ge $br.X -and $X -lt $br.X + $br.W -and $Y -ge $br.Y -and $Y -lt $br.Y + $br.H) { $ours } else { $scaf }
        $code = Get-NcHit $h $w $X $Y
        $script:hitTrace += "$X,$Y=$(Get-ClassName $h):$code"
        if ($code -eq -1 -and $h -ne $scaf) { $code = Get-NcHit $scaf $w $X $Y }
        $code
    }
    $capY = 16
    $caption = @((& $hitAt ($right - 120) $capY), (& $hitAt ($right - 70) $capY), (& $hitAt ($right - 20) $capY))
    $low = @((& $hitAt ($right - 120) ($sr.Y + $sr.H - 3)), (& $hitAt ($right - 20) ($sr.Y + $sr.H - 3)))
    $edge = & $hitAt ($right - 20) 0
    $dragHit = Get-NcHit $scaf $w ($sr.X + 6) $capY # (anche: nessun pulsante della finestra resta evidenziato dopo la prova)
    Check '  pulsanti della finestra al loro posto su tutta la riga, bordo in cima, tratto libero trascinabile' (($caption -join ',') -eq '8,9,20' -and ($low -join ',') -eq '8,20' -and $edge -eq 12 -and $dragHit -eq 2) "riduci/ingrandisci/chiudi $($caption -join ','), in fondo $($low -join ','), in cima $edge, tratto libero $dragHit ($($script:hitTrace -join '; '); pulsanti [$($br.X),$($br.Y) $($br.W)x$($br.H)])"
    # Aspetto senza mouse vero (comando di prova "caption", stesso disegno del passaggio del mouse): chiudi evidenziato è il
    # rosso dei pulsanti di Windows 11 su tutta l'altezza della riga, poi torna trasparente (si vede la barra del titolo)
    $closeIdle = Get-ClientPixel $w ($right - 10) 6
    Send-ModCommandWait $w 'caption hover 2'
    $closeHot = Get-ClientPixel $w ($right - 10) 6
    $closeHotLow = Get-ClientPixel $w ($right - 10) ($sr.Y + $sr.H - 2)
    Send-ModCommandWait $w 'caption clear'
    $closeAgain = Get-ClientPixel $w ($right - 10) 6
    Check '  chiudi evidenziato: rosso su tutta la riga, poi di nuovo come prima' ((Format-Rgb $closeHot) -eq '#C42B1C' -and (Format-Rgb $closeHotLow) -eq '#C42B1C' -and (Format-Rgb $closeAgain) -eq (Format-Rgb $closeIdle)) ("prima {0}, evidenziato {1} (in fondo {2}), dopo {3}" -f (Format-Rgb $closeIdle), (Format-Rgb $closeHot), (Format-Rgb $closeHotLow), (Format-Rgb $closeAgain))
    # La barra degli indirizzi non va sotto i pulsanti della finestra: la casella di ricerca finisce prima della zona di
    # trascinamento e riceve il mouse; il bordo superiore sopra l'intestazione resta alla finestra (ridimensiona)
    $searchBox = @($headerRoot.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::Edit))) |
            Where-Object { $_.Current.Name -like '*Cerca*' }) | Select-Object -First 1 # il nome comincia con un carattere invisibile
    if ($searchBox) {
        $sb = $searchBox.Current.BoundingRectangle
        $sbRight = $sb.X + $sb.Width + $origin.X
        $sbUnder = Get-ChildAtScreen $w ([int]($sb.X + $sb.Width / 2)) ([int]($sb.Y + $sb.Height / 2))
        Check '  casella di ricerca prima dei pulsanti della finestra, riceve il mouse' ($sbRight -le $sr.X -and $sbUnder -eq [int64](Get-HeaderIsland $w)) ("casella fino a x={0:N0}, zona di trascinamento da x={1}" -f $sbRight, $sr.X)
    }
    else { Check '  casella di ricerca trovata' $false }
    $topUnder = Get-ChildAt $w ($hr.X + 200) 1
    $topHit = Get-NcHit $w $w ($hr.X + 200) 1
    Check '  in cima, sopra l''intestazione: il bordo della finestra (ridimensiona)' ($topUnder -eq $w -and $topHit -eq 12) ("sotto il punto {0:x}, risposta {1}" -f $topUnder, $topHit)
    $nat = Get-NativeNames $w; $pan = Get-PanelNames $w
    Check 'righe = schede native' (($nat -join ',') -eq ($pan -join ',')) "native [$($nat -join ',')] pannello [$($pan -join ',')]"
    Check 'riga attiva = scheda attiva' ((Get-NativeActive $w) -eq (Get-PanelActive $w)) "native $(Get-NativeActive $w) pannello $(Get-PanelActive $w)"
    if (-not $NoShots) { Save-MaskedShot $w (Join-Path $shot 'vt-1.png') }

    Select-PanelRow $w 'A'
    Check 'clic su una riga: cambia scheda' (Wait-Until { (Get-NativeActive $w) -eq 'A' } 3000) "attiva: $(Get-NativeActive $w)"
    Start-Sleep -Milliseconds 300
    Check '  e il contenuto resta a destra' ((Get-ChildRect $w (Get-Children $w 'ShellTabWindowClass' | Select-Object -First 1)).X -eq $pr.W) (Format-Rects $w)

    Invoke-PanelButton $w 'Nuova scheda'
    Check '"+" apre una scheda' (Wait-Until { @(Get-NativeNames $w).Count -eq 4 } 5000) "native: $((Get-NativeNames $w) -join ',')"
    Check '  e compare nel pannello' (Wait-Until { @(Get-PanelNames $w).Count -eq 4 } 3000) "pannello: $((Get-PanelNames $w) -join ',')"
    Start-Sleep -Milliseconds 500
    Check '  attiva anche nel pannello' ((Get-NativeActive $w) -eq (Get-PanelActive $w)) "native $(Get-NativeActive $w) pannello $(Get-PanelActive $w)"
    $tabsOk = $true
    foreach ($t in Get-Children $w 'ShellTabWindowClass') { if ((Get-ChildRect $w $t).X -ne $pr.W) { $tabsOk = $false } }
    Check '  il suo contenuto è a destra del pannello' $tabsOk (Format-Rects $w)

    $newName = Get-PanelActive $w
    $row = Get-PanelRows $w | Where-Object { $_.Current.Name -eq $newName } | Select-Object -First 1
    Invoke-PanelButton $w 'Chiudi scheda' $row
    Check 'X della riga chiude la scheda' (Wait-Until { @(Get-NativeNames $w).Count -eq 3 } 5000) "native: $((Get-NativeNames $w) -join ',')"
    Check '  e la riga sparisce' (Wait-Until { @(Get-PanelNames $w).Count -eq 3 } 3000) "pannello: $((Get-PanelNames $w) -join ',')"

    $pl = Get-Placement $w
    Move-TestWindow $w $pl.rcNormal.Left $pl.rcNormal.Top ($pl.rcNormal.Right - $pl.rcNormal.Left - 60) ($pl.rcNormal.Bottom - $pl.rcNormal.Top - 40)
    Start-Sleep -Milliseconds 700
    $pr2 = Get-ChildRect $w (Get-PanelIsland $w)
    $c = Get-ChildRect $w (Get-Children $w 'ShellTabWindowClass' | Select-Object -First 1)
    $hr2 = Get-ChildRect $w (Get-HeaderIsland $w)
    $sr2 = Get-ChildRect $w (Get-Children $w 'TITLE_BAR_SCAFFOLDING_WINDOW_CLASS' | Select-Object -First 1)
    Check 'ridimensionando la finestra il layout resta' ($c.X -eq $pr2.W -and $pr2.Y -ge 0 -and $pr2.Y -le 8 -and $hr2.Y -lt 0 -and $c.Y -eq $hr2.Y + $hr2.H -and $pr2.Y + $pr2.H -eq $c.Y + $c.H -and $hr2.X -eq $pr2.W -and $hr2.X + $hr2.W -eq $c.X + $c.W -and $sr2.X + $sr2.W -eq $c.X + $c.W) "intestazione [$($hr2.X),$($hr2.Y) $($hr2.W)x$($hr2.H)] | $(Format-Rects $w)"
    $br2 = if (Get-OurButtons $w) { Get-ChildRect $w (Get-OurButtons $w) } else { $null }
    Check '  e i pulsanti della finestra seguono il bordo destro' ($br2 -and $br2.X + $br2.W -eq $c.X + $c.W -and $br2.Y -eq 0) $(if ($br2) { "pulsanti [$($br2.X),$($br2.Y) $($br2.W)x$($br2.H)]" } else { 'assenti' })

    Set-XpxOption 'VerticalTabs' 'Enabled' '0'
    Check 'spegnendo: contenuto di nuovo a tutta larghezza' (Wait-Until { (Get-ChildRect $w (Get-Children $w 'ShellTabWindowClass' | Select-Object -First 1)).X -eq 0 } 4000) (Format-Rects $w)
    Start-Sleep -Milliseconds 300
    Check '  pannello nascosto' (-not [XpxKit]::IsWindowVisible([IntPtr](Get-PanelIsland $w))) (Format-Rects $w)
    Check '  barra del titolo com''era' ((Get-ChildRect $w (Get-Children $w 'TITLE_BAR_SCAFFOLDING_WINDOW_CLASS' | Select-Object -First 1)).X -gt 0) (Format-Rects $w)
    $hr3 = Get-ChildRect $w (Get-HeaderIsland $w)
    $c3 = Get-ChildRect $w (Get-Children $w 'ShellTabWindowClass' | Select-Object -First 1)
    Check '  intestazione di nuovo a tutta larghezza' ($hr3.X -eq 0 -and $hr3.W -eq $c3.W) "intestazione [$($hr3.X),$($hr3.Y) $($hr3.W)x$($hr3.H)]"
    $scaf3 = Get-Children $w 'TITLE_BAR_SCAFFOLDING_WINDOW_CLASS' | Select-Object -First 1
    $right3 = $c3.X + $c3.W
    $caption3 = @((Get-NcHit $scaf3 $w ($right3 - 120) 16), (Get-NcHit $scaf3 $w ($right3 - 70) 16), (Get-NcHit $scaf3 $w ($right3 - 20) 16))
    Check '  di nuovo i pulsanti di DWM (i nostri via)' (-not (Get-OurButtons $w) -and (Get-DwmButtonsWidth $w) -gt 0 -and ($caption3 -join ',') -eq '8,9,20') "nostri $([bool](Get-OurButtons $w)), DWM largo $(Get-DwmButtonsWidth $w), riduci/ingrandisci/chiudi $($caption3 -join ',')"
    [void](Get-NcHit $scaf3 $w ($right3 - 300) 200) # nessun pulsante rimasto evidenziato
    if (-not $NoShots) { Save-MaskedShot $w (Join-Path $shot 'vt-off.png') }
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1' # prima di Enabled: la tray non entra mai nel processo principale
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Check 'riaccendendo: di nuovo a destra del pannello' (Wait-Until { (Get-ChildRect $w (Get-Children $w 'ShellTabWindowClass' | Select-Object -First 1)).X -gt 0 } 4000) (Format-Rects $w)
    Start-Sleep -Milliseconds 500
    $nat = Get-NativeNames $w; $pan = Get-PanelNames $w
    Check '  righe = schede native' (($nat -join ',') -eq ($pan -join ',')) "native [$($nat -join ',')] pannello [$($pan -join ',')]"
    Check '  di nuovo i pulsanti della finestra nostri' (Wait-Until { (Get-OurButtons $w) -and (Get-DwmButtonsWidth $w) -eq 0 } 3000) "nostri $([bool](Get-OurButtons $w)), DWM largo $(Get-DwmButtonsWidth $w)"
    if (-not $NoShots) { Save-MaskedShot $w (Join-Path $shot 'vt-2.png') }

    "--- log della mod (dopo i controlli):"
    Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $logBefore | Where-Object { $_ -match 'errore|impossibile|non riuscit|non caricata|spostata senza' } | ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
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
    Remove-Item -LiteralPath $XpxDevFilter -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Exit-VtIsolation
    Stop-Tray; Start-Tray
    "processi explorer: " + ((Get-Process explorer | ForEach-Object { "$($_.Id)" }) -join ', ') + " (barra: $shellPid); finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows)
}
