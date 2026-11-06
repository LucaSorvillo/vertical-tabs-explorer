<#
.SYNOPSIS
    Test dal vivo di VTabsExplorer con finestre di prova di Esplora file.

.DESCRIPTION
    Mette la tray in modalità prova (dev-filter.txt su %TEMP%\xpx-test: conta solo le finestre di prova
    e usa session-dev.txt), poi con finestre e schede di prova verifica: sessione salvata mentre si
    lavora (schede in ordine, scheda attiva, posizione), chiusura di più finestre insieme o una alla
    volta, riapertura dalla cartella iniziale e su una cartella precisa, cartelle virtuali e cartelle
    sparite, opzione spenta, "Riapri ora", riavvio della tray, finestra ingrandita, molte schede,
    pulsante "Torna su" nella vista dei file (solo con una finestra in primo piano) e "riapri la scheda
    chiusa" (Ctrl+Maiusc+T, senza premere tasti davvero: vedi la sezione 13).
    A ogni ripristino un osservatore controlla ogni 5 ms le finestre nuove: devono restare velate
    (invisibili) finché si costruiscono e comparire già con tutte le schede. In modalità prova il velo
    si tende solo mentre esiste dev-arm.txt, che la prova crea prima di aprire la finestra.
    Alla fine chiude le finestre di prova, toglie la modalità prova e rimette le impostazioni com'erano.
    Le finestre dell'utente non vengono toccate (si controlla solo che il loro numero non cambi).
    Nessun input reale: le finestre di prova però si aprono in primo piano.

.EXAMPLE
    .\tools\test\Run-Tests.ps1
#>
[CmdletBinding()]
param([switch] $KeepWindows)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\TestKit.ps1"

$results = [System.Collections.Generic.List[object]]::new()
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
    $results.Add([pscustomobject]@{ Test = $Name; Ok = $Ok; Dettagli = $Detail })
    $mark = if ($Ok) { '[OK]      ' } else { '[FALLITO] ' }
    Write-Host ($mark + $Name + $(if ($Detail) { "  ($Detail)" } else { '' })) -ForegroundColor $(if ($Ok) { 'Green' } else { 'Red' })
}
function Get-LogCount([string]$Pattern) {
    $path = Join-Path $XpxData 'logs\vtabsexplorer.log'
    if (-not (Test-Path $path)) { return 0 }
    @(Get-Content $path -Encoding UTF8 | Where-Object { $_ -match $Pattern }).Count
}
function Rect([int64]$Hwnd) { $p = Get-Placement $Hwnd; '{0},{1},{2},{3}' -f $p.rcNormal.Left, $p.rcNormal.Top, $p.rcNormal.Right, $p.rcNormal.Bottom }
# Esegue un'azione che apre finestre di Esplora file osservando come compaiono (con il segnale che in
# modalità prova permette di velarle) e aspetta la condizione finale
function Watch-Appearance([scriptblock]$Action, [scriptblock]$Until, [int]$TimeoutMs = 30000) {
    [IO.File]::WriteAllText($XpxDevArm, 'prova')
    $watch = New-Object XpxAppearance
    $watch.Start()
    try {
        $value = & $Action
        $ok = Wait-Until $Until $TimeoutMs
        Start-Sleep -Milliseconds 800
    }
    finally {
        $appearance = $watch.Stop()
        Remove-Item -LiteralPath $XpxDevArm -ErrorAction SilentlyContinue
    }
    [pscustomobject]@{ Value = $value; Done = $ok; Appearance = $appearance }
}
# Apre Esplora file su una cartella e aspetta che VTabsExplorer abbia finito di ripristinare. La finestra
# aperta è la prima a comparire (la sua scheda può essere già stata portata altrove dal ripristino, e
# Esplora file può mostrare una finestra che teneva pronta, nascosta)
function Open-AndWaitRestore([string]$Path) {
    $before = Get-LogCount 'Ripristino completato'
    $r = Watch-Appearance { if (-not [XpxKit]::OpenNewWindow($Path)) { throw "Finestra non aperta su $Path" } } { (Get-LogCount 'Ripristino completato') -gt $before }
    $first = @($r.Appearance | Where-Object { [XpxKit]::IsWindow([IntPtr]$_.Hwnd) }) | Select-Object -First 1
    [pscustomobject]@{ Hwnd = $(if ($first) { $first.Hwnd } else { 0 }); Restored = $r.Done; Appearance = $r.Appearance }
}
# Finestre nuove comparse già complete: velate finché mancavano schede (visibili incomplete al massimo
# per un istante: una finestra che Esplora file teneva pronta si vela solo quando compare)
function Test-AppearedComplete($Appearance) {
    $windows = @($Appearance | Where-Object { $_.FinalTabs -gt 0 })
    $windows.Count -gt 0 -and @($windows | Where-Object { -not $_.Veiled -or $_.VisibleIncompleteMs -gt 100 -or $_.FirstVisibleTabs -lt 1 }).Count -eq 0
}
function Format-Appearance($Appearance) {
    (@($Appearance) | Where-Object { $_.FinalTabs -gt 0 } | ForEach-Object {
            '{0} schede, {1}, visibile incompleta {2} ms' -f $_.FinalTabs,
            $(if ($_.Veiled) { 'velata per {0:N1} s' -f (($_.VeiledUntilMs - $_.FirstSeenMs) / 1000) } else { 'mai velata' }), $_.VisibleIncompleteMs
        }) -join '; '
}

