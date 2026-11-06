# Come la mod trova le finestre (Discovery.h, senza diagnostica XAML), in un explorer.exe di prova (/separate) con le
# finestre dell'utente isolate: la finestra già aperta quando entra la mod si trova con la ricerca UI Automation; una
# finestra nuova nello stesso processo (Ctrl+N = WM_COMMAND 0x7105 alla scheda) con l'aggancio a OnApplyTemplate, senza
# ricerca; tutte e due con le schede verticali. Chiuse le finestre, nessun thread di finestra deve restare vivo (con la
# diagnostica XAML ne restava uno per finestra: vedi README, "Problema noto").
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'TestKit.ps1')
Add-Type -Namespace XN -Name W -MemberDefinition @'
public delegate bool EnumProc(System.IntPtr h, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, System.IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint p);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(System.IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, System.IntPtr t);
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenThread(uint access, bool inherit, uint id);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
[DllImport("ntdll.dll")] public static extern int NtQueryInformationThread(System.IntPtr h, int cls, out System.IntPtr info, int len, System.IntPtr ret);
public static System.Collections.Generic.List<string> WindowsOf(uint pid) {
  var r = new System.Collections.Generic.List<string>();
  EnumWindows((h, l) => { uint p; uint t = GetWindowThreadProcessId(h, out p); if (p == pid) { var sb = new System.Text.StringBuilder(256); GetClassName(h, sb, 256); r.Add(t + "|" + sb + "|" + h.ToInt64()); } return true; }, System.IntPtr.Zero);
  return r;
}
public static long StartOf(uint tid) {
  System.IntPtr h = OpenThread(0x0040, false, tid);
  if (h == System.IntPtr.Zero) return -1;
  System.IntPtr a; int s = NtQueryInformationThread(h, 9, out a, System.IntPtr.Size, System.IntPtr.Zero);
  CloseHandle(h);
  return s == 0 ? a.ToInt64() : -2;
}
'@
function Get-OrphanThreads([int]$ProcessId) {
    $byThread = [XN.W]::WindowsOf([uint32]$ProcessId) | ForEach-Object { $t, $c, $h = $_ -split '\|'; [pscustomobject]@{ T = $t; C = $c } } | Group-Object T
    $shcore = (Get-Process -Id $ProcessId).Modules | Where-Object { $_.ModuleName -eq 'shcore.dll' } | Select-Object -First 1
    $lo = [int64]$shcore.BaseAddress; $hi = $lo + $shcore.ModuleMemorySize
    @($byThread | Where-Object { ($_.Group.C -contains 'WorkerW') -and ($_.Group.C -notcontains 'CabinetWClass') } |
        Where-Object { $sa = [XN.W]::StartOf([uint32]$_.Name); $sa -ge $lo -and $sa -lt $hi } | ForEach-Object { $_.Name })
}
function Get-ContentX([int64]$Main) {
    $t = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
    $r = New-Object XpxKit+RECT; [void][XpxKit]::GetWindowRect($t, [ref]$r)
    $m = New-Object XpxKit+RECT; [void][XpxKit]::GetWindowRect([IntPtr]$Main, [ref]$m)
    $r.Left - $m.Left
}
function Cabinets([int]$ProcessId) { @([XN.W]::WindowsOf([uint32]$ProcessId) | Where-Object { ($_ -split '\|')[1] -eq 'CabinetWClass' } | ForEach-Object { [int64](($_ -split '\|')[2]) }) }

