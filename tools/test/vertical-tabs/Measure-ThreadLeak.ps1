# Bisezione della perdita dei thread delle finestre chiuse: per ogni configurazione apre una finestra di prova in un
# explorer.exe separato (/separate), lascia entrare la mod, chiude la finestra e guarda se il suo thread resta vivo.
# Finestre dell'utente isolate (dev-windows.txt), tray mai nel processo della barra (SkipShellProcess=1).
param([string[]]$Configs = @('none', 'vt', 'nav', 'vt+nav'))
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\TestKit.ps1')
Add-Type -Namespace XL -Name W -MemberDefinition @'
public delegate bool EnumProc(System.IntPtr h, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, System.IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint p);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(System.IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, System.IntPtr t);
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenThread(uint access, bool inherit, uint id);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
[DllImport("ntdll.dll")] public static extern int NtQueryInformationThread(System.IntPtr h, int cls, out System.IntPtr info, int len, System.IntPtr ret);
public static long StartOf(uint tid) {
  System.IntPtr h = OpenThread(0x0040, false, tid);
  if (h == System.IntPtr.Zero) return -1;
  System.IntPtr a; int s = NtQueryInformationThread(h, 9, out a, System.IntPtr.Size, System.IntPtr.Zero);
  CloseHandle(h);
  return s == 0 ? a.ToInt64() : -2;
}
public static System.Collections.Generic.List<string> WindowsOf(uint pid) {
  var r = new System.Collections.Generic.List<string>();
  EnumWindows((h, l) => { uint p; uint t = GetWindowThreadProcessId(h, out p); if (p == pid) { var sb = new System.Text.StringBuilder(256); GetClassName(h, sb, 256); r.Add(t + "|" + sb); } return true; }, System.IntPtr.Zero);
  return r;
}
'@
function Test-ModIn([int]$ProcessId) {
    $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    $p -and [bool](@($p.Modules | Where-Object { $_.ModuleName -eq 'VTabsExplorerMod.dll' }).Count)
}
# Thread con finestre WorkerW ma senza CabinetWClass: il thread di una finestra chiusa ancora vivo
function Get-OrphanThreads([int]$ProcessId) {
    $byThread = [XL.W]::WindowsOf([uint32]$ProcessId) | ForEach-Object { $t, $c = $_ -split '\|'; [pscustomobject]@{ T = $t; C = $c } } | Group-Object T
    $shcore = (Get-Process -Id $ProcessId).Modules | Where-Object { $_.ModuleName -eq 'shcore.dll' } | Select-Object -First 1
    $lo = [int64]$shcore.BaseAddress; $hi = $lo + $shcore.ModuleMemorySize
    @($byThread | Where-Object { ($_.Group.C -contains 'WorkerW') -and ($_.Group.C -notcontains 'CabinetWClass') } |
        Where-Object { $sa = [XL.W]::StartOf([uint32]$_.Name); $sa -ge $lo -and $sa -lt $hi } | ForEach-Object { $_.Name })
}

$trayWnd = [XL.W]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero)
$shellPid = 0; [void][XL.W]::GetWindowThreadProcessId($trayWnd, [ref]$shellPid)
$settingsFile = Join-Path $XpxData 'settings.ini'
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente: prima chiudere l''altra prova' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userWindows = Get-UserWindowCount
$T = $XpxTestRoot
$started = @()
try {
    Enter-VtIsolation
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    'A', 'B' | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $T $_) | Out-Null }
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1'
    Set-XpxOption 'VerticalTabs' 'Suspended' '0'
    Start-Tray
    foreach ($config in $Configs) {
        $features, $switches = $config -split ':', 2
        $devMod = Join-Path $XpxData 'dev-mod.txt'
        if ($switches) { [IO.File]::WriteAllText($devMod, $switches) } else { Remove-Item -LiteralPath $devMod -ErrorAction SilentlyContinue }
        Set-XpxOption 'VerticalTabs' 'Enabled' $(if ($features -match 'vt') { '1' } else { '0' })
        Set-XpxOption 'NavBar' 'Enabled' $(if ($features -match 'nav') { '1' } else { '0' })
        Start-Sleep -Milliseconds 800
        $before = @(Get-Process explorer | ForEach-Object { $_.Id })
        Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
        Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
        $w = @(Get-TestWindows)[0]
        Add-VtTestWindow $w
        $sepPid = 0; [void][XL.W]::GetWindowThreadProcessId([IntPtr]$w, [ref]$sepPid)
        if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "finestra di prova non in un processo separato ($sepPid)" }
        $started += $sepPid
        if ($features -eq 'none') { & $XpxExe --inject-pid $sepPid | Out-Null }
        $mod = Wait-Until { Test-ModIn $sepPid } 10000
        $attached = $false
        if ($features -match 'vt') {
            $attached = Wait-Until {
                $t = [XpxKit]::FindWindowEx([IntPtr]$w, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
                $r = New-Object XpxKit+RECT; [void][XpxKit]::GetWindowRect($t, [ref]$r)
                $m = New-Object XpxKit+RECT; [void][XpxKit]::GetWindowRect([IntPtr]$w, [ref]$m)
                ($r.Left - $m.Left) -gt 100
            } 12000
        }
        Start-Sleep -Milliseconds 2500
        Close-Window $w
        Start-Sleep -Milliseconds 4000
        if (-not (Get-Process -Id $sepPid -ErrorAction SilentlyContinue)) {
            $crash = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = (Get-Date).AddMinutes(-2) } -ErrorAction SilentlyContinue |
                Where-Object { $_.Message -match ('0x{0:x}' -f $sepPid) } | Select-Object -First 1
            "{0,-8}: mod {1}, PROCESSO TERMINATO{2}" -f $config, $mod, $(if ($crash) { ' (crash: ' + (($crash.Message -split "`n" | Where-Object { $_ -match 'modulo non valido|Codice eccezione' }) -join '; ').Trim() + ')' } else { '' })
            continue
        }
        $orphans = Get-OrphanThreads $sepPid
        "{0,-8}: mod {1}, schede verticali {2}, thread di finestre chiuse ancora vivi: {3}" -f $config, $mod, $(if ($features -match 'vt') { $attached } else { '-' }), $orphans.Count
        Get-Content (Join-Path $XpxData 'logs\mod.log') -Encoding UTF8 -Tail 400 | Where-Object { $_ -match "\[$sepPid[:\]]" -and $_ -match 'Agganci|Ricerca|Nuova finestra|chiusa|errore' } |
            ForEach-Object { '    ' + $_.Substring(11) }
    }
}
finally {
    Remove-Item -LiteralPath (Join-Path $XpxData 'dev-mod.txt') -ErrorAction SilentlyContinue
    Close-TestWindows
    Start-Sleep -Milliseconds 500
    foreach ($id in $started) {
        $p = Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($p -and $id -ne $shellPid -and -not ([XL.W]::WindowsOf([uint32]$id) -match '\|CabinetWClass$')) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue }
    }
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $XpxDevFilter -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Exit-VtIsolation
    Stop-Tray; Start-Tray
    "finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows)
}