$T = $XpxTestRoot
$A, $B, $C, $D, $E, $F, $G = 'A', 'B', 'C', 'D', 'E', 'F', 'G' | ForEach-Object { Join-Path $T $_ }
# Finestre abbastanza larghe perché la barra delle schede non scorra (lo scorrimento ha un caso a sé)
$screenW = Get-ScreenWidth
$Pos1 = @{ X = 60; Y = 60; W = [math]::Min(1300, $screenW - 120); H = 650 }
$Pos2 = @{ X = 90; Y = 220; W = [math]::Min(1250, $screenW - 150); H = 600 }
$R1 = '{0},{1},{2},{3}' -f $Pos1.X, $Pos1.Y, ($Pos1.X + $Pos1.W), ($Pos1.Y + $Pos1.H)
$R2 = '{0},{1},{2},{3}' -f $Pos2.X, $Pos2.Y, ($Pos2.X + $Pos2.W), ($Pos2.Y + $Pos2.H)
$settingsFile = Join-Path $XpxData 'settings.ini'
$settingsBefore = if (Test-Path $settingsFile) { [IO.File]::ReadAllBytes($settingsFile) } else { $null }
$userWindows = Get-UserWindowCount
$userTabs = Get-UserTabCount
# Esplora file apre le finestre nuove come l'ultima chiusa: alla fine l'ultima finestra di prova si chiude
# con posizione e stato di una finestra dell'utente (o, se non ce ne sono, normale e di taglia comune).
$userPlacement = Get-UserPlacement
if (-not $userPlacement) { $userPlacement = New-Placement 1 200 150 1200 800 }
$devFilterOurs = $false

