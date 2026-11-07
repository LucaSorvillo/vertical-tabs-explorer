# Gruppi della barra delle cartelle ([NavBar] Enabled), come le cartelle dei segnalibri di Chrome.
# Finestra di prova in un explorer.exe SEPARATO (/separate), finestre dell'utente isolate (Enter-VtIsolation). Il processo
# di prova usa il suo file dei gruppi (navbar-groups-test.json): quello dell'utente (navbar-groups.json) non si tocca.
# Quattro cartelle di prova aggiunte ad Accesso rapido e tolte alla fine (le fissate dell'utente devono tornare
# identiche, elementi e ordine); i nomi delle cartelle dell'utente non si stampano mai (nel registro sono "f:*").
# Gruppi creati, riempiti, annidati, aperti, rinominati ed eliminati con i comandi di prova ("navbar group ...") e con
# UI Automation (pulsanti, menu, riquadro del nome); il trascinamento con "navbar group drop" (lo stesso calcolo del
# bersaglio del trascinamento vero, con il punto dato su un pulsante); senza -Mouse nessun input reale.
# Con -Mouse anche mouse e tastiera VERI nella sola finestra di prova (clic, passaggio sopra, trascinamenti, Esc, clic
# destro, il nome scritto con la tastiera), con la guardia di Test-NavBar.ps1: prima di ogni movimento, pressione o
# tasto controlla che in primo piano ci sia la finestra di prova e che sotto il punto non ci sia altro.
# -MouseOnly: solo la preparazione e i passi col mouse vero (per una diagnosi più breve).
param([switch]$Keep, [switch]$Mouse, [switch]$MouseOnly, [switch]$DefineOnly) # -DefineOnly (con il punto): solo le funzioni, per le sonde
$ErrorActionPreference = 'Stop'
# Caricato con il punto, Test-NavBar.ps1 rimette i SUOI parametri in questo ambito (DefineOnly compreso): il nostro
# valore si tiene prima.
$groupsDefineOnly = [bool]$DefineOnly
. (Join-Path $PSScriptRoot 'Test-NavBar.ps1') -DefineOnly -Keep:$Keep -Mouse:($Mouse -or $MouseOnly)

$groupsFile = Join-Path $XpxData 'navbar-groups-test.json'
$userGroupsFile = Join-Path $XpxData 'navbar-groups.json'
# Finestre a comparsa della finestra di prova (elenchi, menu, riquadro del nome), senza quelle dei suggerimenti: chiuso un
# elenco, XAML rimette il fuoco della tastiera sul pulsante che l'ha aperto e WinUI ne mostra il suggerimento
function Get-Popups([int64]$Main) {
    $tip = New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::ToolTip)
    @($XAE::FromHandle([IntPtr]$Main).FindAll($XTS::Children, (New-Object Windows.Automation.PropertyCondition($XAE::ClassNameProperty, 'Microsoft.UI.Content.PopupWindowSiteBridge'))) |
            Where-Object { -not $_.FindFirst($XTS::Descendants, $tip) })
}
# Pulsanti degli elenchi aperti (uno per finestra a comparsa), dall'alto in basso
function Get-PopupButtons($Popup) {
    @($Popup.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::Button))) |
            Sort-Object { $_.Current.BoundingRectangle.Y })
}
function Get-PopupLists([int64]$Main) {
    foreach ($p in Get-Popups $Main) { , @(Get-PopupButtons $p | ForEach-Object { $_.Current.Name }) }
}
# L'elenco aperto che contiene esattamente queste voci, in quest'ordine (nomi separati da virgole)
function Test-PopupList([int64]$Main, [string]$Expected) {
    foreach ($names in Get-PopupLists $Main) { if (($names -join ',') -eq $Expected) { return $true } }
    $false
}
function Format-Popups([int64]$Main) { (@(Get-PopupLists $Main | ForEach-Object { '[' + (@($_ | ForEach-Object { if ($_ -like 'xpx*') { $_ } else { '*' } }) -join ',') + ']' })) -join ' ' }
function Find-PopupElement([int64]$Main, $ControlType, [string]$Name) {
    $c = New-Object Windows.Automation.AndCondition((New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, $Name)),
        (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, $ControlType)))
    foreach ($p in Get-Popups $Main) { $e = $p.FindFirst($XTS::Descendants, $c); if ($e) { return $e } }
    $null
}
function Invoke-Element($Element) { $Element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
# Struttura dei gruppi dal registro ("navbar group dump"): primo livello (solo le voci di prova: le cartelle
# dell'utente sono "f:*"), contenuto di ogni gruppo, elenchi aperti
function Get-GroupDump([int64]$Main) {
    $from = LogCount
    Send-AndWait $Main 'navbar group dump'
    $out = [pscustomobject]@{ Top = ''; Open = ''; Groups = @{}; Lit = ''; Animations = ''; Tips = @{} }
    foreach ($l in @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*NavBar\[gruppi\]: " } | ForEach-Object { $_ -replace '^.*NavBar\[gruppi\]: ', '' })) {
        if ($l -match '^primo livello: (.*)\.$') { $out.Top = (@($Matches[1] -split ', ' | Where-Object { $_ -and $_ -ne 'f:*' }) -join ',') }
        elseif ($l -match '^elenchi aperti: (.*)\.$') { $out.Open = $Matches[1] }
        # pulsanti della barra evidenziati (elenco aperto) e animazioni dell'elenco aperto ("sì", "no", "-")
        elseif ($l -match '^evidenziati: (.*); animazioni dell.elenco aperto: (.*)\.$') { $out.Lit = $Matches[1]; $out.Animations = $Matches[2] }
        # suggerimenti dei gruppi della barra (il contenuto, come è sul pulsante): "g:nome = 2 cartelle; ..."
        elseif ($l -match '^suggerimenti: (.*)\.$' -and $Matches[1] -ne 'nessuno') { foreach ($pair in ($Matches[1] -split '; ')) { $kv = $pair -split ' = ', 2; if ($kv.Count -eq 2) { $out.Tips[$kv[0]] = $kv[1] } } }
        elseif ($l -match '^g:(.+?) = \[(.*)\]\.$') { $out.Groups[$Matches[1]] = ($Matches[2] -replace ', ', ',') }
    }
    $out
}
function Format-Dump($D) { "primo livello [$($D.Top)]; " + ((@($D.Groups.Keys | Sort-Object | ForEach-Object { "$_=[$($D.Groups[$_])]" })) -join ' ') + "; aperti: $($D.Open)" }
# Comando di prova e attesa della sua fine: la mod scrive 'comando di prova "<comando>"' quando ha finito (con il
# thread della finestra occupato, ad esempio dalle schede nuove in coda, 1,2 s fissi non bastavano)
function Send-AndWait([int64]$Main, [string]$Command, [int]$TimeoutMs = 6000) {
    $from = LogCount
    Send-ModCommand $Main $Command
    $pattern = "\[\s*$sepPid\:.*comando di prova `"" + [regex]::Escape($Command) + '"'
    Wait-Until { @(ModLines $from | Where-Object { $_ -match $pattern }).Count -gt 0 } $TimeoutMs | Out-Null
}
function Send-Group([int64]$Main, [string]$Command) { Send-AndWait $Main "navbar group $Command" }
# Ordine delle cartelle di prova in Windows (fissate di Esplora file)
function Get-WindowsOrder { (@($script:pinPaths | Where-Object { [XpxPin.PinKit]::IndexOf($_) -ge 0 } | Sort-Object { [XpxPin.PinKit]::IndexOf($_) } | ForEach-Object { Split-Path $_ -Leaf })) -join ',' }
# Cartelle di prova di primo livello nell'ordine della barra (dal registro)
function Get-TopPins($D) { (@($D.Top -split ',' | Where-Object { $_ -like 'f:xpx-pin-*' } | ForEach-Object { $_.Substring(2) })) -join ',' }
# Cartelle di prova nell'ordine della barra letta in profondità (dal registro): il primo livello e, al posto di ogni
# gruppo, il suo contenuto (sottogruppi compresi, prima di passare alla voce dopo)
function Get-DfsPins($D) {
    $out = New-Object System.Collections.Generic.List[string]
    $visit = $null
    $visit = {
        param([string]$Key, [int]$Depth)
        if ($Depth -gt 20) { return }
        if ($Key -like 'g:*') {
            $name = $Key.Substring(2)
            if ($D.Groups.ContainsKey($name) -and $D.Groups[$name]) { foreach ($child in ($D.Groups[$name] -split ',')) { & $visit $child ($Depth + 1) } }
        }
        elseif ($Key -like 'f:xpx-pin-*') { $out.Add($Key.Substring(2)) }
    }
    foreach ($key in ($D.Top -split ',')) { if ($key) { & $visit $key 0 } }
    $out -join ','
}
# Le cartelle di prova sono in Windows (quindi nel riquadro di sistema, nella Home...) nell'ordine della barra letta in
# profondità
function Test-TopOrder($D) { $want = Get-DfsPins $D; Wait-Until { $want -eq (Get-WindowsOrder) } 3000 }
# Cartelle di prova nel riquadro di spostamento di sistema della scheda attiva (anche se nascosto dalla barra), in ordine
function Get-NativePins([int64]$Main) {
    $tree = (Get-TabContent $Main).Tree
    if ($tree -eq [IntPtr]::Zero) { return '(albero assente)' }
    (@([NavKit]::NativeNav($tree) | Where-Object { $_ -like 'xpx-pin-*' })) -join ','
}
# Voce della barra (indice per i comandi di prova) e blocco delle cartelle aggiunte, da "navbar info"
function Get-BarInfo([int64]$Main) {
    $from = LogCount
    Send-AndWait $Main 'navbar info'
    $lines = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:" })
    $line = @($lines | Where-Object { $_ -match 'gruppi nella barra alle voci: ' }) | Select-Object -Last 1
    $visible = @($lines | Where-Object { $_ -match 'nella barra si vedono \d+ voci' }) | Select-Object -Last 1
    $out = [pscustomobject]@{ Groups = @(); First = -1; End = -1; Count = -1; Visible = -1 }
    if ("$line" -match 'gruppi nella barra alle voci: (.+) \(blocco (\d+)-(\d+), (\d+) voci\)') {
        $out.Groups = @(if ($Matches[1] -ne 'nessuna') { $Matches[1] -split ', ' | ForEach-Object { [int]$_ } })
        $out.First = [int]$Matches[2]; $out.End = [int]$Matches[3]; $out.Count = [int]$Matches[4]
    }
    if ("$visible" -match 'nella barra si vedono (\d+) voci') { $out.Visible = [int]$Matches[1] }
    $out
}
# Menu classico aperto (#32768) del processo di prova: voci; Sub apre il sottomenu della voce e ne dà le voci
function Wait-ClassicMenu { $script:menus = @(); Wait-Until { $script:menus = @([NavKit]::Menus([uint32]$sepPid)); $script:menus.Count -gt 0 } 5000 | Out-Null; $script:menus }
function Close-ClassicMenus { foreach ($m in [NavKit]::Menus([uint32]$sepPid)) { [void][NavKit]::PostMessage($m, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) }; Wait-Until { [NavKit]::Menus([uint32]$sepPid).Count -eq 0 } 3000 | Out-Null }
function Select-ClassicItem([IntPtr]$MenuWindow, [int]$Position) {
    [void][NavKit]::SendMessage($MenuWindow, 0x01E5, [IntPtr]$Position, [IntPtr]::Zero) # MN_SELECTITEM
    [void][NavKit]::PostMessage($MenuWindow, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero)       # Invio
}
function Open-ClassicSubmenu([IntPtr]$MenuWindow, [int]$Position) {
    [void][NavKit]::SendMessage($MenuWindow, 0x01E5, [IntPtr]$Position, [IntPtr]::Zero) # MN_SELECTITEM
    [void][NavKit]::SendMessage($MenuWindow, 0x01E3, [IntPtr]::Zero, [IntPtr]::Zero)    # MN_OPENHIERARCHY
    $script:sub = $null
    if (Wait-Until { $script:sub = @([NavKit]::Menus([uint32]$sepPid) | Where-Object { $_ -ne $MenuWindow }) | Select-Object -First 1; $null -ne $script:sub } 4000) { $script:sub }
}
# Righe di errore della mod nel processo di prova dalla riga From
function Get-ModErrors([int]$From) { @(ModLines $From | Where-Object { $_ -match "\[\s*$sepPid\:" -and $_ -match 'NavBar: errore|NavGroups: .*(illeggibile|non salvati)|ModernMenu: .*errore' }) }
# Schede della finestra di prova (UI Automation) anche mentre Esplora file ne crea: una finestra figlia che sparisce a
# metà lettura fa fallire la chiamata, e allora si riprova
# Prima finestra di Esplora file (CabinetWClass visibile) di uno dei processi indicati; 0 se non c'è
function Find-ProcessWindow([int[]]$Pids) {
    if (-not $Pids) { return 0 }
    $h = [XpxKit]::FindWindowEx([IntPtr]::Zero, [IntPtr]::Zero, 'CabinetWClass', [IntPtr]::Zero)
    while ($h -ne [IntPtr]::Zero) {
        $p = 0; [void][NavKit]::GetWindowThreadProcessId($h, [ref]$p)
        if ($Pids -contains [int]$p -and [XpxKit]::IsWindowVisible($h)) { return [int64]$h }
        $h = [XpxKit]::FindWindowEx([IntPtr]::Zero, $h, 'CabinetWClass', [IntPtr]::Zero)
    }
    0
}
function Get-TabNamesSafe([int64]$Main) {
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        try { return @(Get-TabNames $Main) } catch { Start-Sleep -Milliseconds 300 }
    }
    @()
}

