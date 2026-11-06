# Prove con mouse e tastiera VERI sulle schede verticali, nella SOLA finestra di prova di un explorer.exe
# separato (/separate). Se il mouse viene mosso da fuori la prova si interrompe da sola.
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'TestKit.ps1')
if (-not ('MouseKit' -as [type])) {
    Add-Type -Namespace '' -Name MouseKit -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
[StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO { public int cbSize; public uint flags; public System.IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret; public RECT rcCaret; }
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, System.IntPtr t);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
[DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, System.IntPtr extra);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.IntPtr extra);
[DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint thread, ref GUITHREADINFO info);
[DllImport("user32.dll")] public static extern uint GetDpiForWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern System.IntPtr WindowFromPoint(POINT p);
[DllImport("user32.dll")] public static extern System.IntPtr SendMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern int GetMenuItemCount(System.IntPtr m);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetMenuString(System.IntPtr m, uint item, System.Text.StringBuilder s, int n, uint flags);
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

# --- Input reale (come VTabsNotepad): movimenti assoluti, attesa che il cursore arrivi, stop se l'utente muove il mouse
$script:LastCursor = $null
function Get-Cursor { $p = New-Object MouseKit+POINT; [void][MouseKit]::GetCursorPos([ref]$p); $p }
function Move-To([int]$X, [int]$Y, [int]$Tolerance = 6) {
    if ($script:LastCursor) {
        $c = Get-Cursor
        if ([math]::Abs($c.X - $script:LastCursor.X) -gt $Tolerance -or [math]::Abs($c.Y - $script:LastCursor.Y) -gt $Tolerance) {
            [MouseKit]::mouse_event(0x04, 0, 0, 0, [IntPtr]::Zero); [MouseKit]::mouse_event(0x10, 0, 0, 0, [IntPtr]::Zero); [MouseKit]::mouse_event(0x40, 0, 0, 0, [IntPtr]::Zero)
            throw ("Mouse mosso dall'esterno (atteso {0},{1}, trovato {2},{3}): prova interrotta." -f $script:LastCursor.X, $script:LastCursor.Y, $c.X, $c.Y)
        }
    }
    $vx = [MouseKit]::GetSystemMetrics(76); $vy = [MouseKit]::GetSystemMetrics(77)
    $vw = [MouseKit]::GetSystemMetrics(78); $vh = [MouseKit]::GetSystemMetrics(79)
    $nx = [int][math]::Round(($X - $vx) * 65535.0 / ($vw - 1)); $ny = [int][math]::Round(($Y - $vy) * 65535.0 / ($vh - 1))
    [MouseKit]::mouse_event(0x0001 -bor 0x8000 -bor 0x4000, $nx, $ny, 0, [IntPtr]::Zero)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    do { Start-Sleep -Milliseconds 2; $c = Get-Cursor } while (($c.X -ne $X -or $c.Y -ne $Y) -and $sw.ElapsedMilliseconds -lt 400)
    $script:LastCursor = $c
}
function Mouse-Down([string]$Button = 'Left') { [MouseKit]::mouse_event($(switch ($Button) { 'Right' { 0x08 } 'Middle' { 0x20 } default { 0x02 } }), 0, 0, 0, [IntPtr]::Zero) }
function Mouse-Up([string]$Button = 'Left') { [MouseKit]::mouse_event($(switch ($Button) { 'Right' { 0x10 } 'Middle' { 0x40 } default { 0x04 } }), 0, 0, 0, [IntPtr]::Zero) }
# Prima di ogni pressione: il punto deve stare su una finestra del processo di prova (mai su quelle dell'utente)
function Assert-Ours([int]$X, [int]$Y) {
    $pt = New-Object MouseKit+POINT; $pt.X = $X; $pt.Y = $Y
    $owner = 0; [void][MouseKit]::GetWindowThreadProcessId([MouseKit]::WindowFromPoint($pt), [ref]$owner)
    if (-not $sepPid -or $owner -ne $sepPid) { throw "Il punto $X,$Y non è sulla finestra di prova (processo $owner): prova interrotta." }
}
function Click-At([int]$X, [int]$Y, [string]$Button = 'Left') { Move-To $X $Y 12; Start-Sleep -Milliseconds 150; Assert-Ours $X $Y; Mouse-Down $Button; Start-Sleep -Milliseconds 60; Mouse-Up $Button }
function Double-Click-At([int]$X, [int]$Y) {
    Move-To $X $Y 12; Start-Sleep -Milliseconds 200
    Assert-Ours $X $Y
    Mouse-Down; Start-Sleep -Milliseconds 40; Mouse-Up; Start-Sleep -Milliseconds 90
    Mouse-Down; Start-Sleep -Milliseconds 40; Mouse-Up
}
function Drag([int]$X1, [int]$Y1, [int]$X2, [int]$Y2, [int]$Steps = 25, [int]$DelayMs = 20, [int]$HoldMs = 250) {
    Move-To $X1 $Y1 12; Start-Sleep -Milliseconds 150
    Assert-Ours $X1 $Y1
    Mouse-Down; Start-Sleep -Milliseconds 120
    try {
        for ($i = 1; $i -le $Steps; $i++) {
            Move-To ([int]($X1 + ($X2 - $X1) * $i / $Steps)) ([int]($Y1 + ($Y2 - $Y1) * $i / $Steps))
            Start-Sleep -Milliseconds $DelayMs
        }
        for ($t = 0; $t -lt $HoldMs; $t += 100) { Start-Sleep -Milliseconds 100; Move-To $X2 $Y2 }
    }
    finally { Mouse-Up }
}
function Press-Key([byte]$Vk, [byte[]]$Modifiers = @()) {
    # Tasti solo con la finestra di prova (o un suo menu) in primo piano: mai nella finestra dell'utente
    $owner = 0; [void][MouseKit]::GetWindowThreadProcessId([XpxKit]::GetForegroundWindow(), [ref]$owner)
    if (-not $sepPid -or $owner -ne $sepPid) { throw "In primo piano non c'è la finestra di prova (processo $owner): tasti non inviati, prova interrotta." }
    foreach ($m in $Modifiers) { [MouseKit]::keybd_event($m, 0, 0, [IntPtr]::Zero) }
    [MouseKit]::keybd_event($Vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 40; [MouseKit]::keybd_event($Vk, 0, 2, [IntPtr]::Zero)
    [array]::Reverse($Modifiers); foreach ($m in $Modifiers) { [MouseKit]::keybd_event($m, 0, 2, [IntPtr]::Zero) }
    Start-Sleep -Milliseconds 250
}

# --- Finestra di prova
function Get-Children([int64]$Main, [string]$Class) {
    $list = @(); $h = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, $Class, [IntPtr]::Zero)
    while ($h -ne [IntPtr]::Zero) { $list += [int64]$h; $h = [XpxKit]::FindWindowEx([IntPtr]$Main, $h, $Class, [IntPtr]::Zero) }
    $list
}
function Get-Rect([int64]$H) { $r = New-Object MouseKit+RECT; [void][MouseKit]::GetWindowRect([IntPtr]$H, [ref]$r); $r }
# Get-PanelIsland: TestKit (contrassegno della mod); finché il pannello non c'è, nessuna riga
function Get-PanelRoot([int64]$Main) { $p = Get-PanelIsland $Main; if ($p) { $XAE::FromHandle([IntPtr]$p) } else { $null } }
function Get-PanelRows([int64]$Main) {
    $root = Get-PanelRoot $Main
    if (-not $root) { return @() }
    @($root.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::ListItem))) |
            Sort-Object { $_.Current.BoundingRectangle.Y })
}
function Get-PanelNames([int64]$Main) { @(Get-PanelRows $Main | ForEach-Object { $_.Current.Name }) }
function Get-Row([int64]$Main, [string]$Name) { Get-PanelRows $Main | Where-Object { $_.Current.Name -eq $Name } | Select-Object -First 1 }
function Get-RowCenter([int64]$Main, [string]$Name) {
    $b = (Get-Row $Main $Name).Current.BoundingRectangle
    [pscustomobject]@{ X = [int]($b.X + 70); Y = [int]($b.Y + $b.Height / 2) } # a sinistra, lontano dalla X
}
function Get-NativeNames([int64]$Main) { Get-TabNames $Main }
function Get-NativeActive([int64]$Main) { Get-ActiveTabName $Main }
function Test-RowCloseVisible([int64]$Main, [string]$Name) {
    $row = Get-Row $Main $Name
    [bool]($row -and $row.FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, 'Chiudi scheda'))))
}
function Get-FocusClass([int64]$Main) {
    $pid0 = 0; $thread = [MouseKit]::GetWindowThreadProcessId([IntPtr]$Main, [ref]$pid0)
    $info = New-Object MouseKit+GUITHREADINFO; $info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($info)
    if (-not [MouseKit]::GetGUIThreadInfo($thread, [ref]$info)) { return '?' }
    $sb = New-Object Text.StringBuilder 128; [void][XpxKit]::GetClassName($info.hwndFocus, $sb, 128); $sb.ToString()
}
function Get-Ini([string]$Section, [string]$Key) {
    if (-not ('IniKit' -as [type])) {
        Add-Type -Namespace '' -Name IniKit -MemberDefinition '[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern int GetPrivateProfileString(string s, string k, string d, System.Text.StringBuilder r, int n, string f);'
    }
    $sb = New-Object Text.StringBuilder 256
    [void][IniKit]::GetPrivateProfileString($Section, $Key, '', $sb, 256, (Join-Path $XpxData 'settings.ini'))
    if ($sb.Length -eq 0) { $null } else { $sb.ToString() }
}