try {
    if (-not (Test-Path $XpxExe)) { throw 'Manca build\bin\VTabsExplorer.exe: compila con build.ps1.' }
    if (Test-Path $XpxDevFilter) {
        if ((Get-Content $XpxDevFilter -TotalCount 1) -ne $T) { throw 'C''è già un dev-filter.txt diverso: rimuovilo prima del test.' }
    }
    else { $devFilterOurs = $true }
    Close-TestWindows # finestre di prova rimaste da un giro precedente
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    $A, $B, $C, $D, $E, $F, $G | ForEach-Object { New-Item -ItemType Directory -Force $_ | Out-Null }
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Stop-Tray
    # (closed-dev.txt: pila di Ctrl+Maiusc+T della modalità prova, su disco: si parte senza schede chiuse di un giro precedente)
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm, (Join-Path $XpxData 'closed-dev.txt') -ErrorAction SilentlyContinue
    Set-XpxOption 'Debug' 'VerboseLog' '1' # tempi nel log durante la prova (alla fine si rimettono le impostazioni)
    # Le finestre di prova si aprono dal processo della prova, come una cartella aperta da un altro programma: qui devono
    # restare finestre (le cartelle in una scheda hanno la loro prova, Test-OpenInTabs.ps1).
    Set-XpxOption 'Tabs' 'OpenInTabs' '0'
    Start-Tray
    Check 'Tray in modalità prova' (Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000)

    # --- 1. Sessione salvata mentre si lavora ---------------------------------------------------
    $w1 = Open-TestWindow $A
    Add-TestTab $w1 $B
    Add-TestTab $w1 $C
    Select-TabByName $w1 'B'
    Move-TestWindow $w1 $Pos1.X $Pos1.Y $Pos1.W $Pos1.H
    $w2 = Open-TestWindow $D
    Add-TestTab $w2 $E
    Move-TestWindow $w2 $Pos2.X $Pos2.Y $Pos2.W $Pos2.H
    Check 'Sessione salvata: finestre, schede in ordine, scheda attiva' (Wait-Until { (Format-DevSession) -eq '[D,*E] [A,*B,C]' } 6000) (Format-DevSession)
    $placed = Wait-Until { $s = Read-DevSession; @($s).Count -eq 2 -and ($s[1].Rect -join ',') -eq $R1 -and ($s[0].Rect -join ',') -eq $R2 } 6000
    $s = Read-DevSession
    Check 'Sessione salvata: posizione delle finestre' $placed ("{0} / {1}" -f ($s[1].Rect -join ','), ($s[0].Rect -join ','))

    # --- 1b. File della sessione occupato da un altro programma (antivirus, indicizzazione) ------
    # Aperto senza condivisione per l'eliminazione non si può sostituire: la tray deve riprovare finché
    # non riesce, invece di lasciare su disco la sessione vecchia.
    $failedSaves = Get-LogCount 'Sessione non salvata'
    $retriedSaves = Get-LogCount 'Sessione salvata al nuovo tentativo'
    $holder = [IO.File]::Open($XpxDevSession, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        Select-TabByName $w1 'C'
        Check 'Sessione occupata: il salvataggio non riesce e la tray lo segnala' (Wait-Until { (Get-LogCount 'Sessione non salvata') -gt $failedSaves } 6000) (Format-DevSession)
        Start-Sleep -Milliseconds 1200
    }
    finally { $holder.Dispose() }
    Check '...appena il file si libera la sessione è salvata' ((Wait-Until { (Format-DevSession) -match '\[A,B,\*C\]' } 4000) -and (Get-LogCount 'Sessione salvata al nuovo tentativo') -gt $retriedSaves) (Format-DevSession)
    Select-TabByName $w1 'B'
    Wait-Until { (Format-DevSession) -match '\[A,\*B,C\]' } 6000 | Out-Null

    # --- 2. Chiuse insieme --------------------------------------------------------------------
    Close-Window $w2
    Start-Sleep -Milliseconds 500
    Close-Window $w1
    Check 'Chiuse insieme: ricordate tutte e due (prima l''ultima chiusa)' (Wait-Until { @(Get-TestWindows).Count -eq 0 -and (Format-DevSession) -eq '[A,*B,C] [D,*E]' } 8000) (Format-DevSession)

    # --- 3. Riapertura dalla cartella iniziale --------------------------------------------------
    $r = Open-AndWaitRestore $T
    Check 'Riaprendo Esplora file la sessione torna' $r.Restored
    $opened = $r.Hwnd
    $others = @(Get-TestWindows | Where-Object { $_ -ne $opened })
    Check 'La finestra aperta diventa la prima della sessione' ((Format-Window $opened) -eq '[A,*B,C]') (Format-Window $opened)
    Check '...nella sua posizione' ((Rect $opened) -eq $R1) (Rect $opened)
    Check 'Riaperta anche l''altra finestra, nella sua posizione' ($others.Count -eq 1 -and (Format-Window $others[0]) -eq '[D,*E]' -and (Rect $others[0]) -eq $R2) $(if ($others.Count) { "$(Format-Window $others[0]) $(Rect $others[0])" } else { 'nessuna' })
    $foreground = [int64][XpxKit]::GetForegroundWindow()
    if ($foreground -eq 0) { Write-Warning 'Nessuna finestra in primo piano (es. Desktop remoto ridotto a icona): controllo del primo piano saltato.' }
    else { Check 'La finestra aperta resta in primo piano' ($foreground -eq $opened) }
    Check 'Le finestre compaiono già complete (velate mentre si costruiscono)' ((Test-AppearedComplete $r.Appearance) -and @($r.Appearance | Where-Object { $_.FinalTabs -gt 0 }).Count -eq 2) (Format-Appearance $r.Appearance)

    # --- 3b. Con altre finestre aperte non scatta niente: la finestra nuova non viene velata -------
    $r = Watch-Appearance { Open-TestWindow $G } { $false } 2500
    Check 'Con altre finestre aperte una finestra nuova non viene velata' (@($r.Appearance | Where-Object { $_.Veiled }).Count -eq 0 -and @(Get-TabNames $r.Value).Count -eq 1) (Format-Appearance $r.Appearance)
    Close-Window $r.Value

    # --- 4. Chiuse una alla volta ---------------------------------------------------------------
    Close-Window $others[0]
    Wait-Until { -not [XpxKit]::IsWindow([IntPtr]$others[0]) } 5000 | Out-Null
    Start-Sleep -Seconds 7
    Close-Window $opened
    Check 'Chiuse una alla volta: ricordata solo l''ultima' (Wait-Until { @(Get-TestWindows).Count -eq 0 -and (Format-DevSession) -eq '[A,*B,C]' } 8000) (Format-DevSession)

    # --- 5. Apertura su una cartella precisa ----------------------------------------------------
    $r = Open-AndWaitRestore $F
    Check 'Aprendo una cartella precisa la sessione torna' $r.Restored
    $opened = $r.Hwnd
    Check 'La cartella aperta resta la prima scheda, attiva; la sessione accanto' ((Format-Window $opened) -eq '[*F,A,B,C]') (Format-Window $opened)
    Check 'Nessuna finestra in più' (@(Get-TestWindows).Count -eq 1)
    Check '...e la finestra compare già completa' (Test-AppearedComplete $r.Appearance) (Format-Appearance $r.Appearance)

    # --- 6. Cartelle virtuali e cartelle sparite ------------------------------------------------
    Add-StartTab $opened      # resta su "Questo PC"
    Add-TestTab $opened $G
    Wait-Until { (Format-DevSession) -match 'G\]$' } 6000 | Out-Null
    Close-Window $opened
    Wait-Until { @(Get-TestWindows).Count -eq 0 } 6000 | Out-Null
    Start-Sleep -Seconds 1
    Remove-Item -LiteralPath $G -Force
    $r = Open-AndWaitRestore $T
    $names = @(Get-TabNamesWide $r.Hwnd)
    Check 'Cartella virtuale ripristinata, cartella sparita saltata' ($r.Restored -and ($names -join ',') -eq 'F,A,B,C,Questo PC') ($names -join ',')
    Close-TestWindows

    # --- 7. Opzione spenta ----------------------------------------------------------------------
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Start-Sleep -Milliseconds 2500
    $before = Get-LogCount 'Ripristino completato'
    $r = Watch-Appearance { Open-TestWindow $T } { $false } 5000
    $opened = $r.Value
    Check 'Con l''opzione spenta non si riapre nulla' ((Get-LogCount 'Ripristino completato') -eq $before -and @(Get-TestWindows).Count -eq 1 -and @(Get-TabNames $opened).Count -eq 1)
    Check '...e la finestra non viene velata' (@($r.Appearance | Where-Object { $_.Veiled }).Count -eq 0) (Format-Appearance $r.Appearance)
    Close-TestWindows

    # --- 8. "Riapri ora" dalla tray (vale anche con l'opzione spenta) ----------------------------
    $w = Open-TestWindow $A
    Add-TestTab $w $B
    Start-Sleep -Seconds 1
    Close-Window $w
    Wait-Until { @(Get-TestWindows).Count -eq 0 -and (Format-DevSession) -eq '[A,*B]' } 8000 | Out-Null
    $before = Get-LogCount 'Riapri ora completato'
    $r = Watch-Appearance { & $XpxExe --restore-now } { (Get-LogCount 'Riapri ora completato') -gt $before }
    Check '"Riapri ora" riapre l''ultima sessione' ($r.Done -and @(Get-TestWindows).Count -eq 1 -and (Format-Window @(Get-TestWindows)[0]) -eq '[A,*B]') $(if (@(Get-TestWindows).Count) { Format-Window @(Get-TestWindows)[0] })
    Check '...già completa' (Test-AppearedComplete $r.Appearance) (Format-Appearance $r.Appearance)

    # --- 8b. Esplora file perde il comando "nuova scheda" (come appena riavviato) -----------------
    # dev-drop-newtab.txt ("<lasciati passare> <persi>", vale solo in modalità prova) fa perdere alla
    # tray i prossimi comandi "nuova scheda": con un comando perso la scheda si chiede di nuovo e la
    # finestra torna completa; con la stessa scheda persa due volte la finestra torna senza, e la scheda
    # mancante resta da riaprire con Ctrl+Maiusc+T (non va persa).
    # Si parte dalla finestra del caso 8 (chiuderla e aprirne un'altra a pochi secondi di distanza le
    # farebbe ricordare come "chiuse insieme": due finestre nella sessione).
    $dropFile = Join-Path $XpxData 'dev-drop-newtab.txt'
    Set-XpxOption 'ReopenTab' 'Enabled' '1'
    $w = @(Get-TestWindows) | Select-Object -First 1
    if (-not $w) { $w = Open-TestWindow $A; Add-TestTab $w $B }
    Move-TestWindow $w $Pos1.X $Pos1.Y $Pos1.W $Pos1.H
    Add-TestTab $w $C
    Select-TabByName $w 'B'
    Wait-Until { (Format-DevSession) -eq '[A,*B,C]' } 8000 | Out-Null
    Close-Window $w
    Wait-Until { @(Get-TestWindows).Count -eq 0 -and (Format-DevSession) -eq '[A,*B,C]' } 8000 | Out-Null
    [IO.File]::WriteAllText($dropFile, '0 1')
    $retried = Get-LogCount 'richiesta ripetuta'
    $before = Get-LogCount 'Riapri ora completato'
    $r = Watch-Appearance { & $XpxExe --restore-now } { (Get-LogCount 'Riapri ora completato') -gt $before } 40000
    $got = if (@(Get-TestWindows).Count) { Format-Window @(Get-TestWindows)[0] }
    Check 'Comando "nuova scheda" perso: chiesto di nuovo, la finestra torna completa' ($r.Done -and (Get-LogCount 'richiesta ripetuta') -gt $retried -and $got -eq '[A,*B,C]') $got
    Check '...già completa' (Test-AppearedComplete $r.Appearance) (Format-Appearance $r.Appearance)

    Close-TestWindows
    Wait-Until { @(Get-TestWindows).Count -eq 0 -and (Format-DevSession) -eq '[A,*B,C]' } 8000 | Out-Null
    [IO.File]::WriteAllText($dropFile, '1 2') # B passa, C si perde due volte
    $kept = Get-LogCount '1 scheda non riaperta'
    $before = Get-LogCount 'Riapri ora completato'
    $r = Watch-Appearance { & $XpxExe --restore-now } { (Get-LogCount 'Riapri ora completato') -gt $before } 40000
    $w = @(Get-TestWindows) | Select-Object -First 1
    $got = if ($w) { Format-Window $w }
    Check 'Scheda persa anche al secondo tentativo: la finestra torna senza' ($r.Done -and $got -eq '[A,*B]') $got
    Check '...la scheda mancante resta da riaprire con Ctrl+Maiusc+T' ((Get-LogCount '1 scheda non riaperta') -gt $kept)
    Check '...e la sessione è quella aperta' (Wait-Until { (Format-DevSession) -eq '[A,*B]' } 6000) (Format-DevSession)
    if ($w) {
        Invoke-ReopenTab
        Check 'Ctrl+Maiusc+T riapre la scheda mancante' (Wait-Until { (Format-Window $w) -eq '[A,B,*C]' } 15000) (Format-Window $w)
        Check '...e torna nella sessione' (Wait-Until { (Format-DevSession) -eq '[A,B,*C]' } 6000) (Format-DevSession)
    }
    Remove-Item -LiteralPath $dropFile -ErrorAction SilentlyContinue
    # Come alla fine del caso 8: una finestra [A,*B] (la stessa, per lo stesso motivo di prima)
    if ($w -and (Get-TabNames $w) -contains 'C') { Close-TestTab $w 'C' }
    if ($w) { Select-TabByName $w 'B' }
    Wait-Until { (Format-DevSession) -eq '[A,*B]' } 8000 | Out-Null

    # --- 9. La sessione sopravvive al riavvio della tray ----------------------------------------
    Close-TestWindows
    Wait-Until { (Format-DevSession) -eq '[A,*B]' } 6000 | Out-Null
    Set-XpxOption 'Session' 'RestoreOnOpen' '1'
    Stop-Tray
    Start-Tray
    $r = Open-AndWaitRestore $T
    Check 'Dopo il riavvio della tray la sessione torna' ($r.Restored -and (Format-Window $r.Hwnd) -eq '[A,*B]') (Format-Window $r.Hwnd)

    # --- 10. Finestra ingrandita ----------------------------------------------------------------
    Set-WindowShow $r.Hwnd 3   # SW_SHOWMAXIMIZED
    Wait-Until { (Read-DevSession)[0].ShowCmd -eq 3 } 6000 | Out-Null
    Close-Window $r.Hwnd
    Wait-Until { @(Get-TestWindows).Count -eq 0 } 6000 | Out-Null
    Start-Sleep -Seconds 1
    $r = Open-AndWaitRestore $T
    Check 'Una finestra ingrandita torna ingrandita' ($r.Restored -and (Get-Placement $r.Hwnd).showCmd -eq 3) ("showCmd {0}" -f (Get-Placement $r.Hwnd).showCmd)
    Close-TestWindows

    # --- 11. Molte schede: la barra scorre e non le espone tutte ---------------------------------
    $K = 1..11 | ForEach-Object { Join-Path $T ('K{0:D2}' -f $_) }
    $K | ForEach-Object { New-Item -ItemType Directory -Force $_ | Out-Null }
    $names11 = 1..11 | ForEach-Object { 'K{0:D2}' -f $_ }
    # a) salvataggio: dieci schede viste tutte, poi finestra stretta e una scheda in più (la barra si
    #    rilegge con alcune schede fuori vista: l'ordine salvato deve restare quello giusto). Ripristino
    #    spento durante la preparazione: aprendo la finestra tornerebbe la sessione del caso precedente.
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Start-Sleep -Milliseconds 600
    $w = Open-TestWindow $K[0]
    Move-TestWindow $w 60 60 ([math]::Min(1800, $screenW - 80)) 600
    foreach ($folder in $K[1..9]) { Add-TestTab $w $folder }
    Start-Sleep -Seconds 2
    Move-TestWindow $w 60 60 650 600
    Start-Sleep -Milliseconds 800
    Add-TestTab $w $K[10]
    $saved = '[' + (($names11 | Select-Object -First 10) -join ',') + ',*K11]'
    Check 'Molte schede: ordine salvato giusto anche con la barra che scorre' (Wait-Until { (Format-DevSession) -eq $saved } 6000) (Format-DevSession)
    Close-Window $w
    Wait-Until { @(Get-TestWindows).Count -eq 0 } 6000 | Out-Null
    # b) ripristino: sessione con la prima scheda attiva in una finestra stretta (dopo l'apertura delle
    #    schede la barra mostra le ultime: la prima è fuori vista)
    Set-XpxOption 'Session' 'RestoreOnOpen' '1'
    Stop-Tray
    $lines = @('VTabsExplorer-session 1', 'window', 'placement 0 1 -1 -1 -1 -1 60 60 710 660', 'active 0') + @($K | ForEach-Object { "tab $_" })
    [IO.File]::WriteAllText($XpxDevSession, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
    Start-Tray
    $r = Open-AndWaitRestore $T
    Check 'Molte schede: la scheda attiva fuori vista torna attiva' ($r.Restored -and (Get-ActiveTabName $r.Hwnd) -eq 'K01') (Get-ActiveTabName $r.Hwnd)
    Check 'Molte schede: la finestra compare già completa' (Test-AppearedComplete $r.Appearance) (Format-Appearance $r.Appearance)
    $names = @(Get-TabNamesWide $r.Hwnd)
    Check 'Molte schede: riaperte nell''ordine giusto' (($names -join ',') -eq ($names11 -join ',')) ($names -join ',')

    # --- 12. Pulsante "Torna su" (vista dei file scorsa verso il basso) ------------------------------
    # Vale solo per la finestra in primo piano: in una sessione senza primo piano (es. Desktop remoto
    # con il client ridotto a icona, Windows non attiva finestre) i controlli si saltano.
    Close-TestWindows
    $many = Join-Path $T 'Molti'
    New-Item -ItemType Directory -Force $many | Out-Null
    1..300 | ForEach-Object { [IO.File]::WriteAllText((Join-Path $many ('file-{0:D3}.txt' -f $_)), '') }
    Set-XpxOption 'Session' 'RestoreOnOpen' '0' # aprendo la finestra non deve tornare la sessione del caso precedente
    Start-Sleep -Milliseconds 600
    $w = Open-TestWindow $many
    Wait-Until { (Format-DevSession) -match 'Molti' } 6000 | Out-Null # finestra considerata dalla tray
    if ([XpxKit]::GetForegroundWindow() -ne [IntPtr]$w) { try { $XAE::FromHandle([IntPtr]$w).SetFocus() } catch {}; Start-Sleep -Milliseconds 500 }
    if ([XpxKit]::GetForegroundWindow() -ne [IntPtr]$w) {
        Write-Warning 'Nessuna finestra in primo piano (es. Desktop remoto ridotto a icona): controlli del pulsante "Torna su" saltati.'
    }
    else {
        $sp = Get-ViewScroll $w
        $sp.SetScrollPercent(-1, 0)
        Check 'Torna su: in cima alla vista il pulsante non c''è' ((Wait-Until { $sp.Current.VerticalScrollPercent -eq 0 }) -and -not (Test-BackToTopShown))
        $sp.SetScrollPercent(-1, 40)
        Check 'Torna su: compare quando la vista scorre' (Wait-Until { Test-BackToTopShown } 3000)
        $vr = New-Object XpxKit+RECT; [XpxKit]::GetWindowRect((Get-FileView $w), [ref]$vr) | Out-Null
        $br = New-Object XpxKit+RECT; [XpxKit]::GetWindowRect((Get-BackToTopWindow), [ref]$br) | Out-Null
        Check 'Torna su: nell''angolo in basso a destra della vista' ($br.Right -le $vr.Right -and $br.Right -ge $vr.Right - 100 -and $br.Bottom -le $vr.Bottom -and $br.Bottom -ge $vr.Bottom - 100) ("pulsante {0},{1}-{2},{3}, vista fino a {4},{5}" -f $br.Left, $br.Top, $br.Right, $br.Bottom, $vr.Right, $vr.Bottom)
        Invoke-BackToTop
        Check 'Torna su: il clic riporta la vista all''inizio' (Wait-Until { $sp.Current.VerticalScrollPercent -eq 0 } 3000) ("{0:N1}%" -f $sp.Current.VerticalScrollPercent)
        Check 'Torna su: in cima il pulsante sparisce' (Wait-Until { -not (Test-BackToTopShown) } 3000)
        Check 'Torna su: Esplora file resta in primo piano (il pulsante non prende il focus)' ([XpxKit]::GetForegroundWindow() -eq [IntPtr]$w)
        $sp.SetScrollPercent(-1, 40)
        Wait-Until { Test-BackToTopShown } 3000 | Out-Null
        [XpxKit]::ShowWindow([IntPtr]$w, 6) | Out-Null # SW_MINIMIZE
        Check 'Torna su: con la finestra ridotta a icona sparisce' (Wait-Until { -not (Test-BackToTopShown) } 3000)
        [XpxKit]::ShowWindow([IntPtr]$w, 9) | Out-Null # SW_RESTORE
        # Da un altro programma Windows può negare il primo piano: se serve lo si chiede come Alt+Tab
        if (-not (Wait-Until { [XpxKit]::GetForegroundWindow() -eq [IntPtr]$w } 1500)) { [XpxKit]::SwitchToThisWindow([IntPtr]$w, $true) }
        if (Wait-Until { [XpxKit]::GetForegroundWindow() -eq [IntPtr]$w } 2000) {
            Check 'Torna su: di nuovo in primo piano ricompare' (Wait-Until { Test-BackToTopShown } 4000)
            Set-XpxOption 'BackToTop' 'Enabled' '0'
            Check 'Torna su spento dal menu: il pulsante sparisce' (Wait-Until { -not (Test-BackToTopShown) } 4000)
            Set-XpxOption 'BackToTop' 'Enabled' '1'
            Check 'Torna su riacceso: il pulsante ricompare' (Wait-Until { Test-BackToTopShown } 4000)
            Invoke-BackToTop
            Wait-Until { -not (Test-BackToTopShown) } 3000 | Out-Null
        }
        else { Write-Warning 'Windows non ha rimesso in primo piano la finestra di prova: ultimi controlli del pulsante saltati.' }
    }

    # --- 13. Riapri la scheda chiusa (Ctrl+Maiusc+T) ---------------------------------------------
    # La combinazione si verifica senza premere tasti: mentre la tiene la tray, il test non riesce a
    # registrarla (Test-HotkeyHeld); l'azione si chiede con --reopen-tab, lo stesso comando della
    # scorciatoia. La pila delle schede chiuse sta su disco (closed-dev.txt in modalità prova, come le
    # schede chiuse di Chrome resta riavviando la tray o il PC): per cominciare vuota la si cancella con
    # la tray ferma. La preparazione avviene con il ripristino all'apertura spento, altrimenti aprire la
    # finestra di prova farebbe tornare la sessione del caso precedente.
    Close-TestWindows
    Set-XpxOption 'ReopenTab' 'Enabled' '1'
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Stop-Tray
    Remove-Item -LiteralPath (Join-Path $XpxData 'closed-dev.txt') -ErrorAction SilentlyContinue
    Start-Tray
    Start-Sleep -Milliseconds 800
    $w = Open-TestWindow $A
    # Finestra larga: con la barra delle schede che scorre i nomi letti per coordinata possono risultare
    # "ruotati" (vedi le note tecniche del README)
    Move-TestWindow $w $Pos1.X $Pos1.Y $Pos1.W $Pos1.H
    Add-TestTab $w $B
    Add-TestTab $w $C
    Wait-Until { (Format-DevSession) -eq '[A,B,*C]' } 8000 | Out-Null
    if ([XpxKit]::GetForegroundWindow() -ne [IntPtr]$w) { [XpxKit]::SwitchToThisWindow([IntPtr]$w, $true); Start-Sleep -Milliseconds 600 }
    if ([XpxKit]::GetForegroundWindow() -ne [IntPtr]$w) {
        Write-Warning 'Nessuna finestra in primo piano: controlli sulla combinazione saltati (l''azione si prova comunque).'
    }
    else {
        Check 'Ctrl+Maiusc+T riservata solo con una finestra di Esplora file in primo piano' (Wait-Until { Test-HotkeyHeld } 3000)
        [XpxKit]::ShowWindow([IntPtr]$w, 6) | Out-Null # SW_MINIMIZE
        Check 'Ridotta a icona: la combinazione torna agli altri programmi' (Wait-Until { -not (Test-HotkeyHeld) } 5000)
        [XpxKit]::ShowWindow([IntPtr]$w, 9) | Out-Null # SW_RESTORE
        if (-not (Wait-Until { [XpxKit]::GetForegroundWindow() -eq [IntPtr]$w } 2000)) { [XpxKit]::SwitchToThisWindow([IntPtr]$w, $true) }
        Check 'Di nuovo in primo piano: la combinazione torna a noi' (Wait-Until { Test-HotkeyHeld } 5000)
    }
    Close-TestTab $w 'C'
    Check 'Scheda chiusa: la sessione la dimentica' (Wait-Until { (Format-DevSession) -eq '[A,*B]' } 6000) (Format-DevSession)
    Invoke-ReopenTab
    Check 'La scheda chiusa torna (in fondo alla barra, attiva)' (Wait-Until { (Format-Window $w) -eq '[A,B,*C]' } 12000) (Format-Window $w)
    Close-TestTab $w 'B'
    Close-TestTab $w 'C'
    Invoke-ReopenTab
    Check 'Due schede chiuse: torna prima l''ultima' (Wait-Until { (Format-Window $w) -eq '[A,*C]' } 12000) (Format-Window $w)
    Invoke-ReopenTab
    Check '...e premendo di nuovo torna la precedente' (Wait-Until { (Format-Window $w) -eq '[A,C,*B]' } 12000) (Format-Window $w)
    # Finestra chiusa: torna intera, con la sua scheda attiva, e già completa. Qui il ripristino
    # all'apertura torna acceso: riaprire una finestra chiusa non deve farlo scattare.
    # Si chiude anche una scheda e non si riapre: così sotto la finestra, nella pila, resta una scheda
    # la cui finestra non esisterà più.
    Close-TestTab $w 'C'
    Wait-Until { (Format-DevSession) -notmatch 'C' } 8000 | Out-Null
    $saved = Format-DevSession
    Set-XpxOption 'Session' 'RestoreOnOpen' '1'
    Start-Sleep -Milliseconds 800
    $before = Get-LogCount 'Ripristino completato'
    Close-Window $w
    Wait-Until { @(Get-TestWindows).Count -eq 0 } 8000 | Out-Null
    Start-Sleep -Milliseconds 900
    $r = Watch-Appearance { Invoke-ReopenTab } { @(Get-TestWindows).Count -eq 1 } 20000
    $w = @(Get-TestWindows) | Select-Object -First 1
    $back = Wait-Until { (Format-DevSession) -eq $saved } 10000
    Check 'La finestra chiusa torna intera, nella sua posizione' ($r.Done -and $w -and $back -and (Rect $w) -eq $R1) ("{0} (attesa {1}) {2}" -f (Format-DevSession), $saved, $(if ($w) { Rect $w } else { 'nessuna' }))
    Check '...già completa (velata mentre si costruisce)' (Test-AppearedComplete $r.Appearance) (Format-Appearance $r.Appearance)
    Check '...senza far scattare il ripristino della sessione' ((Get-LogCount 'Ripristino completato') -eq $before -and @(Get-TestWindows).Count -eq 1)
    # La scheda chiusa prima della finestra: la sua finestra non c'è più, quindi torna in quella in uso
    # (è la finestra in primo piano quando si preme la combinazione).
    if ($w) { [XpxKit]::SwitchToThisWindow([IntPtr]$w, $true); Start-Sleep -Milliseconds 700 }
    Invoke-ReopenTab
    Check 'Una scheda la cui finestra non c''è più torna in quella in uso' (Wait-Until { (Format-DevSession) -match ',\*C\]$' } 12000) (Format-DevSession)
    $before = Get-LogCount 'niente da riaprire'
    Invoke-ReopenTab
    Invoke-ReopenTab
    Start-Sleep -Milliseconds 800
    Check 'Senza niente da riaprire non succede nulla' ((Get-LogCount 'niente da riaprire') -gt $before -and @(Get-TestWindows).Count -eq 1)
    if ($w -and [XpxKit]::GetForegroundWindow() -eq [IntPtr]$w) {
        Set-XpxOption 'ReopenTab' 'Enabled' '0'
        Check 'Scorciatoia spenta: la combinazione torna agli altri programmi' (Wait-Until { -not (Test-HotkeyHeld) } 5000)
        Set-XpxOption 'ReopenTab' 'Enabled' '1'
        Check 'Scorciatoia riaccesa: la combinazione torna a noi' (Wait-Until { Test-HotkeyHeld } 5000)
    }
    # Chiusa l'ultima finestra e riaperto Esplora file, la sessione torna da sola: quella finestra non
    # è più "chiusa", e Ctrl+Maiusc+T non deve riaprirla una seconda volta.
    if ($w) {
        Close-Window $w
        Wait-Until { @(Get-TestWindows).Count -eq 0 } 8000 | Out-Null
        Start-Sleep -Milliseconds 900
        $r = Open-AndWaitRestore $T
        if ($r.Hwnd) { [XpxKit]::SwitchToThisWindow([IntPtr]$r.Hwnd, $true); Start-Sleep -Milliseconds 700 }
        $before = Get-LogCount 'Riaperta la finestra chiusa'
        Invoke-ReopenTab
        Start-Sleep -Seconds 4
        Check 'Dopo il ripristino della sessione Ctrl+Maiusc+T non riapre la stessa finestra' ($r.Restored -and @(Get-TestWindows).Count -eq 1 -and (Get-LogCount 'Riaperta la finestra chiusa') -eq $before) ("finestre di prova: {0}" -f @(Get-TestWindows).Count)
        # Come in Chrome: chiusa una scheda e poi la finestra (l'ultima), riaperto Esplora file (torna la sessione, senza
        # quella scheda), Ctrl+Maiusc+T riporta la scheda chiusa prima della finestra.
        $w = @(Get-TestWindows) | Select-Object -First 1
        if ($w -and (Format-DevSession) -match 'B') {
            Close-TestTab $w 'B'
            Wait-Until { (Format-DevSession) -notmatch 'B' } 8000 | Out-Null
            Close-Window $w
            Wait-Until { @(Get-TestWindows).Count -eq 0 } 8000 | Out-Null
            Start-Sleep -Milliseconds 900
            $r2 = Open-AndWaitRestore $T
            if ($r2.Hwnd) { [XpxKit]::SwitchToThisWindow([IntPtr]$r2.Hwnd, $true); Start-Sleep -Milliseconds 700 }
            Invoke-ReopenTab
            Check 'Chiusa una scheda, poi la finestra, riaperto Esplora file: Ctrl+Maiusc+T riporta la scheda' ($r2.Restored -and (Wait-Until { (Format-DevSession) -match 'B' } 12000)) (Format-DevSession)
            # La pila è su disco: resta riavviando la tray (o il PC)
            $w = @(Get-TestWindows) | Select-Object -First 1
            if ($w -and (Format-DevSession) -match 'A') {
                Close-TestTab $w 'A'
                Wait-Until { (Format-DevSession) -notmatch 'A' } 8000 | Out-Null
                Start-Sleep -Milliseconds 800
                Check '  pila delle schede chiuse su disco (closed-dev.txt)' (Test-Path (Join-Path $XpxData 'closed-dev.txt'))
                Stop-Tray
                Start-Tray
                Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000 | Out-Null
                Start-Sleep -Milliseconds 800
                if ([XpxKit]::GetForegroundWindow() -ne [IntPtr]$w) { [XpxKit]::SwitchToThisWindow([IntPtr]$w, $true); Start-Sleep -Milliseconds 700 }
                Invoke-ReopenTab
                Check 'Riavviata la tray, Ctrl+Maiusc+T riporta ancora la scheda chiusa' (Wait-Until { (Format-DevSession) -match 'A' } 12000) (Format-DevSession)
            }
            else { Check 'Riavviata la tray, Ctrl+Maiusc+T riporta ancora la scheda chiusa' $false ("sessione inattesa: {0}" -f (Format-DevSession)) }
        }
        else { Check 'Chiusa una scheda, poi la finestra, riaperto Esplora file: Ctrl+Maiusc+T riporta la scheda' $false ("sessione inattesa: {0}" -f (Format-DevSession)) }
    }
}
catch {
    Check 'Esecuzione completa' $false $_.Exception.Message
}
finally {
    if (-not $KeepWindows) { Close-TestWindowsAs $userPlacement }
    if ($settingsBefore) { [IO.File]::WriteAllBytes($settingsFile, $settingsBefore) }
    if ($devFilterOurs) { Remove-Item -LiteralPath $XpxDevFilter -ErrorAction SilentlyContinue }
    if (-not $KeepWindows) { Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue }
    Check 'Finestre dell''utente non toccate' ((Get-UserWindowCount) -eq $userWindows -and (Get-UserTabCount) -eq $userTabs) ("finestre {0}, schede {1}" -f (Get-UserWindowCount), (Get-UserTabCount))
    # Una finestra riaperta deve chiudersi al primo comando, come con la X
    Check 'Le finestre si chiudono al primo comando' ($script:XpxCloseRetries.Count -eq 0) ($script:XpxCloseRetries -join '; ')
    # Nessuna finestra di Esplora file (neanche dell'utente) resta velata
    $veiled = 0
    for ($h = [XpxKit]::FindWindowEx([IntPtr]::Zero, [IntPtr]::Zero, 'CabinetWClass', [IntPtr]::Zero); $h -ne [IntPtr]::Zero; $h = [XpxKit]::FindWindowEx([IntPtr]::Zero, $h, 'CabinetWClass', [IntPtr]::Zero)) {
        if ([XpxAppearance]::IsVeiled($h)) { $veiled++ }
    }
    Check 'Nessuna finestra resta invisibile' ($veiled -eq 0) "velate: $veiled"
    Remove-Item -LiteralPath $XpxDevArm, (Join-Path $XpxData 'dev-drop-newtab.txt'), (Join-Path $XpxData 'closed-dev.txt') -ErrorAction SilentlyContinue
    # Con qualche errore la tray non resta ad agire sulle finestre dell'utente.
    if (@($results | Where-Object { -not $_.Ok }).Count -gt 0) {
        Stop-Tray
        Write-Warning 'Test falliti: la tray di VTabsExplorer è stata chiusa.'
    }
}

$failed = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ''
Write-Host ("Risultato: {0} test superati, {1} falliti." -f ($results.Count - $failed), $failed) -ForegroundColor $(if ($failed) { 'Red' } else { 'Green' })
exit $failed
