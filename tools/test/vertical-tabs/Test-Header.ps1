# Barre degli indirizzi e dei comandi del colore della barra del titolo (sempre, con le schede verticali
# attive). Finestra di prova in un explorer.exe SEPARATO (/separate), finestre dell'utente isolate
# (Enter-VtIsolation). Nessun input reale.
# Controlli: colori campione dallo screenshot (PrintWindow, contenuto delle schede coperto) e sfondi delle
# parti toccate dall'albero visuale (dump-tree.txt): con le schede verticali le fasce sono trasparenti e
# hanno il colore della barra del titolo; spente le schede verticali tornano esattamente i valori
# originali, anche dopo un cambio di tema avvenuto mentre erano accese (tema della sola intestazione, con
# il comando di prova "theme"); cambio di scheda, "Questo PC", menu "Altre opzioni".
param([switch]$Keep)
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'TestKit.ps1')
Add-Type -AssemblyName System.Drawing
if (-not ('HdrKit' -as [type])) {
    Add-Type -Namespace '' -Name HdrKit -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, System.IntPtr t);
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr dc, uint flags);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern uint GetDpiForWindow(System.IntPtr h);
'@
}
$script:fails = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { "  OK   $Name" } else { $script:fails++; "  FAIL $Name $Detail" }
}
$modLog = Join-Path $XpxData 'logs\mod.log'
function ModLines([int]$From) { Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $From }
function LogCount { (Get-Content $modLog -Encoding UTF8).Count }

# Sfondi delle parti toccate, dall'albero visuale della finestra di prova
function Get-HeaderParts([int64]$Main) {
    $before = LogCount
    [IO.File]::WriteAllText((Join-Path $XpxData 'dump-tree.txt'), ('{0:x}' -f $Main), [Text.Encoding]::ASCII)
    $script:dumpLine = $null
    Wait-Until { $script:dumpLine = ModLines $before | Where-Object { $_ -match 'Albero visuale esportato' } | Select-Object -First 1; [bool]$script:dumpLine } 10000 | Out-Null
    if (-not ($script:dumpLine -match ': (.+\.txt)$')) { return $null }
    $lines = Get-Content $Matches[1] -Encoding UTF8
    Remove-Item -LiteralPath $Matches[1] -ErrorAction SilentlyContinue
    $out = [ordered]@{}
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $l = $lines[$i]
        $bg = if ($l -match ' bg=(\S+)') { $Matches[1] -replace '!$', '' } else { '-' }
        if ($l -match '#NavigationBarControlGrid ') { $out['indirizzi'] = $bg }
        elseif ($l -match 'FileExplorerExtensions\.CommandBarControl') { if ($lines[$i + 1] -match ' bg=(\S+)') { $out['fascia comandi'] = $Matches[1] -replace '!$', '' } }
        elseif ($l -match '#FileExplorerCommandBar ') { $out['barra comandi'] = $bg }
        elseif ($l -match '#FileExplorerSecondaryCommandBar ') { $out['barra secondaria'] = $bg }
        elseif ($l -match '^ {16}\S+ #LeftBottomBorderLine ') { $out['linea sinistra'] = $bg }
        elseif ($l -match '^ {16}\S+ #RightBottomBorderLine ') { $out['linea destra'] = $bg }
    }
    $out
}
function Format-Parts($Parts) { if (-not $Parts) { return '(albero non letto)' }; ($Parts.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }) -join ', ' }
function Test-AllClear($Parts) { $Parts -and $Parts.Count -ge 5 -and @($Parts.Values | Where-Object { $_ -ne '#00FFFFFF' }).Count -eq 0 }
function Test-SameParts($A, $B) { $A -and $B -and $A.Count -eq $B.Count -and (Format-Parts $A) -eq (Format-Parts $B) }