$trayWnd = [MouseKit]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero)
$shellPid = 0; [void][MouseKit]::GetWindowThreadProcessId($trayWnd, [ref]$shellPid)
if ([XpxKit]::GetForegroundWindow() -eq [IntPtr]::Zero) { throw 'Nessuna finestra in primo piano (sessione Desktop remoto non visibile?): niente prove col mouse.' }
$settingsFile = Join-Path $XpxData 'settings.ini'
# Prima del try: con un'altra prova aperta (dev-filter.txt) la pulizia del finally rimetterebbe le SUE impostazioni.
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente: prima chiudere l''altra prova' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userWindows = Get-UserWindowCount
$T = $XpxTestRoot
$sepPid = 0
$cursorStart = Get-Cursor
try {
    Enter-VtIsolation # le finestre dell'utente restano come sono per tutta la prova
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    'A', 'B', 'C', 'D' | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $T $_) | Out-Null }
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1' # prima di Enabled: mai il processo principale
    Set-XpxOption 'VerticalTabs' 'Width' '240'
    Set-XpxOption 'VerticalTabs' 'Collapsed' '0'
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Set-XpxOption 'NavBar' 'Enabled' '0' # la barra delle cartelle ha la sua prova (Test-NavBar): qui senza
    Start-Tray
    Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000 | Out-Null

    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
    $w = @(Get-TestWindows)[0]
    Add-VtTestWindow $w
    [void][MouseKit]::GetWindowThreadProcessId([IntPtr]$w, [ref]$sepPid)
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "la finestra di prova non è in un processo separato ($sepPid): mi fermo" }
    "finestra di prova {0:x} nel processo separato $sepPid" -f $w
    Add-TestTab $w (Join-Path $T 'B')
    Add-TestTab $w (Join-Path $T 'C')
    Add-TestTab $w (Join-Path $T 'D')
    $scale = [MouseKit]::GetDpiForWindow([IntPtr]$w) / 96.0
    if (-not (Wait-Until { @(Get-PanelNames $w).Count -eq 4 } 15000)) { throw "pannello non pronto: [$((Get-PanelNames $w) -join ',')]" }
    [void][MouseKit]::SetForegroundWindow([IntPtr]$w)
    Start-Sleep -Milliseconds 600
    "pannello: [$((Get-PanelNames $w) -join ',')], attiva $(Get-NativeActive $w); scala $scale"

    "--- mouse vero"
    # 1) Passaggio del mouse: la X compare sulla riga
    $a = Get-RowCenter $w 'A'
    Move-To $a.X $a.Y 400
    Start-Sleep -Milliseconds 400
    Check 'passaggio del mouse: la X compare sulla riga A' (Test-RowCloseVisible $w 'A')

    # 2) Clic su una riga: cambia scheda e la tastiera torna alla vista dei file
    Click-At $a.X $a.Y
    Check 'clic sulla riga A: scheda A attiva' (Wait-Until { (Get-NativeActive $w) -eq 'A' } 3000) "attiva $(Get-NativeActive $w)"
    Start-Sleep -Milliseconds 500
    Check '  e il focus va alla vista dei file' ((Get-FocusClass $w) -eq 'DirectUIHWND') "focus su $(Get-FocusClass $w)"
    Check '  la X della riga B non c''è (né attiva né sotto il mouse)' (-not (Test-RowCloseVisible $w 'B'))

    # 3) Clic centrale su D: chiude la scheda
    $d = Get-RowCenter $w 'D'
    Click-At $d.X $d.Y 'Middle'
    Check 'clic centrale sulla riga D: scheda chiusa' (Wait-Until { (Get-NativeNames $w) -notcontains 'D' -and @(Get-NativeNames $w).Count -eq 3 } 3000) "native [$((Get-NativeNames $w) -join ',')]"
    Check '  e la riga sparisce' (Wait-Until { ((Get-PanelNames $w) -join ',') -eq 'A,B,C' } 3000) "pannello [$((Get-PanelNames $w) -join ',')]"

    # 4) Trascinamento: A sotto C
    $a = Get-RowCenter $w 'A'; $c = Get-RowCenter $w 'C'
    Drag $a.X $a.Y $a.X ($c.Y + 8) 25 20 300
    Check 'trascinamento di A sotto C: ordine nativo B,C,A' (Wait-Until { ((Get-NativeNames $w) -join ',') -eq 'B,C,A' } 3000) "native [$((Get-NativeNames $w) -join ',')]"
    Check '  il pannello mostra B,C,A' (Wait-Until { ((Get-PanelNames $w) -join ',') -eq 'B,C,A' } 2000) "pannello [$((Get-PanelNames $w) -join ',')]"
    Check '  A resta la scheda attiva' ((Get-NativeActive $w) -eq 'A') "attiva $(Get-NativeActive $w)"
    Check '  tre schede vere' (@(Get-Children $w 'ShellTabWindowClass').Count -eq 3)

    # 5) Clic destro su B: il menu nativo compare sotto il puntatore; Esc lo chiude
    $before = @([MouseKit]::WindowsOf([uint32]$sepPid) | Where-Object { [XpxKit]::IsWindowVisible($_) } | ForEach-Object { [int64]$_ })
    $b = Get-RowCenter $w 'B'
    Click-At $b.X $b.Y 'Right'
    Start-Sleep -Milliseconds 600
    $popup = @([MouseKit]::WindowsOf([uint32]$sepPid) | Where-Object { [XpxKit]::IsWindowVisible($_) -and $before -notcontains [int64]$_ }) | Select-Object -First 1
    Check 'clic destro: menu nativo aperto' ([bool]$popup)
    if ($popup) {
        $pr = Get-Rect ([int64]$popup)
        $items = @($XAE::FromHandle($popup).FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::MenuItem))) | ForEach-Object { $_.Current.Name })
        "  voci: " + ($items -join ' | ') + "; menu a [$($pr.Left),$($pr.Top)], clic a [$($b.X),$($b.Y)]"
        Check '  vicino al puntatore' ([math]::Abs($pr.Left - $b.X) -lt 60 -and [math]::Abs($pr.Top - $b.Y) -lt 60) "menu [$($pr.Left),$($pr.Top)] clic [$($b.X),$($b.Y)]"
        Press-Key 0x1B
        Check '  Esc lo chiude' (Wait-Until { -not [XpxKit]::IsWindowVisible($popup) } 2000)
    }
    Check '  nessuna scheda cambiata' (((Get-NativeNames $w) -join ',') -eq 'B,C,A') "native [$((Get-NativeNames $w) -join ',')]"

    # 5b) "Colore scheda" con il mouse: clic destro sulla riga C, passaggio sulla voce (si apre il
    #     sottomenu), a destra dentro il sottomenu e giù fino a "Rosso", clic
    $before = @([MouseKit]::WindowsOf([uint32]$sepPid) | Where-Object { [XpxKit]::IsWindowVisible($_) } | ForEach-Object { [int64]$_ })
    $newPopups = { @([MouseKit]::WindowsOf([uint32]$sepPid) | Where-Object { [XpxKit]::IsWindowVisible($_) -and $before -notcontains [int64]$_ }) }
    $findIn = {
        param($Windows, [string]$Name)
        foreach ($h in $Windows) {
            $found = $XAE::FromHandle($h).FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, $Name)))
            if ($found) { return $found }
        }
        $null
    }
    $c = Get-RowCenter $w 'C'
    Click-At $c.X $c.Y 'Right'
    Start-Sleep -Milliseconds 700
    $sub = & $findIn (& $newPopups) 'Colore scheda'
    Check 'clic destro: c''è "Colore scheda" in fondo al menu' ([bool]$sub)
    if ($sub) {
        $sb = $sub.Current.BoundingRectangle
        Move-To ([int]($sb.X + $sb.Width / 2)) ([int]($sb.Y + $sb.Height / 2)) 12
        Start-Sleep -Milliseconds 900
        $red = & $findIn (& $newPopups) 'Rosso'
        Check '  passandoci sopra si apre il sottomenu dei colori' ([bool]$red)
        if ($red) {
            $rb = $red.Current.BoundingRectangle
            $rx = [int]($rb.X + $rb.Width / 2); $ry = [int]($rb.Y + $rb.Height / 2)
            Move-To $rx ([int]($sb.Y + $sb.Height / 2)) 12 # prima a destra, dentro il sottomenu
            Start-Sleep -Milliseconds 150
            Move-To $rx $ry 12
            Start-Sleep -Milliseconds 200
            Mouse-Down; Start-Sleep -Milliseconds 60; Mouse-Up
            Check '  "Rosso": la riga C è rossa' (Wait-Until { (Get-Row $w 'C').Current.ItemStatus -eq 'Colore: Rosso' } 3000) "stato: '$((Get-Row $w 'C').Current.ItemStatus)'"
        }
        else { Press-Key 0x1B; Press-Key 0x1B }
    }

    # 6) Divisore: trascinato a destra la colonna si allarga; doppio clic torna a 240
    $panel = Get-Rect (Get-PanelIsland $w)
    $sx = $panel.Right - [int](3 * $scale); $sy = $panel.Top + 400
    Drag $sx $sy ($sx + 60) $sy 15 25 200
    Start-Sleep -Milliseconds 500
    $panel2 = Get-Rect (Get-PanelIsland $w)
    $width = Get-Ini 'VerticalTabs' 'Width'
    Check 'divisore trascinato di 60 px: colonna più larga e salvata' (($panel2.Right - $panel2.Left) -ge ($panel.Right - $panel.Left) + 50 -and [int]$width -ge 280) "larghezza $($panel2.Right - $panel2.Left) px, Width=$width"
    $content = Get-Rect (Get-Children $w 'ShellTabWindowClass' | Select-Object -First 1)
    Check '  il contenuto segue' ($content.Left -eq $panel2.Right) "contenuto da $($content.Left), pannello fino a $($panel2.Right)"
    Double-Click-At ($panel2.Right - [int](3 * $scale)) $sy
    Check '  doppio clic: di nuovo 240' (Wait-Until { (Get-Ini 'VerticalTabs' 'Width') -eq '240' } 3000) "Width=$(Get-Ini 'VerticalTabs' 'Width')"

    # 7) Comprimi/espandi con il pulsante
    $toggle = (Get-PanelRoot $w).FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, 'Comprimi schede')))
    $tb = $toggle.Current.BoundingRectangle
    Click-At ([int]($tb.X + $tb.Width / 2)) ([int]($tb.Y + $tb.Height / 2))
    Check 'clic su "Comprimi schede": colonna stretta' (Wait-Until { ((Get-Rect (Get-PanelIsland $w)).Right - (Get-Rect (Get-PanelIsland $w)).Left) -lt 100 } 3000)
    Click-At ([int]($tb.X + $tb.Width / 2)) ([int]($tb.Y + $tb.Height / 2))
    Check '  di nuovo: espansa' (Wait-Until { ((Get-Rect (Get-PanelIsland $w)).Right - (Get-Rect (Get-PanelIsland $w)).Left) -gt 250 } 3000)

    # 8) Barra del titolo: con la colonna in cima alla finestra si trascina dalla fascia in cima alla colonna
    #    (fuori dai pulsanti) e dalla riga del titolo accanto alla colonna (dove c'erano le schede)
    $wr = Get-Rect $w
    $pnl = Get-Rect (Get-PanelIsland $w)
    $bandX = $pnl.Left + [int](($pnl.Right - $pnl.Left) / 2); $bandY = $pnl.Top + [int](16 * $scale)
    Drag $bandX $bandY ($bandX + 80) ($bandY + 40) 15 25 200
    Start-Sleep -Milliseconds 400
    $wr2 = Get-Rect $w
    Check 'trascinando la fascia in cima alla colonna la finestra si sposta' ([math]::Abs(($wr2.Left - $wr.Left) - 80) -le 4 -and [math]::Abs(($wr2.Top - $wr.Top) - 40) -le 4) "spostata di $($wr2.Left - $wr.Left),$($wr2.Top - $wr.Top)"
    # Riga del titolo nascosta (barra degli indirizzi in cima): si trascina anche dal tratto libero a sinistra dei
    # pulsanti della finestra, l'inizio della zona di trascinamento
    $drag = Get-Rect ([int64][XpxKit]::FindWindowEx([IntPtr]$w, [IntPtr]::Zero, 'TITLE_BAR_SCAFFOLDING_WINDOW_CLASS', [IntPtr]::Zero))
    $titleX = $drag.Left + [int](6 * $scale); $titleY = $drag.Top + [int](12 * $scale)
    Drag $titleX $titleY ($titleX - 80) ($titleY + 40) 15 25 200
    Start-Sleep -Milliseconds 400
    $wr3 = Get-Rect $w
    Check '  e dal tratto libero a sinistra dei pulsanti della finestra' ([math]::Abs(($wr3.Left - $wr2.Left) + 80) -le 4 -and [math]::Abs(($wr3.Top - $wr2.Top) - 40) -le 4) "spostata di $($wr3.Left - $wr2.Left),$($wr3.Top - $wr2.Top)"
    $pnl = Get-Rect (Get-PanelIsland $w)
    Drag ($pnl.Left + [int](($pnl.Right - $pnl.Left) / 2)) ($pnl.Top + [int](16 * $scale)) ($pnl.Left + [int](($pnl.Right - $pnl.Left) / 2)) ($pnl.Top + [int](16 * $scale) - 80) 15 25 200
    Start-Sleep -Milliseconds 400
    # Doppio clic sulla fascia della colonna: ingrandisce; di nuovo (la fascia è sempre in cima): torna com'era
    $pnl = Get-Rect (Get-PanelIsland $w)
    Double-Click-At ($pnl.Left + [int](($pnl.Right - $pnl.Left) / 2)) ($pnl.Top + [int](16 * $scale))
    Check 'doppio clic sulla fascia in cima alla colonna: finestra ingrandita' (Wait-Until { (Get-Placement $w).showCmd -eq 3 } 3000) "showCmd $((Get-Placement $w).showCmd)"
    Start-Sleep -Milliseconds 700
    $pnl = Get-Rect (Get-PanelIsland $w)
    $hdr = Get-Rect (Get-HeaderIsland $w)
    Check '  ingrandita: colonna sempre in cima, intestazione dal suo bordo (riga del titolo nascosta)' ($hdr.Top -lt $pnl.Top -and $hdr.Left -eq $pnl.Right) "colonna [$($pnl.Left),$($pnl.Top)-$($pnl.Right),$($pnl.Bottom)] intestazione da $($hdr.Left),$($hdr.Top)"
    Double-Click-At ($pnl.Left + [int](($pnl.Right - $pnl.Left) / 2)) ($pnl.Top + [int](16 * $scale))
    Check '  di nuovo: torna com''era' (Wait-Until { (Get-Placement $w).showCmd -eq 1 } 3000) "showCmd $((Get-Placement $w).showCmd)"
    Start-Sleep -Milliseconds 700
    # Clic destro sulla fascia: il menu di sistema della finestra, come sulla barra del titolo; Esc lo chiude
    $classOf = { param($h) $sb = New-Object Text.StringBuilder 64; [void][XpxKit]::GetClassName([IntPtr]$h, $sb, 64); $sb.ToString() }
    $pnl = Get-Rect (Get-PanelIsland $w)
    $before = @([MouseKit]::WindowsOf([uint32]$sepPid) | Where-Object { [XpxKit]::IsWindowVisible($_) } | ForEach-Object { [int64]$_ })
    Click-At ($pnl.Left + [int](($pnl.Right - $pnl.Left) / 2)) ($pnl.Top + [int](16 * $scale)) 'Right'
    Start-Sleep -Milliseconds 700
    $sysMenu = @([MouseKit]::WindowsOf([uint32]$sepPid) | Where-Object { [XpxKit]::IsWindowVisible($_) -and $before -notcontains [int64]$_ -and (& $classOf $_) -eq '#32768' }) | Select-Object -First 1
    # Voci lette dalla sua HMENU (MN_GETHMENU): UI Automation non le vede da un altro processo
    $sysItems = @()
    if ($sysMenu) {
        $hmenu = [MouseKit]::SendMessage([IntPtr]$sysMenu, 0x01E1, [IntPtr]::Zero, [IntPtr]::Zero)
        for ($i = 0; $i -lt [MouseKit]::GetMenuItemCount($hmenu); $i++) {
            $sb = New-Object Text.StringBuilder 128
            if ([MouseKit]::GetMenuString($hmenu, [uint32]$i, $sb, 128, 0x400) -gt 0) { $sysItems += (($sb.ToString() -replace '&', '') -replace "`t.*$", '') }
        }
    }
    Check 'clic destro sulla fascia: il menu di sistema della finestra' ($sysMenu -and ($sysItems -join '|') -match 'Chiudi') "voci: $($sysItems -join ' | ')"
    if ($sysMenu) {
        Press-Key 0x1B
        Check '  Esc lo chiude, la finestra resta com''era' ((Wait-Until { -not [XpxKit]::IsWindowVisible([IntPtr]$sysMenu) } 2000) -and (Get-Placement $w).showCmd -eq 1)
    }
    # 9) Tastiera: Ctrl+Tab dalla vista dei file; F6 non deve finire sulle schede nascoste
    $first = Get-RowCenter $w 'C'
    Click-At $first.X $first.Y
    Wait-Until { (Get-NativeActive $w) -eq 'C' } 2000 | Out-Null
    Start-Sleep -Milliseconds 400
    Press-Key 0x09 @(0x11) # Ctrl+Tab
    Check 'Ctrl+Tab: passa alla scheda successiva e il pannello la segue' (Wait-Until { (Get-NativeActive $w) -eq 'A' } 2000) "attiva $(Get-NativeActive $w)"
    $focusSeen = @()
    for ($i = 0; $i -lt 5; $i++) {
        Press-Key 0x75 # F6
        $f = $XAE::FocusedElement
        $focusSeen += "$($f.Current.ControlType.ProgrammaticName -replace 'ControlType\.', '')/$($f.Current.Name)"
    }
    "  F6 (5 volte): " + ($focusSeen -join ' -> ')
    Check '  F6 non porta il focus sulle schede nascoste' (-not ($focusSeen -match '^TabItem/')) ($focusSeen -join ' -> ')
    Press-Key 0x1B

    "--- processo della barra intatto: " + [bool](Get-Process -Id $shellPid -ErrorAction SilentlyContinue) + "; errori: $script:fails"
}
finally {
    [MouseKit]::mouse_event(0x04, 0, 0, 0, [IntPtr]::Zero) # nessun tasto resta premuto
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
    $script:LastCursor = $null
    Move-To $cursorStart.X $cursorStart.Y 9999 # il cursore torna dov'era
    "processi explorer: " + ((Get-Process explorer | ForEach-Object { "$($_.Id)" }) -join ', ') + " (barra: $shellPid); finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows)
}