$trayWnd = [XN.W]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero)
$shellPid = 0; [void][XN.W]::GetWindowThreadProcessId($trayWnd, [ref]$shellPid)
$settingsFile = Join-Path $XpxData 'settings.ini'
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userWindows = Get-UserWindowCount
$T = $XpxTestRoot
$sepPid = 0
$script:fails = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') { if ($Ok) { "  OK   $Name $Detail" } else { $script:fails++; "  FAIL $Name $Detail" } }
$logStart = @(Get-Content (Join-Path $XpxData 'logs\mod.log') -Encoding UTF8).Count
try {
    Enter-VtIsolation
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    'A' | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $T $_) | Out-Null }
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1'
    Set-XpxOption 'VerticalTabs' 'Suspended' '0'
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Set-XpxOption 'NavBar' 'Enabled' '1'
    Start-Tray
    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
    $w1 = @(Get-TestWindows)[0]
    Add-VtTestWindow $w1
    [void][XN.W]::GetWindowThreadProcessId([IntPtr]$w1, [ref]$sepPid)
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "non è un processo separato ($sepPid)" }
    Check 'finestra già aperta: schede verticali' (Wait-Until { (Get-ContentX $w1) -gt 100 } 12000)
    Start-Sleep -Milliseconds 1500
    # Ctrl+N
    $before = Cabinets $sepPid
    $t0 = Get-Date
    $tab = [XpxKit]::FindWindowEx([IntPtr]$w1, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero); [void][XpxKit]::PostMessage($tab, 0x0111, [IntPtr]0x7105, [IntPtr]::Zero)
    $script:w2 = 0
    $appeared = Wait-Until { $script:w2 = @(Cabinets $sepPid | Where-Object { $before -notcontains $_ } | Where-Object { [XpxKit]::IsWindowVisible([IntPtr]$_) }) | Select-Object -First 1; [bool]$script:w2 } 8000
    Check 'Ctrl+N: finestra nuova nello stesso processo' $appeared
    if ($appeared) {
        Add-VtTestWindow $script:w2
        $ok = Wait-Until { (Get-ContentX $script:w2) -gt 100 } 8000
        Check '  con le schede verticali' $ok ("dopo {0:N0} ms" -f ((Get-Date) - $t0).TotalMilliseconds)
    }
    Start-Sleep -Milliseconds 1500
    $log = @(Get-Content (Join-Path $XpxData 'logs\mod.log') -Encoding UTF8 | Select-Object -Skip $logStart | Where-Object { $_ -match "\[$sepPid[:\]]" })
    Check 'agganci installati' ([bool]($log -match 'Agganci: OnApplyTemplate di TabView installato, GetAutomationControlTypeCore di .*TabViewItemAutomationPeer installato'))
    Check 'finestra già aperta trovata dalla ricerca' ([bool]($log -match 'Ricerca della barra delle schede .*: trovata'))
    Check 'due finestre gestite, la seconda senza ricerca' ([bool]($log -match 'finestre gestite: 2') -and @($log -match 'Ricerca della barra delle schede .*: trovata').Count -eq 1)
    Check 'nessun errore della mod' (-not ($log -match 'errore|Eccezione'))
    "--- log della mod:"
    $log | Where-Object { $_ -match 'Agganci|Ricerca|Nuova finestra|chiusa' } | ForEach-Object { '  ' + $_.Substring(11) }
    foreach ($h in Cabinets $sepPid) { Close-Window $h }
    Start-Sleep -Milliseconds 4000
    if (Get-Process -Id $sepPid -ErrorAction SilentlyContinue) { $orphans = @(Get-OrphanThreads $sepPid).Count; Check 'chiuse le finestre, nessun thread resta vivo' ($orphans -eq 0) "(vivi: $orphans)" }
    else { Check 'processo di prova ancora vivo dopo la chiusura (non deve cadere)' $false }
}
finally {
    Close-TestWindows
    Start-Sleep -Milliseconds 500
    if ($sepPid -and $sepPid -ne $shellPid -and (Get-Process -Id $sepPid -ErrorAction SilentlyContinue) -and -not (Cabinets $sepPid)) { Stop-Process -Id $sepPid -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $XpxDevFilter -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Exit-VtIsolation
    Stop-Tray; Start-Tray
    Check 'finestre dell''utente invariate' ((Get-UserWindowCount) -eq $userWindows)
    "errori: $script:fails"
}