# Colori campione (screenshot della finestra): punti in DIP rispetto all'isola dell'intestazione, dove
# non ci sono controlli: barra del titolo (y 15), fascia degli indirizzi sotto le caselle (y 83), fascia
# dei comandi sopra i pulsanti (y 90) e a sinistra del primo (x 3)
function Get-HeaderColors([int64]$Main) {
    $r = New-Object HdrKit+RECT; [void][HdrKit]::GetWindowRect([IntPtr]$Main, [ref]$r)
    $ir = New-Object HdrKit+RECT; [void][HdrKit]::GetWindowRect([IntPtr](Get-HeaderIsland $Main), [ref]$ir)
    $scale = [HdrKit]::GetDpiForWindow([IntPtr]$Main) / 96.0
    $bmp = New-Object Drawing.Bitmap ($r.Right - $r.Left), ($r.Bottom - $r.Top)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc(); [void][HdrKit]::PrintWindow([IntPtr]$Main, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
    $px = { param($x, $y) $bmp.GetPixel([int]($ir.Left - $r.Left + $x * $scale), [int]($ir.Top - $r.Top + $y * $scale)) }
    # Barra del titolo: nella riga del titolo dell'isola se si vede; con le schede verticali la riga del titolo è nascosta
    # (isola salita sopra la finestra) e la barra del titolo è il tratto libero all'inizio della zona di trascinamento (a
    # sinistra dei pulsanti della finestra) più la fascia in cima alla colonna
    $title = if ($ir.Top -lt $r.Top) {
        $drag = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, 'TITLE_BAR_SCAFFOLDING_WINDOW_CLASS', [IntPtr]::Zero)
        $dr = New-Object HdrKit+RECT; [void][HdrKit]::GetWindowRect($drag, [ref]$dr)
        $pr = New-Object HdrKit+RECT; [void][HdrKit]::GetWindowRect([IntPtr](Get-PanelIsland $Main), [ref]$pr)
        @($bmp.GetPixel([int]($dr.Left - $r.Left + 6 * $scale), [int]($dr.Top - $r.Top + 12 * $scale)), $bmp.GetPixel([int]($pr.Left - $r.Left + ($pr.Right - $pr.Left) / 2), [int]($pr.Top - $r.Top + 6)))
    }
    else { @((& $px 300 15), (& $px 150 15)) }
    $out = [ordered]@{
        titolo    = $title
        indirizzi = @((& $px 300 83), (& $px 150 83))
        comandi   = @((& $px 300 90), (& $px 3 110))
    }
    $bmp.Dispose()
    $out
}
# Zona vetro (Mica) di Esplora file: arriva fino al fondo dell'intestazione originale, e il contenuto salito (riga del
# titolo nascosta) ci entrava e lì era più chiaro (#191919 + #202020 = #393939). Colore del margine sinistro del contenuto
# (riquadro di spostamento, senza voci) in cima e più in basso, dalla stessa cattura con PrintWindow, che vede la fascia.
function Get-ContentTopColors([int64]$Main) {
    $r = New-Object HdrKit+RECT; [void][HdrKit]::GetWindowRect([IntPtr]$Main, [ref]$r)
    $content = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
    $cr = New-Object HdrKit+RECT; [void][HdrKit]::GetWindowRect($content, [ref]$cr)
    $bmp = New-Object Drawing.Bitmap ($r.Right - $r.Left), ($r.Bottom - $r.Top)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc(); [void][HdrKit]::PrintWindow([IntPtr]$Main, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
    $x = $cr.Left - $r.Left + 6; $y = $cr.Top - $r.Top
    $out = [pscustomobject]@{ Top = $bmp.GetPixel($x, $y + 4); Low = $bmp.GetPixel($x, $y + [int](($cr.Bottom - $cr.Top) * 0.6)) }
    $bmp.Dispose()
    $out
}
function Format-TopLow($C) { 'in cima #{0:X2}{1:X2}{2:X2}, più in basso #{3:X2}{4:X2}{5:X2}' -f $C.Top.R, $C.Top.G, $C.Top.B, $C.Low.R, $C.Low.G, $C.Low.B }
function Distance($A, $B) { [math]::Max([math]::Max([math]::Abs($A.R - $B.R), [math]::Abs($A.G - $B.G)), [math]::Abs($A.B - $B.B)) }
function Test-Unified($C) { @($C.indirizzi + $C.comandi | Where-Object { (Distance $_ $C.titolo[0]) -gt 3 }).Count -eq 0 -and (Distance $C.titolo[0] $C.titolo[1]) -le 3 }
function Test-Distinct($C) { (Distance $C.indirizzi[0] $C.titolo[0]) -ge 6 -and (Distance $C.comandi[0] $C.titolo[0]) -ge 6 }
function Format-Colors($C) { ($C.GetEnumerator() | ForEach-Object { $k = $_.Key; ($_.Value | ForEach-Object { '{0} #{1:X2}{2:X2}{3:X2}' -f $k, $_.R, $_.G, $_.B }) -join ' ' }) -join ', ' }
function Send-ModCommand([int64]$Main, [string]$Command) {
    [IO.File]::WriteAllText((Join-Path $XpxData 'mod-command.txt'), ('{0:x} {1}' -f $Main, $Command), [Text.Encoding]::ASCII)
    Start-Sleep -Milliseconds 1200
}
# Schede verticali accese (intestazione del colore della barra del titolo) o spente (intestazione originale)
function Set-Vertical([string]$Value) {
    $before = LogCount
    Set-XpxOption 'VerticalTabs' 'Enabled' $Value
    $expect = if ($Value -eq '1') { 'intestazione con lo sfondo della barra del titolo' } else { 'colori originali' }
    Wait-Until { (ModLines $before) -match $expect } 6000 | Out-Null
    Start-Sleep -Milliseconds 600
}

$trayWnd = [HdrKit]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero)
$shellPid = 0; [void][HdrKit]::GetWindowThreadProcessId($trayWnd, [ref]$shellPid)
$settingsFile = Join-Path $XpxData 'settings.ini'
# Prima del try: con un'altra prova aperta (dev-filter.txt) la pulizia del finally rimetterebbe le SUE impostazioni.
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente: prima chiudere l''altra prova' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userWindows = Get-UserWindowCount
$userTabs = Get-UserTabCount
$T = $XpxTestRoot
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

    $logStart = LogCount
    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
    $w = @(Get-TestWindows)[0]
    Add-VtTestWindow $w
    [void][HdrKit]::GetWindowThreadProcessId([IntPtr]$w, [ref]$sepPid)
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "la finestra di prova non è in un processo separato ($sepPid): mi fermo" }
    "finestra di prova {0:x} nel processo separato $sepPid" -f $w
    Move-TestWindow $w 80 60 1100 700
    Add-TestTab $w (Join-Path $T 'B')
    Add-TestTab $w (Join-Path $T 'C')
    Start-Sleep -Milliseconds 800
    & $XpxExe --inject-pid $sepPid
    Wait-Until { (ModLines $logStart) -match "\[\s*$sepPid\:.*intestazione con lo sfondo della barra del titolo" } 20000 | Out-Null
    Start-Sleep -Milliseconds 1200

    "--- controlli"
    $c = Get-HeaderColors $w
    Check 'schede verticali: barre degli indirizzi e dei comandi del colore della barra del titolo' (Test-Unified $c) (Format-Colors $c)
    $p = Get-HeaderParts $w
    Check '  albero: le parti toccate sono trasparenti' (Test-AllClear $p) (Format-Parts $p)
    # Zona vetro fino all'inizio del contenuto salito, anche quando Esplora file la reimposta (ingrandisci/ripristina)
    $ct = Get-ContentTopColors $w
    Check 'contenuto salito senza fascia chiara in cima (zona vetro fino al contenuto)' ((Distance $ct.Top $ct.Low) -le 3) (Format-TopLow $ct)
    $placement = Get-Placement $w
    Set-WindowShow $w 3; Start-Sleep -Milliseconds 1500
    $ctMax = Get-ContentTopColors $w
    [void][XpxKit]::SetWindowPlacement([IntPtr]$w, [ref]$placement); Start-Sleep -Milliseconds 1500
    $ctBack = Get-ContentTopColors $w
    Check '  anche da ingrandita e dopo il ripristino' ((Distance $ctMax.Top $ctMax.Low) -le 3 -and (Distance $ctBack.Top $ctBack.Low) -le 3) "ingrandita: $(Format-TopLow $ctMax); ripristinata: $(Format-TopLow $ctBack)"

    # Riferimenti con le schede verticali spente (intestazione originale): tema scuro (quello di Windows) e
    # chiaro (solo l'intestazione)
    $glassBefore = LogCount
    Set-Vertical '0'
    $origDark = Get-HeaderParts $w
    $colorsOff = Get-HeaderColors $w
    Check 'schede verticali spente: intestazione originale, fasce di colore diverso dalla barra del titolo' ((Test-Distinct $colorsOff) -and $origDark.Count -ge 5) "$(Format-Colors $colorsOff) | $(Format-Parts $origDark)"
    $glassLine = @(ModLines $glassBefore | Where-Object { $_ -match "\[\s*$sepPid\:.*Zona vetro: margine superiore \d+ px \(quello di Esplora file" }) | Select-Object -Last 1
    Check '  zona vetro di nuovo quella di Esplora file' ([bool]$glassLine) $(if ($glassLine) { $glassLine -replace '^.*Zona vetro: ', '' } else { 'nessuna riga nel log' })
    Send-ModCommand $w 'theme light'
    $origLight = Get-HeaderParts $w
    Send-ModCommand $w 'theme default'
    Check '  riferimento del tema chiaro letto (valori diversi da quelli scuri)' ($origLight -and -not (Test-SameParts $origLight $origDark)) (Format-Parts $origLight)
    Check '  di nuovo nel tema di prima: valori di prima' (Test-SameParts (Get-HeaderParts $w) $origDark)

    Set-Vertical '1'
    $c = Get-HeaderColors $w
    Check 'riaccese: di nuovo del colore della barra del titolo' (Test-Unified $c) (Format-Colors $c)
    Set-Vertical '0'
    $p = Get-HeaderParts $w
    Check 'rispente: tornano esattamente i valori originali' (Test-SameParts $p $origDark) (Format-Parts $p)
    Set-Vertical '1'

    # Cambio di tema con le schede verticali accese, poi spente: deve tornare il valore del tema nuovo, non
    # quello vecchio
    Send-ModCommand $w 'theme light'
    $p = Get-HeaderParts $w
    Check 'tema cambiato con le schede verticali accese: fasce ancora trasparenti' (Test-AllClear $p) (Format-Parts $p)
    Set-Vertical '0'
    $p = Get-HeaderParts $w
    Check '  spente dopo il cambio: valori del tema nuovo' (Test-SameParts $p $origLight) (Format-Parts $p)
    Send-ModCommand $w 'theme default'
    $p = Get-HeaderParts $w
    Check '  e tornando al tema di prima, quelli di prima' (Test-SameParts $p $origDark) (Format-Parts $p)
    Set-Vertical '1'
    $c = Get-HeaderColors $w
    Check '  riaccese: di nuovo del colore della barra del titolo' (Test-Unified $c) (Format-Colors $c)

    Select-TabByName $w 'A'
    Start-Sleep -Milliseconds 1200
    $c = Get-HeaderColors $w
    Check 'cambio di scheda: resta così' (Test-Unified $c) (Format-Colors $c)

    $tab = @(Get-ShellTabs | Where-Object { $_.Hwnd -eq $w -and $_.Path -like '*\A' })[0]
    if ($tab) {
        $tab.Item.Navigate2('shell:MyComputerFolder')
        Start-Sleep -Milliseconds 5000 # comprende un controllo completo della mod (ogni 4 s)
        $c = Get-HeaderColors $w
        Check '"Questo PC" (barra dei comandi diversa): resta così' (Test-Unified $c) (Format-Colors $c)
        $tab = @(Get-ShellTabs | Where-Object { $_.Hwnd -eq $w -and $_.Path -like '::*' })[0]
        if ($tab) { $tab.Item.Navigate2((Join-Path $T 'A')) }
        Start-Sleep -Milliseconds 2500
    }
    else { Check '"Questo PC": scheda A trovata' $false }

    $root = $XAE::FromHandle([IntPtr](Get-HeaderIsland $w))
    $more = $root.FindAll($XTS::Descendants, [Windows.Automation.Condition]::TrueCondition) |
        Where-Object { $_.Current.Name -eq 'Altre opzioni' } | Select-Object -First 1
    if ($more) {
        $pattern = $null
        $expand = $more.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$pattern)
        if ($expand) { $pattern.Expand() } else { $more.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
        Start-Sleep -Milliseconds 1200
        $c = Get-HeaderColors $w
        Check 'menu "Altre opzioni" aperto: la barra dei comandi resta trasparente' (Test-Unified $c) (Format-Colors $c)
        # Con la colonna in cima l'intestazione comincia dal suo bordo: il menu (una finestra a sé) deve comparire
        # sotto il suo pulsante, non spostato della larghezza della colonna
        $btn = $more.Current.BoundingRectangle
        $ir = New-Object HdrKit+RECT; [void][HdrKit]::GetWindowRect([IntPtr](Get-HeaderIsland $w), [ref]$ir)
        # "Seleziona tutto" c'è sempre nel menu "Altre opzioni" (finestra a sé del processo di prova)
        $item = $null
        foreach ($top in $XAE::RootElement.FindAll($XTS::Children, (New-Object Windows.Automation.PropertyCondition($XAE::ProcessIdProperty, [int]$sepPid)))) {
            $item = $top.FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, 'Seleziona tutto')))
            if ($item) { break }
        }
        if ($item) {
            $mr = $item.Current.BoundingRectangle
            Check '  il menu compare sotto il suo pulsante, dentro l''intestazione' ($mr.X -ge $ir.Left -and $mr.X -le $btn.X + $btn.Width -and $mr.Y -ge $btn.Y + $btn.Height - 8 -and $mr.Y -le $btn.Y + $btn.Height + 500) "pulsante $([int]$btn.X),$([int]$btn.Y) $([int]$btn.Width)x$([int]$btn.Height); ""Seleziona tutto"" a $([int]$mr.X),$([int]$mr.Y); intestazione da x=$($ir.Left)"
        }
        else { Check '  voci del menu trovate' $false }
        if ($expand) { $pattern.Collapse() } else { $more.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
        Start-Sleep -Milliseconds 1000
    }
    else { Check 'pulsante "Altre opzioni" trovato' $false }

    $p = Get-HeaderParts $w
    Check 'alla fine: tutto ancora trasparente' (Test-AllClear $p) (Format-Parts $p)
    "--- log della mod (intestazione ed errori):"
    ModLines $logStart | Where-Object { $_ -match "\[\s*$sepPid\:" -and $_ -match 'intestazione|errore|Errore|eccezione' } | ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
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
    Remove-Item -LiteralPath $XpxDevFilter, (Join-Path $XpxData 'mod-command.txt'), (Join-Path $XpxData 'dump-tree.txt') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Exit-VtIsolation
    Stop-Tray; Start-Tray
    $shellOk = [bool](Get-Process -Id $shellPid -ErrorAction SilentlyContinue)
    "--- processo della barra intatto: $shellOk; errori: $script:fails"
    "processi explorer: " + ((Get-Process explorer | ForEach-Object { "$($_.Id)" }) -join ', ') + " (barra: $shellPid); finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows -and (Get-UserTabCount) -eq $userTabs)
}