# --- Mouse e tastiera veri (-Mouse): le funzioni di Test-NavBar.ps1 (Move-To, Glide, Hold, Mouse-Down/Up, Press-Escape,
# con la guardia Assert-Ours) più clic destro, tasti e testo
function Mouse-Click { Mouse-Down; Start-Sleep -Milliseconds 60; Mouse-Up }
function Mouse-RightClick {
    $c = Get-Cursor; Assert-Ours $c.X $c.Y 'clic destro'
    [NavMouse]::mouse_event(0x08, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60; [NavMouse]::mouse_event(0x10, 0, 0, 0, [IntPtr]::Zero)
}
function Send-Key([byte]$Vk) {
    Assert-Ours -1 -1 'tasto'
    [NavMouse]::keybd_event($Vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 30; [NavMouse]::keybd_event($Vk, 0, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 40
}
# Solo lettere: il tasto virtuale è la lettera maiuscola, senza Maiusc esce minuscola
function Type-Text([string]$Text) { foreach ($ch in $Text.ToUpperInvariant().ToCharArray()) { Send-Key ([byte][char]$ch) } }
# Elemento col fuoco della tastiera (UI Automation): "tipo|nome"
function Get-FocusedName { try { $f = $XAE::FocusedElement; "$($f.Current.ControlType.ProgrammaticName)|$($f.Current.Name)" } catch { '' } }
# Punto di un elemento a una frazione della sua larghezza e altezza (pixel dello schermo)
function Point-Of($Element, [double]$FX, [double]$FY) { $r = $Element.Current.BoundingRectangle; @([int]($r.X + $r.Width * $FX), [int]($r.Y + $r.Height * $FY)) }
# Da un punto di UI Automation (in questo processo, non DPI-aware, in coordinate logiche) ai pixel fisici dello schermo,
# quelli della mod: in proporzione alla finestra, letta senza e con la consapevolezza DPI del thread
if (-not ('XpxDpi' -as [type])) {
    Add-Type -Namespace '' -Name XpxDpi -MemberDefinition @'
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern System.IntPtr SetThreadDpiAwarenessContext(System.IntPtr context);
'@
}
function To-Physical([int64]$Main, $Point) {
    $logical = New-Object XpxDpi+RECT
    [void][XpxDpi]::GetWindowRect([IntPtr]$Main, [ref]$logical)
    $old = [XpxDpi]::SetThreadDpiAwarenessContext([IntPtr](-4))
    $physical = New-Object XpxDpi+RECT
    try { [void][XpxDpi]::GetWindowRect([IntPtr]$Main, [ref]$physical) } finally { [void][XpxDpi]::SetThreadDpiAwarenessContext($old) }
    $sx = ($physical.Right - $physical.Left) / [double]($logical.Right - $logical.Left)
    $sy = ($physical.Bottom - $physical.Top) / [double]($logical.Bottom - $logical.Top)
    @([int]($physical.Left + ($Point[0] - $logical.Left) * $sx), [int]($physical.Top + ($Point[1] - $logical.Top) * $sy))
}
# Trascinamento di prova ("navbar group dragto"): il cursore di prova sul punto (UI Automation) dell'elemento
function Send-DragTo([int64]$Main, $Point) { $p = To-Physical $Main $Point; Send-Group $Main "dragto $($p[0])|$($p[1])"; $p }

if ($groupsDefineOnly) { return }

$trayWnd = [NavKit]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero)
$shellPid = 0; [void][NavKit]::GetWindowThreadProcessId($trayWnd, [ref]$shellPid)
$settingsFile = Join-Path $XpxData 'settings.ini'
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente: prima chiudere l''altra prova' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userGroupsBefore = if (Test-Path $userGroupsFile) { [IO.File]::ReadAllBytes($userGroupsFile) } else { $null }
$userWindows = Get-UserWindowCount
$userTabs = Get-UserTabCount
$T = $XpxTestRoot
$sepPid = 0
$script:startedPids = @() # processi explorer.exe avviati dalla prova (chiusi comunque alla fine)
$script:pinPaths = @()
$pinsSaved = $false
$logStart = 0
try {
    Enter-VtIsolation
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $groupsFile -ErrorAction SilentlyContinue
    'A', 'B' | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $T $_) | Out-Null }
    $script:pinPaths = @('xpx-pin-1', 'xpx-pin-2', 'xpx-pin-3', 'xpx-pin-4' | ForEach-Object { Join-Path $T "pins\$_" })
    foreach ($p in $script:pinPaths) { New-Item -ItemType Directory -Force $p | Out-Null }
    $savedCount = [XpxPin.PinKit]::Save()
    $pinsSaved = $true
    $added = @($script:pinPaths | ForEach-Object { Add-Pin $_ })
    "cartelle di prova aggiunte ad Accesso rapido: $(@($added | Where-Object { $_ }).Count) su 4 (fissate prima: $savedCount)"
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1'
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Set-XpxOption 'NavBar' 'Enabled' '1'
    Start-Tray
    Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000 | Out-Null

    $logStart = LogCount
    $explorersBefore = @(Get-Process explorer -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 20000 | Out-Null
    $script:startedPids += @(Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $explorersBefore -notcontains $_.Id -and $_.Id -ne $shellPid } | ForEach-Object { $_.Id })
    $w = @(Get-TestWindows)[0]
    if (-not $w) { throw 'la finestra di prova non è comparsa entro 20 s' }
    Add-VtTestWindow $w
    [void][NavKit]::GetWindowThreadProcessId([IntPtr]$w, [ref]$sepPid)
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "la finestra di prova non è in un processo separato ($sepPid): mi fermo" }
    "finestra di prova {0:x} nel processo separato $sepPid" -f $w
    Move-TestWindow $w 60 50 1300 760
    Start-Sleep -Milliseconds 800
    & $XpxExe --inject-pid $sepPid
    Wait-Until { (ModLines $logStart) -match "\[\s*$sepPid\:.*NavBar: \d+ voci" } 20000 | Out-Null
    Wait-Until { (Get-GroupDump $w).Top -match 'xpx-pin-4' } 8000 | Out-Null
    $groupsLog = @(ModLines $logStart | Where-Object { $_ -match "\[\s*$sepPid\:.*NavGroups: \d+ gruppi \((.+?)\)" } | Select-Object -First 1)
    Check 'il processo di prova usa il file dei gruppi di prova (senza gruppi all''inizio)' ("$groupsLog" -match 'NavGroups: 0 gruppi \(navbar-groups-test\.json') ("$groupsLog" -replace '^.*NavGroups: ', '')

    if ($MouseOnly) {
    # Solo i passi col mouse vero: il gruppo che il loro blocco si aspetta già fatto (xpxC, vuoto)
    Send-Group $w 'new xpxC'
    }
    else {
    "--- gruppo nuovo e cartelle dentro"
    $d0 = Get-GroupDump $w
    $orderBefore = Get-WindowsOrder
    Send-Group $w 'new xpxA'
    Send-Group $w 'put f:xpx-pin-1|xpxA'
    Send-Group $w 'put f:xpx-pin-2|xpxA'
    $d = Get-GroupDump $w
    Check 'gruppo nuovo in fondo al blocco, con due cartelle dentro (nell''ordine in cui sono entrate)' ($d.Groups['xpxA'] -eq 'f:xpx-pin-1,f:xpx-pin-2' -and $d.Top -eq 'f:xpx-pin-3,f:xpx-pin-4,g:xpxA') (Format-Dump $d)
    # In Windows (riquadro di sistema, Home...) le cartelle del gruppo vanno al posto del gruppo, in fondo
    Wait-Until { (Get-WindowsOrder) -eq 'xpx-pin-3,xpx-pin-4,xpx-pin-1,xpx-pin-2' } 4000 | Out-Null
    Check '  le cartelle restano aggiunte ad Accesso rapido; in Windows sono nell''ordine della barra letta in profondità' ($orderBefore -eq 'xpx-pin-1,xpx-pin-2,xpx-pin-3,xpx-pin-4' -and (Get-WindowsOrder) -eq 'xpx-pin-3,xpx-pin-4,xpx-pin-1,xpx-pin-2' -and (Test-TopOrder $d)) "Windows: $(Get-WindowsOrder) (prima $orderBefore)"
    $native = Get-NativePins $w
    Check '  il riquadro di sistema (nascosto dalla barra) è già nello stesso ordine' ($native -eq (Get-WindowsOrder)) "riquadro: $native"
    $json = $null
    try { $json = Get-Content -LiteralPath $groupsFile -Raw -Encoding UTF8 | ConvertFrom-Json } catch { }
    $jsonGroup = @($json.groups | Where-Object { $_.name -eq 'xpxA' }) | Select-Object -First 1
    Check '  salvato subito nel file di prova (nome e contenuto)' ($jsonGroup -and @($jsonGroup.children).Count -eq 2 -and "$($jsonGroup.children[0])" -like 'f:*\xpx-pin-1' -and "$($jsonGroup.children[1])" -like 'f:*\xpx-pin-2') "file: $(if ($json) { "$(@($json.groups).Count) gruppi" } else { 'assente o illeggibile' })"
    $userSame = if ($userGroupsBefore) { (Test-Path $userGroupsFile) -and ([Convert]::ToBase64String([IO.File]::ReadAllBytes($userGroupsFile)) -eq [Convert]::ToBase64String($userGroupsBefore)) } else { -not (Test-Path $userGroupsFile) }
    Check '  il file dei gruppi dell''utente non è toccato' $userSame

    "--- in testa al blocco, nella barra, ed elenco del gruppo"
    Send-Group $w 'place g:xpxA|-|^'
    $d = Get-GroupDump $w
    $info = Get-BarInfo $w
    $button = Find-BarButton $w 'xpxA'
    Check 'gruppo spostato in testa al blocco delle cartelle aggiunte: è il primo pulsante del blocco nella barra' ($d.Top -like 'g:xpxA,*' -and $info.Groups -contains $info.First -and $button -and $button.Current.AutomationId -eq 'NavGroup') "$(Format-Dump $d); gruppi alle voci $($info.Groups -join ','), blocco da $($info.First)"
    $tip = if ($button) { $button.Current.HelpText } else { '' }
    if ($button) { Invoke-Element $button }
    $ok = Wait-Until { Test-PopupList $w 'xpx-pin-1,xpx-pin-2' } 4000
    Check '  clic sul gruppo: l''elenco con le sue cartelle, in ordine' $ok (Format-Popups $w)
    # Testo uguale nella barra e negli elenchi (come Chrome): stessa altezza della riga di testo (UI Automation)
    $textCond = New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::Text)
    $barText = if ($button) { $button.FindFirst($XTS::Descendants, $textCond) } else { $null }
    $rowEl = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpx-pin-1'
    $rowText = if ($rowEl) { $rowEl.FindFirst($XTS::Descendants, $textCond) } else { $null }
    $hBar = if ($barText) { $barText.Current.BoundingRectangle.Height } else { -1 }
    $hRow = if ($rowText) { $rowText.Current.BoundingRectangle.Height } else { -1 }
    Check '  testo dell''elenco grande come quello della barra' ($hBar -gt 0 -and $hRow -gt 0 -and [Math]::Abs($hBar - $hRow) -le 1) "altezza della riga di testo: barra $hBar, elenco $hRow"
    $row = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpx-pin-2'
    if ($row) { Invoke-Element $row }
    $ok = Wait-Until { (Get-ActivePath $w).Paths -contains $script:pinPaths[1] } 6000
    Check '  clic su una cartella dell''elenco: si apre nella scheda attiva e l''elenco si chiude' ($ok -and (Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000)) "schede: $(((Get-ActivePath $w).Paths | ForEach-Object { Short $_ }) -join ' | ')"

    "--- gruppi annidati"
    Send-Group $w 'newin xpxA|xpxB'
    Send-Group $w 'put f:xpx-pin-3|xpxB'
    $d = Get-GroupDump $w
    Check 'sottogruppo in fondo al gruppo, con una cartella' ($d.Groups['xpxA'] -eq 'f:xpx-pin-1,f:xpx-pin-2,g:xpxB' -and $d.Groups['xpxB'] -eq 'f:xpx-pin-3' -and $d.Top -like 'g:xpxA,f:xpx-pin-4') (Format-Dump $d)
    $button = Find-BarButton $w 'xpxA'
    if ($button) { Invoke-Element $button }
    $ok = Wait-Until { Test-PopupList $w 'xpx-pin-1,xpx-pin-2,xpxB' } 4000
    $groupRow = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpxB'
    Check '  nell''elenco del gruppo il sottogruppo è una riga a sé (gruppo)' ($ok -and $groupRow -and $groupRow.Current.AutomationId -eq 'NavGroupRow') (Format-Popups $w)
    if ($groupRow) { Invoke-Element $groupRow }
    $ok = Wait-Until { Test-PopupList $w 'xpx-pin-3' } 4000
    $both = Test-PopupList $w 'xpx-pin-1,xpx-pin-2,xpxB'
    $d = Get-GroupDump $w
    Check '  clic sul sottogruppo: il suo elenco si apre a lato e quello del gruppo resta aperto' ($ok -and $both -and $d.Open -eq 'g:xpxA > g:xpxB') "$(Format-Popups $w); aperti: $($d.Open)"
    Send-ModCommand $w 'navbar closemenu'
    Check '  si chiudono insieme ("navbar closemenu")' (Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000)

    "--- gruppo vuoto"
    Send-Group $w 'new xpxE'
    for ($k = 0; $k -lt 3 -and (Get-GroupDump $w).Open -notmatch 'g:xpxE'; $k++) { Send-Group $w 'show xpxE' }
    $empty = Find-PopupElement $w ([Windows.Automation.ControlType]::Text) (Ui 'Empty group' 'Gruppo vuoto')
    Check 'gruppo vuoto: l''elenco dice "(vuoto)"' ($null -ne $empty) "$(Format-Popups $w); aperti: $((Get-GroupDump $w).Open)"
    Send-ModCommand $w 'navbar closemenu'
    Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000 | Out-Null

    "--- con un elenco aperto, passando su un altro gruppo (come Chrome); gruppo aperto evidenziato"
    # Senza mouse: "hover <gruppo>" (">>" = il pulsante ») fa vedere al controllo del cambio (OnSwitchTimer, ogni 16 ms) il
    # cursore su quel pulsante della barra. I due gruppi devono essere nella barra e non nell'elenco » (a scale alte la
    # finestra di prova mostra poche voci): per questo blocco xpxE (in fondo al blocco) va in testa, prima di xpxA, e alla
    # fine torna in fondo.
    $topBeforeHover = (Get-GroupDump $w).Top
    Send-Group $w 'place g:xpxE|-|^'
    $info = Get-BarInfo $w
    $d = Get-GroupDump $w
    $bothOnBar = ($info.Groups -contains $info.First) -and ($info.Groups -contains ($info.First + 1)) -and ($info.First + 1) -lt $info.Visible
    Check 'per la prova: xpxE in testa al blocco e xpxA subito dopo, tutti e due nella barra (non nell''elenco »)' ($bothOnBar -and $d.Top -like 'g:xpxE,g:xpxA,*') "$(Format-Dump $d); gruppi alle voci $($info.Groups -join ','), blocco da $($info.First), si vedono $($info.Visible) voci"
    for ($k = 0; $k -lt 3 -and (Get-GroupDump $w).Open -ne 'g:xpxA'; $k++) { Send-Group $w 'show xpxA' }
    $d = Get-GroupDump $w
    Check 'elenco aperto dalla barra: il suo gruppo è evidenziato, solo lui' ($d.Open -eq 'g:xpxA' -and $d.Lit -eq 'g:xpxA') "aperti: $($d.Open); evidenziati: $($d.Lit)"
    # Cursore di prova su un altro pulsante: cambio d'elenco atteso e millisecondi dal comando (dal registro della mod)
    $hover = {
        param([string]$Target, [string]$ExpectedOpen)
        $from = LogCount
        Send-Group $w "hover $Target" | Out-Null
        $ok = Wait-Until { (Get-GroupDump $w).Open -eq $ExpectedOpen } 3000
        $line = "$(@(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*elenco cambiato passandoci sopra \(prova\), \d+ ms" }) | Select-Object -First 1)"
        $ms = -1
        if ($line -match '\(prova\), (\d+) ms') { $ms = [int]$Matches[1] }
        [pscustomobject]@{ Switched = [bool]$ok; Ms = $ms; Dump = (Get-GroupDump $w) }
    }
    $r = & $hover 'xpxE' 'g:xpxE'
    Check '  sul pulsante di un altro gruppo si apre subito il suo, senza animazioni' ($r.Switched -and $r.Ms -ge 0 -and $r.Ms -le 60 -and $r.Dump.Animations -eq 'no') "cambiato $($r.Ms) ms dopo il comando; aperti: $($r.Dump.Open); animazioni: $($r.Dump.Animations)"
    Check '  evidenziato ora solo il gruppo aperto' ($r.Dump.Lit -eq 'g:xpxE') "evidenziati: $($r.Dump.Lit)"
    $r = & $hover '>>' '»'
    Check '  sul pulsante » si apre subito il suo elenco (le voci che non entrano), evidenziato solo »' ($r.Switched -and $r.Ms -ge 0 -and $r.Ms -le 60 -and $r.Dump.Lit -eq '»') "cambiato $($r.Ms) ms dopo il comando; aperti: $($r.Dump.Open); evidenziati: $($r.Dump.Lit)"
    $r = & $hover 'xpxA' 'g:xpxA'
    Check '  e da » di nuovo a un gruppo della barra' ($r.Switched -and $r.Ms -ge 0 -and $r.Ms -le 60 -and $r.Dump.Lit -eq 'g:xpxA') "cambiato $($r.Ms) ms dopo il comando; aperti: $($r.Dump.Open); evidenziati: $($r.Dump.Lit)"
    Send-Group $w 'hover off'
    Send-ModCommand $w 'navbar closemenu'
    Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000 | Out-Null
    $d = Get-GroupDump $w
    Check '  chiuso l''elenco, nessun gruppo evidenziato' ($d.Open -eq 'nessuno' -and $d.Lit -eq 'nessuno') "aperti: $($d.Open); evidenziati: $($d.Lit)"
    for ($k = 0; $k -lt 3 -and (Get-GroupDump $w).Open -ne 'g:xpxE'; $k++) { Send-Group $w 'show xpxE' }
    $d = Get-GroupDump $w
    Check '  riaperto (come col clic): di nuovo con le animazioni' ($d.Open -eq 'g:xpxE' -and $d.Animations -eq 'sì') "aperti: $($d.Open); animazioni: $($d.Animations)"
    Send-ModCommand $w 'navbar closemenu'
    Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000 | Out-Null
    # Aperto col clic (il flyout del pulsante, UI Automation), cambiato passandoci sopra e chiuso: il clic sul primo gruppo
    # riapre il suo elenco (come i passi 12 e 13 col mouse vero)
    $ba = Find-BarButton $w 'xpxA'
    if ($ba) { Invoke-Element $ba }
    $okA = Wait-Until { (Get-GroupDump $w).Open -eq 'g:xpxA' } 3000
    $r = & $hover 'xpxE' 'g:xpxE'
    Send-Group $w 'hover off'
    Send-ModCommand $w 'navbar closemenu'
    Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000 | Out-Null
    $ba = Find-BarButton $w 'xpxA'
    if ($ba) { Invoke-Element $ba }
    $okA2 = Wait-Until { (Get-GroupDump $w).Open -eq 'g:xpxA' } 3000
    Check '  aperto col clic, cambiato passandoci sopra e chiuso: il clic sul primo gruppo riapre il suo elenco' ($ba -and $okA -and $r.Switched -and $okA2) "primo clic: $okA; cambio: $($r.Switched); clic dopo: $okA2; aperti: $((Get-GroupDump $w).Open); $(Format-Popups $w)"
    Send-ModCommand $w 'navbar closemenu'
    Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000 | Out-Null
    Send-Group $w 'place g:xpxE|-|-'
    $d = Get-GroupDump $w
    Check '  xpxE rimesso in fondo al blocco: ordine come prima del blocco' ($d.Top -eq $topBeforeHover) "$(Format-Dump $d) (prima [$topBeforeHover])"

    "--- trascinando dall'elenco di un gruppo, fermi su un altro gruppo della barra: si apre il suo (come Chrome)"
    # Senza mouse: "drag <voce>" comincia un trascinamento vero (timer, elenchi che si aprono restando fermi, stacco
    # dall'elenco di partenza) guidato da punto e tasto di prova: "dragto x|y" (pixel fisici, dai punti di UI Automation),
    # "dragup" il rilascio. Per il blocco xpxE va in testa con una cartella, prima di xpxA; alla fine tutto torna com'era.
    $dragBefore = Get-GroupDump $w
    Send-Group $w 'place g:xpxE|-|^'
    Send-Group $w 'put f:xpx-pin-1|xpxE'
    $d = Get-GroupDump $w
    Check 'per la prova: xpxE [xpx-pin-1] in testa al blocco, xpxA [xpx-pin-2, xpxB] subito dopo' ($d.Top -like 'g:xpxE,g:xpxA,*' -and $d.Groups['xpxE'] -eq 'f:xpx-pin-1' -and $d.Groups['xpxA'] -eq 'f:xpx-pin-2,g:xpxB') (Format-Dump $d)
    for ($k = 0; $k -lt 3 -and (Get-GroupDump $w).Open -ne 'g:xpxA'; $k++) { Send-Group $w 'show xpxA' }
    $from = LogCount
    Send-Group $w 'drag f:xpx-pin-2'
    $be = Find-BarButton $w 'xpxE'
    $pe = if ($be) { Send-DragTo $w (Center $be) } else { @() }
    $opened = Wait-Until { (Get-GroupDump $w).Open -eq 'g:xpxE' } 3000
    $detached = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*il trascinamento continua staccato" }).Count -eq 1
    $listE = Wait-Until { Test-PopupList $w 'xpx-pin-1' } 2000
    Check '  dall''elenco di xpxA, fermi su xpxE nella barra: si apre il suo elenco al posto di quello di xpxA, il trascinamento continua' ($be -and $opened -and $detached -and $listE) "punto $($pe -join ','); aperti: $((Get-GroupDump $w).Open); staccato: $detached; $(Format-Popups $w)"
    $row = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpx-pin-1'
    if ($row) { Send-DragTo $w (Point-Of $row 0.3 0.2) | Out-Null }
    Send-Group $w 'dragup'
    $d = Get-GroupDump $w
    $bad = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*(trascinamento annullato|fuori posto|non iniziato)" }).Count
    Check '  lasciata nel suo elenco sopra la prima riga: xpxE [xpx-pin-2, xpx-pin-1]; l''elenco resta aperto e aggiornato' ($row -and $d.Groups['xpxE'] -eq 'f:xpx-pin-2,f:xpx-pin-1' -and $d.Groups['xpxA'] -eq 'g:xpxB' -and $d.Open -eq 'g:xpxE' -and $bad -eq 0 -and (Wait-Until { Test-PopupList $w 'xpx-pin-2,xpx-pin-1' } 2000) -and (Test-TopOrder $d)) "$(Format-Dump $d); annullati o fuori posto: $bad; $(Format-Popups $w); Windows: $(Get-WindowsOrder)"
    # Il pulsante del gruppo resta lo stesso (il suo elenco è rimasto aperto); il suggerimento dice il contenuto nuovo
    $tipE = "$($d.Tips['g:xpxE'])"; $tipA = "$($d.Tips['g:xpxA'])"
    Check '  suggerimenti dei due gruppi nella barra col contenuto nuovo (xpxE 2 cartelle, xpxA 1 gruppo)' ($tipE -eq (Ui '2 folders' '2 cartelle') -and $tipA -eq (Ui '1 group' '1 gruppo')) "xpxE: $tipE; xpxA: $tipA"
    # Da un sottogruppo (elenco di profondità 1): fermi su un gruppo della barra si chiudono gruppo e sottogruppo, si apre il suo
    Send-ModCommand $w 'navbar closemenu'
    Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000 | Out-Null
    for ($k = 0; $k -lt 4 -and (Get-GroupDump $w).Open -ne 'g:xpxA > g:xpxB'; $k++) { Send-Group $w 'show xpxB' }
    $from = LogCount
    Send-Group $w 'drag f:xpx-pin-3'
    $be = Find-BarButton $w 'xpxE'
    if ($be) { Send-DragTo $w (Center $be) | Out-Null }
    $opened = Wait-Until { (Get-GroupDump $w).Open -eq 'g:xpxE' } 3000
    $row = $null
    if ($opened -and (Wait-Until { Test-PopupList $w 'xpx-pin-2,xpx-pin-1' } 2000)) { $row = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpx-pin-1' }
    if ($row) { Send-DragTo $w (Point-Of $row 0.3 0.85) | Out-Null }
    Send-Group $w 'dragup'
    $d = Get-GroupDump $w
    $bad = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*(trascinamento annullato|fuori posto|non iniziato)" }).Count
    Check '  dall''elenco di un sottogruppo: fermi su xpxE si apre il suo; lasciata in fondo, dopo l''ultima riga' ($opened -and $row -and $d.Groups['xpxE'] -eq 'f:xpx-pin-2,f:xpx-pin-1,f:xpx-pin-3' -and $d.Groups['xpxB'] -eq '' -and $d.Open -eq 'g:xpxE' -and $bad -eq 0) "$(Format-Dump $d); annullati o fuori posto: $bad"
    Send-ModCommand $w 'navbar closemenu'
    Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000 | Out-Null
    Send-Group $w 'put f:xpx-pin-3|xpxB'
    Send-Group $w 'place f:xpx-pin-1|xpxA|^'
    Send-Group $w 'place f:xpx-pin-2|xpxA|g:xpxB'
    Send-Group $w 'place g:xpxE|-|-'
    $d = Get-GroupDump $w
    Check '  gruppi e ordine rimessi com''erano (anche in Windows)' ((Format-Dump $d) -eq (Format-Dump $dragBefore) -and (Test-TopOrder $d)) "$(Format-Dump $d) (prima $(Format-Dump $dragBefore)); Windows: $(Get-WindowsOrder)"

    "--- menu del gruppo"
    Send-Group $w 'menu xpxA'
    Wait-Until { @(Get-MenuItems $w).Count -gt 0 } 3000 | Out-Null
    $names = @(Get-MenuItems $w | ForEach-Object { $_.Name })
    Check 'clic destro sul gruppo: Apri tutte (2), Nuovo gruppo, Rinomina, Elimina gruppo' (($names -join '|') -eq "$(Ui 'Open all (2)' 'Apri tutte (2)')|$(Ui 'New group' 'Nuovo gruppo')$([char]0x2026)|$(Ui 'Rename' 'Rinomina')$([char]0x2026)|$(Ui 'Delete group' 'Elimina gruppo')") ($names -join ' | ')
    $tabsBefore = @(Get-TabNamesSafe $w)
    $item = Get-MenuItems $w | Where-Object { $_.Name -eq (Ui 'Open all (2)' 'Apri tutte (2)') } | Select-Object -First 1
    if ($item) { Invoke-Element $item.Element }
    $ok = Wait-Until { @(Get-TabNamesSafe $w).Count -ge $tabsBefore.Count + 2 } 8000
    Start-Sleep -Milliseconds 800
    $tabs = @(Get-TabNamesSafe $w)
    $newTabs = @($tabs | Where-Object { $_ -like 'xpx-pin-*' })
    Check '  "Apri tutte": le due cartelle in due schede nuove, nell''ordine del gruppo' ($ok -and ($newTabs -join ',') -match 'xpx-pin-1,xpx-pin-2') "schede: $(($tabs | ForEach-Object { Short-TabName $_ }) -join ' | ')"
    foreach ($name in 'xpx-pin-1', 'xpx-pin-2') { try { Close-TestTab $w $name } catch { "  (scheda $name non chiusa: $_)" } }
    $info = Get-BarInfo $w
    $from = LogCount
    $tabsBefore = @(Get-TabNamesSafe $w)
    Send-ModCommand $w "navbar newtab $($info.First)" # clic centrale sul gruppo (voce in testa al blocco)
    $ok = Wait-Until { @(Get-TabNamesSafe $w).Count -ge $tabsBefore.Count + 2 } 8000
    Check '  clic centrale sul gruppo: tutte le sue cartelle in schede nuove' ($ok -and @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*2 cartelle del gruppo da aprire in schede nuove" }).Count -eq 1) "$(@(Get-TabNamesSafe $w).Count) schede (prima $($tabsBefore.Count))"
    foreach ($name in 'xpx-pin-1', 'xpx-pin-2') { try { Close-TestTab $w $name } catch { } }

    "--- rinomina e gruppo nuovo con il riquadro del nome"
    Send-Group $w 'menu xpxA'
    $script:rename = $null
    if (Wait-Until { $script:rename = Get-MenuItems $w | Where-Object { $_.Name -eq "$(Ui 'Rename' 'Rinomina')$([char]0x2026)" } | Select-Object -First 1; $null -ne $script:rename } 3000) { Invoke-Element $script:rename.Element }
    $script:box = $null
    $ok = Wait-Until { $script:box = Find-PopupElement $w ([Windows.Automation.ControlType]::Edit) (Ui 'Group name' 'Nome del gruppo'); $null -ne $script:box } 4000
    $value = if ($ok) { $script:box.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value } else { '' }
    Check '"Rinomina…": riquadro con il nome attuale' ($ok -and $value -eq 'xpxA') "valore '$value'"
    if ($ok) {
        $script:box.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('xpxR')
        $save = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) (Ui 'Save' 'Salva')
        if ($save) { Invoke-Element $save }
    }
    Start-Sleep -Milliseconds 800
    $d = Get-GroupDump $w
    Check '  "Salva": gruppo rinominato (nella barra e nel file)' ($d.Groups.ContainsKey('xpxR') -and -not $d.Groups.ContainsKey('xpxA') -and (Find-BarButton $w 'xpxR') -and (Get-Content -LiteralPath $groupsFile -Raw -Encoding UTF8) -match '"xpxR"') (Format-Dump $d)
    Check '  il riquadro si chiude' (Wait-Until { -not (Find-PopupElement $w ([Windows.Automation.ControlType]::Edit) (Ui 'Group name' 'Nome del gruppo')) } 3000)
    # Annulla: il nome resta
    Send-Group $w 'edit xpxR'
    $ok = Wait-Until { $script:box = Find-PopupElement $w ([Windows.Automation.ControlType]::Edit) (Ui 'Group name' 'Nome del gruppo'); $null -ne $script:box } 4000
    if ($ok) {
        $script:box.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('xpxNO')
        $cancel = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) (Ui 'Cancel' 'Annulla')
        if ($cancel) { Invoke-Element $cancel }
    }
    Start-Sleep -Milliseconds 800
    $d = Get-GroupDump $w
    Check '  "Annulla": il nome resta quello di prima' ($ok -and $d.Groups.ContainsKey('xpxR') -and -not $d.Groups.ContainsKey('xpxNO')) (Format-Dump $d)
    # Clic destro sullo sfondo della barra: "Nuovo gruppo…", poi il nome
    Send-Group $w 'barmenu'
    $script:newItem = $null
    if (Wait-Until { $script:newItem = Get-MenuItems $w | Where-Object { $_.Name -eq "$(Ui 'New group' 'Nuovo gruppo')$([char]0x2026)" } | Select-Object -First 1; $null -ne $script:newItem } 3000) { Invoke-Element $script:newItem.Element }
    $ok = Wait-Until { $script:box = Find-PopupElement $w ([Windows.Automation.ControlType]::Edit) (Ui 'Group name' 'Nome del gruppo'); $null -ne $script:box } 4000
    $value = if ($ok) { $script:box.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value } else { '' }
    $d = Get-GroupDump $w
    Check 'clic destro sullo sfondo della barra, "Nuovo gruppo…": gruppo creato in fondo, riquadro del nome aperto' ($ok -and $value -eq (Ui 'New group' 'Nuovo gruppo') -and $d.Top -like ('*,g:' + (Ui 'New group' 'Nuovo gruppo'))) "valore '$value'; $(Format-Dump $d)"
    if ($ok) {
        $script:box.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('xpxC')
        $save = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) (Ui 'Save' 'Salva')
        if ($save) { Invoke-Element $save }
    }
    Start-Sleep -Milliseconds 800
    $d = Get-GroupDump $w
    Check '  col nome scelto' ($d.Groups.ContainsKey('xpxC') -and -not $d.Groups.ContainsKey((Ui 'New group' 'Nuovo gruppo'))) (Format-Dump $d)

    "--- menu di una cartella: Sposta nel gruppo, Togli dal gruppo"
    Send-Group $w 'classic f:xpx-pin-4'
    $menus = @(Wait-ClassicMenu)
    $items = if ($menus.Count -gt 0) { @(Read-ClassicMenu $menus[0]) } else { @() }
    $pos = [array]::IndexOf($items, (Ui 'Move to group' 'Sposta nel gruppo'))
    $sub = @()
    if ($pos -ge 0) {
        $subWindow = Open-ClassicSubmenu $menus[0] $pos
        if ($subWindow) { $sub = @(Read-ClassicMenu $subWindow) }
    }
    $expectedSub = "xpxR|xpxR $([char]0x203A) xpxB|xpxE|xpxC||$(Ui 'New group' 'Nuovo gruppo')$([char]0x2026)"
    Check 'menu classico di una cartella di primo livello: "Sposta nel gruppo" con tutti i gruppi (annidati col percorso), senza "Togli dal gruppo"' ($pos -ge 0 -and $items -notcontains (Ui 'Remove from group' 'Togli dal gruppo') -and ($sub -join '|') -eq $expectedSub) "voce al posto $pos su $($items.Count); sottomenu: $($sub -join ' | ')"
    $k = [array]::IndexOf($sub, 'xpxC')
    if ($subWindow -and $k -ge 0) { Select-ClassicItem $subWindow $k } else { Close-ClassicMenus }
    Wait-Until { [NavKit]::Menus([uint32]$sepPid).Count -eq 0 } 3000 | Out-Null
    Start-Sleep -Milliseconds 600
    $d = Get-GroupDump $w
    Check '  scelto "xpxC": la cartella è nel gruppo' ($d.Groups['xpxC'] -eq 'f:xpx-pin-4' -and $d.Top -notmatch 'xpx-pin-4') (Format-Dump $d)
    Send-Group $w 'classic f:xpx-pin-4'
    $menus = @(Wait-ClassicMenu)
    $items = if ($menus.Count -gt 0) { @(Read-ClassicMenu $menus[0]) } else { @() }
    $pos = [array]::IndexOf($items, (Ui 'Remove from group' 'Togli dal gruppo'))
    if ($pos -ge 0) { Select-ClassicItem $menus[0] $pos } else { Close-ClassicMenus }
    Wait-Until { [NavKit]::Menus([uint32]$sepPid).Count -eq 0 } 3000 | Out-Null
    Start-Sleep -Milliseconds 600
    $d = Get-GroupDump $w
    Check '  "Togli dal gruppo" (nel menu della cartella che è in un gruppo): torna al primo livello subito dopo il gruppo' ($pos -ge 0 -and $d.Groups['xpxC'] -eq '' -and $d.Top -like '*g:xpxC,f:xpx-pin-4*') "voce al posto $pos; $(Format-Dump $d)"
    # Dal menu nuovo di Windows 11 (clic destro): il presenter mostra solo i verbi di Windows e le estensioni installate,
    # quindi le voci dei gruppi sono in "Mostra altre opzioni", il menu classico
    Send-Group $w 'context f:xpx-pin-1'
    $script:more = $null
    if (Wait-Until { $script:more = Find-MenuItemById $w 'expandtoclassic'; $null -ne $script:more } 4000) { Invoke-Element $script:more }
    $menus = @(Wait-ClassicMenu)
    $items = if ($menus.Count -gt 0) { @(Read-ClassicMenu $menus[0]) } else { @() }
    $pos = [array]::IndexOf($items, (Ui 'Remove from group' 'Togli dal gruppo'))
    Check 'menu nuovo di una cartella in un gruppo, "Mostra altre opzioni": "Sposta nel gruppo" e "Togli dal gruppo"' ($null -ne $script:more -and $items -contains (Ui 'Move to group' 'Sposta nel gruppo') -and $pos -ge 0) "$($items.Count) voci"
    $orderBefore = Get-WindowsOrder
    if ($pos -ge 0) { Select-ClassicItem $menus[0] $pos } else { Close-ClassicMenus }
    Wait-Until { [NavKit]::Menus([uint32]$sepPid).Count -eq 0 } 3000 | Out-Null
    Start-Sleep -Milliseconds 1500
    Wait-Until { (Get-WindowsOrder) -ne $orderBefore } 3000 | Out-Null
    $d = Get-GroupDump $w
    Check '  "Togli dal gruppo": la cartella torna al primo livello dopo il gruppo, e in Windows al posto giusto' ($d.Top -like 'g:xpxR,f:xpx-pin-1,*' -and (Test-TopOrder $d)) "$(Format-Dump $d); Windows: $(Get-WindowsOrder)"
    Send-ModCommand $w 'navbar closemenu'
    Wait-Until { @(Get-MenuItems $w).Count -eq 0 } 3000 | Out-Null

    "--- trascinamento (bersaglio calcolato come col mouse)"
    $info = Get-BarInfo $w
    $from = LogCount
    Send-Group $w "drop f:xpx-pin-4|bar|$($info.First)|0.5"
    $d = Get-GroupDump $w
    $target = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*bersaglio di prova: " } | ForEach-Object { $_ -replace '^.*bersaglio di prova: ', '' }) | Select-Object -Last 1
    Check 'cartella lasciata al centro del gruppo nella barra: ci entra (in fondo)' ($d.Groups['xpxR'] -like '*,f:xpx-pin-4' -and $target -match 'dentro 1') "bersaglio: $target; $(Format-Dump $d)"
    $from = LogCount
    Send-Group $w "drop f:xpx-pin-1|bar|$($info.First)|0.1"
    $target = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*bersaglio di prova: " } | ForEach-Object { $_ -replace '^.*bersaglio di prova: ', '' }) | Select-Object -Last 1
    $d = Get-GroupDump $w
    Check '  lasciata sul bordo sinistro del gruppo: prima del gruppo, non dentro (linea nella barra)' ($d.Top -like 'f:xpx-pin-1,g:xpxR,*' -and $target -match 'dentro 0' -and $target -match 'linea nella barra 1' -and (Test-TopOrder $d)) "bersaglio: $target; $(Format-Dump $d); Windows: $(Get-WindowsOrder)"
    # Nell'elenco aperto del gruppo: riordino, dentro un sottogruppo, fuori nella barra
    $button = Find-BarButton $w 'xpxR'
    if ($button) { Invoke-Element $button }
    Wait-Until { (Get-GroupDump $w).Open -eq 'g:xpxR' } 3000 | Out-Null
    $before = $d.Groups['xpxR']
    $from = LogCount
    Send-Group $w 'drop f:xpx-pin-4|list|0|0|0.1'
    $target = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*bersaglio di prova: " } | ForEach-Object { $_ -replace '^.*bersaglio di prova: ', '' }) | Select-Object -Last 1
    $d = Get-GroupDump $w
    Check 'nell''elenco del gruppo: in cima (riordino, con la linea nell''elenco), l''elenco resta aperto e aggiornato' ($d.Groups['xpxR'] -like 'f:xpx-pin-4,*' -and $d.Open -eq 'g:xpxR' -and $target -match "nell'elenco 0" -and (Test-PopupList $w (($d.Groups['xpxR'] -split ',' | ForEach-Object { $_.Substring(2) }) -join ','))) "prima [$before]; bersaglio: $target; $(Format-Dump $d); $(Format-Popups $w)"
    $rows = @($d.Groups['xpxR'] -split ',')
    $bRow = [array]::IndexOf($rows, 'g:xpxB')
    Send-Group $w "drop f:xpx-pin-2|list|0|$bRow|0.5"
    $d = Get-GroupDump $w
    Check '  lasciata al centro del sottogruppo: ci entra' ($d.Groups['xpxB'] -eq 'f:xpx-pin-3,f:xpx-pin-2' -and $d.Groups['xpxR'] -notmatch 'xpx-pin-2') (Format-Dump $d)
    Send-Group $w 'place g:xpxR|xpxB|-'
    $d2 = Get-GroupDump $w
    Check '  un gruppo non entra in un suo sottogruppo' ((Format-Dump $d2) -eq (Format-Dump $d) -or ($d2.Groups['xpxB'] -eq $d.Groups['xpxB'] -and $d2.Top -eq $d.Top)) (Format-Dump $d2)
    # Sull'ultima voce del blocco che si vede nella barra, verso destra: subito dopo di lei
    $info = Get-BarInfo $w
    $last = [Math]::Min($info.End, $info.Visible) - 1
    $from = LogCount
    Send-Group $w "drop f:xpx-pin-4|bar|$last|0.9"
    $target = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*bersaglio di prova: " } | ForEach-Object { $_ -replace '^.*bersaglio di prova: ', '' }) | Select-Object -Last 1
    $d = Get-GroupDump $w
    Check '  dal gruppo alla barra: primo livello, nello stesso ordine in Windows; a vista la linea nella barra' ($d.Top -match 'f:xpx-pin-4' -and $d.Groups['xpxR'] -notmatch 'xpx-pin-4' -and (Test-TopOrder $d) -and $target -match 'linea nella barra 1') "voce $last (visibili $($info.Visible), blocco $($info.First)-$($info.End)); bersaglio: $target; $(Format-Dump $d); Windows: $(Get-WindowsOrder)"
    Send-ModCommand $w 'navbar closemenu'

    "--- elimina gruppo"
    Send-Group $w 'drop g:xpxC|bar|0|0.5' # un gruppo lasciato su una voce della shell (Home): resta nel blocco, in testa
    $d = Get-GroupDump $w
    Check 'gruppo trascinato fuori dal blocco: torna nel blocco (in testa)' ($d.Top -like 'g:xpxC,*') (Format-Dump $d)
    Send-Group $w 'menu xpxR'
    $script:remove = $null
    if (Wait-Until { $script:remove = Get-MenuItems $w | Where-Object { $_.Name -eq (Ui 'Delete group' 'Elimina gruppo') } | Select-Object -First 1; $null -ne $script:remove } 3000) { Invoke-Element $script:remove.Element }
    Start-Sleep -Milliseconds 1500
    $d = Get-GroupDump $w
    $topPins = Get-TopPins $d
    Check '"Elimina gruppo": il contenuto (sottogruppo compreso) torna al primo livello al suo posto' (-not $d.Groups.ContainsKey('xpxR') -and $d.Top -match 'f:xpx-pin-1,g:xpxB' -and $d.Groups['xpxB'] -eq 'f:xpx-pin-3,f:xpx-pin-2') (Format-Dump $d)
    Check '  ordine di Windows delle cartelle di primo livello come nella barra' (Test-TopOrder $d) "barra $topPins; Windows $(Get-WindowsOrder)"

    "--- barra spenta: il riquadro di sistema con l'ordine della barra"
    $d = Get-GroupDump $w
    $dfs = Get-DfsPins $d
    $before = LogCount
    Set-XpxOption 'NavBar' 'Enabled' '0'
    Wait-Until { (ModLines $before) -match "\[\s*$sepPid\:.*barra delle cartelle tolta" } 6000 | Out-Null
    Start-Sleep -Milliseconds 800
    $native = Get-NativePins $w
    $c = Get-TabContent $w
    Check 'barra spenta: il riquadro di sistema torna, con le cartelle nell''ordine della barra letta in profondità' ($c.Region -lt 0 -and $native -eq $dfs -and $dfs -eq (Get-WindowsOrder)) "riquadro: $native; barra in profondità: $dfs; Windows: $(Get-WindowsOrder)"
    $before = LogCount
    Set-XpxOption 'NavBar' 'Enabled' '1'
    Wait-Until { (ModLines $before) -match "\[\s*$sepPid\:.*NavBar: \d+ voci" } 8000 | Out-Null
    Start-Sleep -Milliseconds 1000

    "--- cartella tolta da Accesso rapido dal menu della barra"
    Send-Group $w 'context f:xpx-pin-3'
    $script:unpin = $null
    if (Wait-Until { $script:unpin = Find-MenuItemById $w 'unpinfromhome'; $null -ne $script:unpin } 4000) { Invoke-Element $script:unpin }
    $gone = Wait-Until { [XpxPin.PinKit]::IndexOf($script:pinPaths[2]) -lt 0 } 6000
    Start-Sleep -Milliseconds 1500
    $text = Get-Content -LiteralPath $groupsFile -Raw -Encoding UTF8
    Check '"Rimuovi da Accesso rapido" su una cartella in un gruppo: la barra la dimentica anche nel gruppo' ($gone -and $text -notmatch 'xpx-pin-3') "tolta: $gone"
    Add-Pin $script:pinPaths[2] | Out-Null
    Wait-Until { (Get-GroupDump $w).Top -match 'xpx-pin-3' } 8000 | Out-Null
    $d = Get-GroupDump $w
    Check '  aggiunta di nuovo: al primo livello, non più nel gruppo' ($d.Top -like '*f:xpx-pin-3*' -and $d.Groups['xpxB'] -eq 'f:xpx-pin-2') (Format-Dump $d)

    } # fine dei passi senza mouse (saltati con -MouseOnly)

    if ($Mouse) {
      "--- mouse e tastiera veri nella finestra di prova"
      # Disposizione nota in testa al blocco: xpx-pin-4, xpx-pin-1, xpxM (vuoto), xpxN [xpx-pin-2, xpxS [xpx-pin-3]], xpxC
      Send-Group $w 'new xpxN'
      Send-Group $w 'put f:xpx-pin-2|xpxN'
      Send-Group $w 'newin xpxN|xpxS'
      Send-Group $w 'put f:xpx-pin-3|xpxS'
      Send-Group $w 'new xpxM'
      foreach ($ref in 'g:xpxC', 'g:xpxN', 'g:xpxM', 'f:xpx-pin-1', 'f:xpx-pin-4') { Send-Group $w "place $ref|-|^" }
      $d = Get-GroupDump $w
      Check 'mouse: disposizione di partenza' ($d.Top -like 'f:xpx-pin-4,f:xpx-pin-1,g:xpxM,g:xpxN,g:xpxC*' -and $d.Groups['xpxN'] -eq 'f:xpx-pin-2,g:xpxS' -and $d.Groups['xpxS'] -eq 'f:xpx-pin-3') (Format-Dump $d)
      $script:GuardWindow = [IntPtr]$w
      try {
        # 1) Clic vero sul gruppo: il suo elenco
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 400
        $bn = Find-BarButton $w 'xpxN'
        if ($bn) { $p = Center $bn; Move-To $p[0] $p[1] 12; Start-Sleep -Milliseconds 150; Mouse-Click }
        $ok = Wait-Until { Test-PopupList $w 'xpx-pin-2,xpxS' } 3000
        Check 'mouse: clic sul gruppo, il suo elenco si apre' ($bn -and $ok) (Format-Popups $w)
        # 2) Fermi sulla riga del sottogruppo, senza cliccare: il suo elenco si apre a lato
        $rowS = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpxS'
        if ($rowS) { $p = Center $rowS; Glide $script:LastCursor.X $script:LastCursor.Y $p[0] $p[1] 12; Hold $p[0] $p[1] 900 }
        Check 'mouse: fermi sul sottogruppo (senza clic), il suo elenco si apre a lato' ($rowS -and (Test-PopupList $w 'xpx-pin-3') -and (Test-PopupList $w 'xpx-pin-2,xpxS')) (Format-Popups $w)
        # 3) Fermi su un'altra riga del gruppo: il sottogruppo si chiude, il gruppo resta
        $row2 = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpx-pin-2'
        if ($row2) { $p = Center $row2; Glide $script:LastCursor.X $script:LastCursor.Y $p[0] $p[1] 12; Hold $p[0] $p[1] 900 }
        Check '  fermi su un''altra riga: il sottogruppo si chiude, il gruppo resta' ($row2 -and -not (Test-PopupList $w 'xpx-pin-3') -and (Test-PopupList $w 'xpx-pin-2,xpxS')) (Format-Popups $w)
        # 4) Clic su una cartella dell'elenco: si apre, gli elenchi si chiudono
        if ($row2) { Mouse-Click }
        $ok = Wait-Until { (Get-ActivePath $w).Paths -contains $script:pinPaths[1] } 5000
        Check '  clic sulla cartella dell''elenco: si apre nella scheda attiva, elenchi chiusi' ($ok -and (Wait-Until { @(Get-Popups $w).Count -eq 0 } 3000))
        Start-Sleep -Milliseconds 800
        # 5) Cartella trascinata dalla barra al centro di un gruppo: ci entra (e non si apre)
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $pathsBefore = (Get-ActivePath $w).Paths -join '|'
        $b1 = Find-BarButton $w 'xpx-pin-1'; $bm = Find-BarButton $w 'xpxM'
        if ($b1 -and $bm) {
            $s = Center $b1; $t = Center $bm
            Move-To $s[0] $s[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 100
            try { Glide $s[0] $s[1] $t[0] $t[1] 20; Hold $t[0] $t[1] 300 } finally { Mouse-Up }
            Start-Sleep -Milliseconds 800
        }
        $d = Get-GroupDump $w
        Check 'mouse: cartella trascinata al centro di un gruppo nella barra: ci entra' ($b1 -and $bm -and $d.Groups['xpxM'] -eq 'f:xpx-pin-1' -and $d.Top -notmatch 'xpx-pin-1') (Format-Dump $d)
        Check '  trascinare non apre la cartella' (((Get-ActivePath $w).Paths -join '|') -eq $pathsBefore)
        # 6) Fermi sul gruppo trascinando: il suo elenco si apre, e la cartella si lascia in cima all'elenco
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $b4 = Find-BarButton $w 'xpx-pin-4'; $bn = Find-BarButton $w 'xpxN'
        $opened = $false
        if ($b4 -and $bn) {
            $s = Center $b4; $t = Center $bn
            Move-To $s[0] $s[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 100
            try {
                Glide $s[0] $s[1] $t[0] $t[1] 20
                Hold $t[0] $t[1] 1300
                $first = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpx-pin-2'
                if ($first) {
                    $opened = $true
                    $q = Point-Of $first 0.3 0.2
                    Glide $t[0] $t[1] $q[0] $q[1] 15; Hold $q[0] $q[1] 300
                }
            }
            finally { Mouse-Up }
            Start-Sleep -Milliseconds 800
        }
        $d = Get-GroupDump $w
        Check 'mouse: fermi sul gruppo trascinando, l''elenco si apre; lasciata in cima all''elenco' ($opened -and $d.Groups['xpxN'] -eq 'f:xpx-pin-4,f:xpx-pin-2,g:xpxS') "elenco aperto: $opened; $(Format-Dump $d)"
        Send-ModCommand $w 'navbar closemenu'
        # 7) Dall'elenco del gruppo alla barra, sul bordo sinistro di un gruppo: primo livello prima del gruppo
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $bn = Find-BarButton $w 'xpxN'
        if ($bn) { $p = Center $bn; Move-To $p[0] $p[1] 12; Start-Sleep -Milliseconds 150; Mouse-Click }
        Wait-Until { Test-PopupList $w 'xpx-pin-4,xpx-pin-2,xpxS' } 3000 | Out-Null
        $row = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpx-pin-2'
        $bm = Find-BarButton $w 'xpxM'
        if ($row -and $bm) {
            $s = Center $row; $t = Point-Of $bm 0.08 0.5
            Move-To $s[0] $s[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 100
            try { Glide $s[0] $s[1] $t[0] $t[1] 25; Hold $t[0] $t[1] 300 } finally { Mouse-Up }
            Start-Sleep -Milliseconds 1000
        }
        $d = Get-GroupDump $w
        Check 'mouse: dall''elenco alla barra (bordo sinistro di un gruppo): primo livello prima del gruppo, elenco chiuso' ($row -and $bm -and $d.Top -like 'f:xpx-pin-2,g:xpxM,*' -and $d.Groups['xpxN'] -eq 'f:xpx-pin-4,g:xpxS' -and @(Get-Popups $w).Count -eq 0 -and (Test-TopOrder $d)) "$(Format-Dump $d); Windows: $(Get-WindowsOrder)"
        # 8) Un gruppo trascinato al centro di un altro: ci entra, con il suo contenuto
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $bm = Find-BarButton $w 'xpxM'; $bc = Find-BarButton $w 'xpxC'
        if ($bm -and $bc) {
            $s = Center $bm; $t = Center $bc
            Move-To $s[0] $s[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 100
            try { Glide $s[0] $s[1] $t[0] $t[1] 20; Hold $t[0] $t[1] 300 } finally { Mouse-Up }
            Start-Sleep -Milliseconds 800
        }
        $d = Get-GroupDump $w
        Check 'mouse: gruppo trascinato al centro di un altro: ci entra con il suo contenuto' ($bm -and $bc -and $d.Groups['xpxC'] -like '*g:xpxM' -and $d.Groups['xpxM'] -eq 'f:xpx-pin-1' -and $d.Top -notmatch 'g:xpxM') (Format-Dump $d)
        # 9) Esc durante il trascinamento di un gruppo: annullato, nulla cambia
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $before = Format-Dump (Get-GroupDump $w)
        $bn = Find-BarButton $w 'xpxN'
        $from = LogCount
        if ($bn) {
            $p = Center $bn
            Move-To $p[0] $p[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 100
            try { Glide $p[0] $p[1] ($p[0] + 120) $p[1] 10; Start-Sleep -Milliseconds 200; Press-Escape; Start-Sleep -Milliseconds 300 } finally { Mouse-Up }
            Start-Sleep -Milliseconds 800
        }
        $after = Format-Dump (Get-GroupDump $w)
        Check 'mouse: Esc durante il trascinamento di un gruppo lo annulla' ($bn -and $after -eq $before -and @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*annullato \(Esc\)" }).Count -eq 1) "prima: $before / dopo: $after"
        # 10) Clic destro vero sul gruppo: il suo menu; Esc lo chiude
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $bn = Find-BarButton $w 'xpxN'
        if ($bn) { $p = Center $bn; Move-To $p[0] $p[1] 12; Start-Sleep -Milliseconds 150; Mouse-RightClick }
        $ok = Wait-Until { @(Get-MenuItems $w | Where-Object { $_.Name -like ((Ui 'Open all' 'Apri tutte') + ' (*') }).Count -gt 0 } 3000
        $names = @(Get-MenuItems $w | ForEach-Object { $_.Name })
        if ($ok) { Press-Escape }
        Check 'mouse: clic destro sul gruppo, il suo menu; Esc lo chiude' ($ok -and ($names -contains "$(Ui 'Rename' 'Rinomina')$([char]0x2026)") -and (Wait-Until { @(Get-MenuItems $w).Count -eq 0 } 3000)) ($names -join ' | ')
        # 11) Il nome scritto con la tastiera nel riquadro: tutto selezionato, lettere, Backspace, Invio (Backspace e Invio
        #     non devono arrivare a Esplora file: la scheda non torna indietro)
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $pathsBefore = (Get-ActivePath $w).Paths -join '|'
        Send-Group $w 'edit xpxN'
        $script:focus = ''
        $ok = Wait-Until { $script:focus = Get-FocusedName; $script:focus -like ('*|' + (Ui 'Group name' 'Nome del gruppo')) } 3000
        if ($ok) { Type-Text 'xpxkq'; Send-Key 0x08; Send-Key 0x0D }
        Start-Sleep -Milliseconds 1000
        $d = Get-GroupDump $w
        Check 'mouse: nome scritto con la tastiera (Backspace e Invio nel riquadro, non a Esplora file)' ($ok -and $d.Groups.ContainsKey('xpxk') -and -not $d.Groups.ContainsKey('xpxN') -and ((Get-ActivePath $w).Paths -join '|') -eq $pathsBefore -and @(Get-Popups $w).Count -eq 0) "fuoco: $($script:focus -replace '\|.*$', ''); $(Format-Dump $d)"
        # 12) Con l'elenco di un gruppo aperto, passando sopra un altro gruppo della barra si apre il suo
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $bk = Find-BarButton $w 'xpxk'; $bc = Find-BarButton $w 'xpxC'
        $switched = $false
        if ($bk -and $bc) {
            $p = Center $bk; Move-To $p[0] $p[1] 12; Start-Sleep -Milliseconds 150; Mouse-Click
            Wait-Until { Test-PopupList $w 'xpx-pin-4,xpxS' } 3000 | Out-Null
            $t = Center $bc; Glide $p[0] $p[1] $t[0] $t[1] 10; Hold $t[0] $t[1] 700
            $switched = (Get-GroupDump $w).Open -eq 'g:xpxC'
        }
        Check 'mouse: con un elenco aperto, passando sopra un altro gruppo si apre il suo' $switched "aperti: $((Get-GroupDump $w).Open)"
        Send-ModCommand $w 'navbar closemenu'
        # 13) Trascinando una cartella dall'elenco di un gruppo, fermi su un altro gruppo della barra: il suo elenco si apre
        #     (quello di partenza si chiude) e la cartella si lascia dove si vuole, qui in cima al suo elenco
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        Send-ModCommand $w 'navbar dragdebug on'
        $bk = Find-BarButton $w 'xpxk'
        $opened = $false
        $afterClick = ''
        $from = LogCount
        if ($bk) {
            $p = Center $bk; Move-To $p[0] $p[1] 12; Start-Sleep -Milliseconds 150; Mouse-Click
            $listK = Wait-Until { Test-PopupList $w 'xpx-pin-4,xpxS' } 3000
            $afterClick = "dopo il clic su xpxk: aperti $((Get-GroupDump $w).Open), $(Format-Popups $w)"
            if ($listK) {
                $row = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpx-pin-4'
                $bc = Find-BarButton $w 'xpxC'
                if ($row -and $bc) {
                    $s = Center $row; $t = Center $bc
                    Move-To $s[0] $s[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 100
                    try {
                        Glide $s[0] $s[1] $t[0] $t[1] 25
                        Hold $t[0] $t[1] 1300
                        $first = Find-PopupElement $w ([Windows.Automation.ControlType]::Button) 'xpxM'
                        if ($first) {
                            $opened = $true
                            $q = Point-Of $first 0.3 0.2
                            Glide $t[0] $t[1] $q[0] $q[1] 15; Hold $q[0] $q[1] 300
                        }
                    }
                    finally { Mouse-Up }
                    Start-Sleep -Milliseconds 800
                }
            }
        }
        $d = Get-GroupDump $w
        Send-ModCommand $w 'navbar dragdebug off'
        $bad = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*(trascinamento annullato|fuori posto)" }).Count
        Check 'mouse: dall''elenco di un gruppo, fermi su un altro gruppo della barra si apre il suo; lasciata in cima al suo elenco' ($opened -and $d.Groups['xpxC'] -eq 'f:xpx-pin-4,g:xpxM' -and $d.Groups['xpxk'] -eq 'g:xpxS' -and $bad -eq 0) "elenco aperto: $opened; annullati o fuori posto: $bad; $afterClick; $(Format-Dump $d)"
        if (-not ($opened -and $d.Groups['xpxC'] -eq 'f:xpx-pin-4,g:xpxM')) {
            # Diagnosi: il registro dettagliato del trascinamento di questo passo (senza nomi)
            @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*(NavBar\[trascinamento\]|NavBar: (trascinamento|si apre|voce|cartella|gruppo))" } | Select-Object -First 30) | ForEach-Object { '    ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
        }
        Send-ModCommand $w 'navbar closemenu'
      }
      catch {
        if ("$_" -notlike 'GUARDIA:*') { throw }
        [NavMouse]::mouse_event(0x04, 0, 0, 0, [IntPtr]::Zero)
        "  SALTATE le prove col mouse rimaste: $("$_".Substring(9))"
        $script:mouseSkipped = $true
        Send-ModCommand $w 'navbar closemenu'
      }
      finally { $script:GuardWindow = [IntPtr]::Zero }
    }

    $logStart2 = $null
    if (-not $MouseOnly) {
    "--- i gruppi restano (processo di prova nuovo, con la barra spenta e l'ordine cambiato da fuori)"
    # Un gruppo con due cartelle in fondo (in Windows: in fila, in fondo); poi, a processo chiuso, una delle due portata
    # in cima da fuori. Il processo nuovo parte con la barra spenta: la mod deve rimettere l'ordine dei gruppi e il
    # riquadro di sistema (visibile) mostrarlo.
    Send-Group $w 'new xpxP'
    Send-Group $w 'put f:xpx-pin-1|xpxP'
    Send-Group $w 'put f:xpx-pin-4|xpxP'
    $dumpSaved = Get-GroupDump $w
    $saved = Format-Dump $dumpSaved
    $dfsSaved = Get-DfsPins $dumpSaved
    Wait-Until { (Get-WindowsOrder) -eq $dfsSaved } 4000 | Out-Null
    "  prima: $saved; Windows: $(Get-WindowsOrder)"
    Close-TestWindows
    Start-Sleep -Milliseconds 800
    if ($sepPid -and $sepPid -ne $shellPid) { Stop-Process -Id $sepPid -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 1500
    Set-XpxOption 'NavBar' 'Enabled' '0'
    $hr = [XpxPin.PinKit]::MoveTo($script:pinPaths[3], 0)
    "  ordine di Windows cambiato da fuori (xpx-pin-4 in cima, 0x{0:X8}): $(Get-WindowsOrder)" -f $hr
    $logStart2 = LogCount
    # I processi explorer.exe avviati da qui si ricordano: alla fine si chiudono comunque (mai quello della barra). La
    # finestra nuova si cerca come finestra del processo nuovo (dopo le prove col mouse la shell a volte non ne dava il
    # percorso entro 20 s, e Get-TestWindows non la riconosceva).
    $explorersBefore = @(Get-Process explorer -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'B')`""
    $script:newPids = @()
    Wait-Until { $script:newPids = @(Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $explorersBefore -notcontains $_.Id -and $_.Id -ne $shellPid } | ForEach-Object { $_.Id }); (Find-ProcessWindow $script:newPids) -ne 0 } 20000 | Out-Null
    $script:startedPids += $script:newPids
    $w = Find-ProcessWindow $script:newPids
    if (-not $w) {
        "  finestre viste dalla shell: " + ((@(Get-ShellTabs | ForEach-Object { $p = 0; [void][NavKit]::GetWindowThreadProcessId([IntPtr]$_.Hwnd, [ref]$p); if ($p -ne $shellPid) { "{0}:{1}" -f $p, $(if ($_.Path) { Short $_.Path } else { '(nessun percorso)' }) } })) -join ' ; ')
        throw 'la finestra di prova del processo nuovo non è comparsa entro 20 s'
    }
    Wait-Until { (Get-TabPaths $w) -contains (Join-Path $T 'B') } 10000 | Out-Null
    # Dove si è aperta (solo categorie: i percorsi fuori dalla radice di prova non si stampano)
    $kind = {
        param([string]$Path)
        if (-not $Path) { '(nessun percorso)' }
        elseif (Test-UnderTestRoot $Path) { Short $Path }
        elseif ($Path -like '::*') { '<virtuale>' }
        elseif ($Path.TrimEnd('\') -eq $env:TEMP.TrimEnd('\')) { '<Temp, sopra la radice di prova>' }
        elseif ($Path.StartsWith($env:USERPROFILE, [StringComparison]::OrdinalIgnoreCase)) {
            # Relativo al profilo, con le parti che non sono di sistema o di prova mascherate (solo la lunghezza)
            $safe = 'AppData', 'Local', 'LocalLow', 'Roaming', 'Temp', 'xpx-test', 'pins', 'A', 'B', 'C', 'xpx-pin-1', 'xpx-pin-2', 'xpx-pin-3', 'xpx-pin-4'
            '<profilo>\' + ((@($Path.Substring($env:USERPROFILE.Length).Trim('\') -split '\\' | ForEach-Object { if ($safe -contains $_) { $_ } elseif ($_ -match '~\d') { "(8.3:$_)" } else { "*($($_.Length))" } })) -join '\')
        }
        elseif ($Path.Length -le 3) { '<radice di un''unità>' }
        else { '<altra>' }
    }
    $count = 0
    $h = [XpxKit]::FindWindowEx([IntPtr]::Zero, [IntPtr]::Zero, 'CabinetWClass', [IntPtr]::Zero)
    while ($h -ne [IntPtr]::Zero) { $p = 0; [void][NavKit]::GetWindowThreadProcessId($h, [ref]$p); if ($script:newPids -contains [int]$p) { $count++ }; $h = [XpxKit]::FindWindowEx([IntPtr]::Zero, $h, 'CabinetWClass', [IntPtr]::Zero) }
    "  finestra nuova {0:x} ($count finestre nel processo nuovo): $((Get-TabPaths $w | ForEach-Object { & $kind $_ }) -join ' | ')" -f $w
    Add-VtTestWindow $w
    $oldPid = $sepPid
    [void][NavKit]::GetWindowThreadProcessId([IntPtr]$w, [ref]$sepPid)
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "la finestra di prova non è in un processo separato ($sepPid): mi fermo" }
    Move-TestWindow $w 60 50 1300 760
    Start-Sleep -Milliseconds 800
    & $XpxExe --inject-pid $sepPid
    $okOrder = Wait-Until { (Get-WindowsOrder) -eq $dfsSaved } 15000
    Start-Sleep -Milliseconds 1500
    $native = Get-NativePins $w
    $c = Get-TabContent $w
    Check 'barra spenta, processo nuovo, ordine cambiato da fuori: la mod rimette in Windows l''ordine dei gruppi' $okOrder "Windows: $(Get-WindowsOrder); voluto: $dfsSaved"
    Check '  e il riquadro di sistema, visibile, lo mostra' ($c.Region -lt 0 -and $native -eq $dfsSaved) "riquadro: $native"
    $before = LogCount
    Set-XpxOption 'NavBar' 'Enabled' '1'
    Wait-Until { (ModLines $before) -match "\[\s*$sepPid\:.*NavBar: \d+ voci" } 20000 | Out-Null
    Start-Sleep -Milliseconds 1500
    $again = Format-Dump (Get-GroupDump $w)
    Check "barra riaccesa nel processo nuovo (prima $oldPid, ora $sepPid): stessi gruppi, stesso posto" ($again -eq $saved) "prima: $saved / ora: $again"
    } # fine di "i gruppi restano" (saltato con -MouseOnly)

    $errors = @(Get-ModErrors $logStart) + @(if ($null -ne $logStart2) { Get-ModErrors $logStart2 })
    Check 'nessun errore della barra nel registro' ($errors.Count -eq 0) (($errors | Select-Object -First 5 | ForEach-Object { $_ -replace '^.*?(NavBar|NavGroups|ModernMenu)', '$1' }) -join ' / ')
    "--- registro (gruppi e barra):"
    ModLines $logStart2 | Where-Object { $_ -match "\[\s*$sepPid\:" -and $_ -match 'NavGroups|NavBar: \d+ voci' } | Select-Object -Last 4 | ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
}
finally {
    if ($script:pinPaths.Count -gt 0) {
        $removed = @($script:pinPaths | ForEach-Object { Remove-Pin $_ })
        $same = $pinsSaved -and [XpxPin.PinKit]::SameAsSaved()
        Check 'cartelle di prova tolte; le fissate dell''utente identiche a prima (elementi e ordine)' (@($removed | Where-Object { -not $_ }).Count -eq 0 -and $same) "tolte $(@($removed | Where-Object { $_ }).Count) su 4, identiche: $same"
    }
    if (-not $Keep) {
        Close-TestWindows
        Start-Sleep -Milliseconds 800
        if ($sepPid -and $sepPid -ne $shellPid) { Stop-Process -Id $sepPid -Force -ErrorAction SilentlyContinue }
        foreach ($p in $script:startedPids) { if ($p -and $p -ne $shellPid) { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } }
        Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $groupsFile -ErrorAction SilentlyContinue
    }
    $userSame = if ($userGroupsBefore) { (Test-Path $userGroupsFile) -and ([Convert]::ToBase64String([IO.File]::ReadAllBytes($userGroupsFile)) -eq [Convert]::ToBase64String($userGroupsBefore)) } else { -not (Test-Path $userGroupsFile) }
    Check 'file dei gruppi dell''utente intatto' $userSame
    Remove-Item -LiteralPath $XpxDevFilter, (Join-Path $XpxData 'mod-command.txt'), (Join-Path $XpxData 'dump-tree.txt') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Exit-VtIsolation
    Stop-Tray; Start-Tray
    $shellOk = [bool](Get-Process -Id $shellPid -ErrorAction SilentlyContinue)
    "--- processo della barra intatto: $shellOk; errori: $script:fails$(if ($script:mouseSkipped) { '; ATTENZIONE: prove col mouse in parte saltate (finestra di prova non in primo piano)' })"
    "processi explorer: " + ((Get-Process explorer | ForEach-Object { "$($_.Id)" }) -join ', ') + " (barra: $shellPid); finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows -and (Get-UserTabCount) -eq $userTabs)
}
