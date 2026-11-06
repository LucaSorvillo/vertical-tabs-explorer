<#
.SYNOPSIS
    Kit di test di VTabsExplorer (da includere: . .\tools\test\TestKit.ps1).

.DESCRIPTION
    Le prove usano solo finestre di Esplora file aperte apposta su cartelle in %TEMP%\xpx-test, con la
    tray in "modalità prova" (dev-filter.txt): VTabsExplorer considera solo quelle finestre e usa un file
    di sessione separato (session-dev.txt). Le finestre dell'utente non vengono mai toccate né
    elencate: i loro percorsi non vengono letti né stampati (si contano soltanto).
    Nessun input reale: finestre e schede si aprono con le API della shell, l'ordine delle schede si
    legge con UI Automation.
#>

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$XAE = [Windows.Automation.AutomationElement]
$XTS = [Windows.Automation.TreeScope]

$XpxRepo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$XpxExe = Join-Path $XpxRepo 'build\bin\VTabsExplorer.exe'
$XpxData = Join-Path $env:LOCALAPPDATA 'VTabsExplorer'
$XpxTestRoot = Join-Path $env:TEMP 'xpx-test'
$XpxDevFilter = Join-Path $XpxData 'dev-filter.txt'
$XpxDevSession = Join-Path $XpxData 'session-dev.txt'
# In modalità prova VTabsExplorer vela le finestre solo mentre esiste questo file
$XpxDevArm = Join-Path $XpxData 'dev-arm.txt'
# Schede verticali, isolamento dei test: mentre esiste questo file la mod applica le impostazioni solo
# alle finestre elencate (HWND esadecimali); le altre, cioè quelle dell'utente, restano esattamente come
# sono. Va creato PRIMA di cambiare le impostazioni e tolto DOPO averle rimesse.
$XpxDevWindows = Join-Path $XpxData 'dev-windows.txt'
function Enter-VtIsolation { [IO.File]::WriteAllText($XpxDevWindows, "0`r`n", [Text.Encoding]::ASCII); Start-Sleep -Milliseconds 400 }
function Add-VtTestWindow([int64]$Hwnd) { [IO.File]::AppendAllText($XpxDevWindows, ('{0:x}' -f $Hwnd) + "`r`n", [Text.Encoding]::ASCII) }
function Exit-VtIsolation { Start-Sleep -Milliseconds 600; Remove-Item -LiteralPath $XpxDevWindows -ErrorAction SilentlyContinue }

if (-not ('XpxKit' -as [type])) {
    Add-Type -Namespace '' -Name XpxKit -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
[StructLayout(LayoutKind.Sequential)] public struct WINDOWPLACEMENT { public int length, flags, showCmd; public POINT ptMin, ptMax; public RECT rcNormal; }
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
public struct SHELLEXECUTEINFO {
  public int cbSize; public uint fMask; public IntPtr hwnd;
  [MarshalAs(UnmanagedType.LPWStr)] public string lpVerb; [MarshalAs(UnmanagedType.LPWStr)] public string lpFile;
  [MarshalAs(UnmanagedType.LPWStr)] public string lpParameters; [MarshalAs(UnmanagedType.LPWStr)] public string lpDirectory;
  public int nShow; public IntPtr hInstApp; public IntPtr lpIDList; [MarshalAs(UnmanagedType.LPWStr)] public string lpClass;
  public IntPtr hkeyClass; public uint dwHotKey; public IntPtr hIcon; public IntPtr hProcess; }
[DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern bool ShellExecuteEx(ref SHELLEXECUTEINFO i);
// SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI: con una cartella inesistente fallisce invece di mostrare
// una finestra di errore che bloccherebbe il test
public static bool OpenNewWindow(string path) {
  var i = new SHELLEXECUTEINFO(); i.cbSize = Marshal.SizeOf(i); i.fMask = 0x0000040C; i.lpVerb = "opennewwindow"; i.lpFile = path; i.nShow = 1;
  return ShellExecuteEx(ref i); }
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, IntPtr t);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern bool GetWindowPlacement(IntPtr h, ref WINDOWPLACEMENT p);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
[DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool altTab);
public delegate bool EnumProc(IntPtr h, IntPtr p);
[DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
public static IntPtr FindDescendant(IntPtr root, string cls) {
  IntPtr found = IntPtr.Zero;
  EnumChildWindows(root, (h, p) => { var sb = new System.Text.StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == cls) { found = h; return false; } return true; }, IntPtr.Zero);
  return found; }
[DllImport("user32.dll")] public static extern bool SetWindowPlacement(IntPtr h, ref WINDOWPLACEMENT p);
[DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool repaint);
[DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
[DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
[DllImport("user32.dll", SetLastError = true)] public static extern bool RegisterHotKey(IntPtr h, int id, uint mods, uint vk);
[DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr h, int id);
'@
}
[XpxKit]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null # coordinate fisiche, come VTabsExplorer

# Osservatore della comparsa delle finestre: ogni 5 ms, per ogni finestra di Esplora file nuova, se è
# visibile, se è velata (WS_EX_LAYERED con opacità zero) e quante schede ha (finestre ShellTabWindowClass).
# Alla fine dice per quanto tempo ciascuna è stata visibile con meno schede di quelle finali.
if (-not ('XpxAppearance' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

public class XpxAppearance
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, IntPtr title);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr h, int index);
    [DllImport("user32.dll")] static extern bool GetLayeredWindowAttributes(IntPtr h, out uint key, out byte alpha, out uint flags);

    public class Result
    {
        public long Hwnd;
        public bool Veiled;              // velata almeno una volta
        public int FirstVisibleTabs = -1; // schede la prima volta che si è vista
        public long VisibleIncompleteMs; // tempo visibile con meno schede di quelle finali
        public int FinalTabs;
        public long FirstSeenMs, VeiledUntilMs = -1;
        public long FirstShownMs = -1;   // prima volta con WS_VISIBLE (la finestra aperta è la prima a comparire)
    }
    struct Sample { public long Ms; public bool Visible, Veiled; public int Tabs; }

    readonly HashSet<IntPtr> initial = new HashSet<IntPtr>();
    readonly Dictionary<IntPtr, List<Sample>> samples = new Dictionary<IntPtr, List<Sample>>();
    Thread thread;
    volatile bool stop;

    public static bool IsVeiled(IntPtr h)
    {
        uint key, flags; byte alpha;
        return (GetWindowLong(h, -20) & 0x80000) != 0 && GetLayeredWindowAttributes(h, out key, out alpha, out flags) && (flags & 2) != 0 && alpha == 0;
    }
    static List<IntPtr> Frames()
    {
        var list = new List<IntPtr>();
        for (IntPtr h = FindWindowEx(IntPtr.Zero, IntPtr.Zero, "CabinetWClass", IntPtr.Zero); h != IntPtr.Zero; h = FindWindowEx(IntPtr.Zero, h, "CabinetWClass", IntPtr.Zero)) list.Add(h);
        return list;
    }
    public static int TabCount(IntPtr frame)
    {
        int n = 0;
        for (IntPtr t = FindWindowEx(frame, IntPtr.Zero, "ShellTabWindowClass", IntPtr.Zero); t != IntPtr.Zero; t = FindWindowEx(frame, t, "ShellTabWindowClass", IntPtr.Zero)) n++;
        return n;
    }
    public void Start()
    {
        // Solo quelle già visibili: Esplora file può tenere pronta una finestra nascosta e mostrare quella
        foreach (var h in Frames()) if (IsWindowVisible(h)) initial.Add(h);
        thread = new Thread(Loop) { IsBackground = true };
        thread.Start();
    }
    void Loop()
    {
        var clock = Stopwatch.StartNew();
        while (!stop)
        {
            foreach (var h in Frames())
            {
                if (initial.Contains(h)) continue;
                List<Sample> list;
                if (!samples.TryGetValue(h, out list)) { list = new List<Sample>(); samples[h] = list; }
                list.Add(new Sample { Ms = clock.ElapsedMilliseconds, Visible = IsWindowVisible(h), Veiled = IsVeiled(h), Tabs = TabCount(h) });
            }
            Thread.Sleep(5);
        }
    }
    public Result[] Stop()
    {
        stop = true;
        thread.Join();
        var results = new List<Result>();
        foreach (var entry in samples)
        {
            var list = entry.Value;
            var r = new Result { Hwnd = entry.Key.ToInt64(), FinalTabs = list[list.Count - 1].Tabs, FirstSeenMs = list[0].Ms };
            for (int i = 0; i < list.Count; i++)
            {
                var s = list[i];
                if (s.Visible && r.FirstShownMs < 0) r.FirstShownMs = s.Ms;
                if (s.Veiled) { r.Veiled = true; r.VeiledUntilMs = s.Ms; }
                if (s.Visible && !s.Veiled)
                {
                    if (r.FirstVisibleTabs < 0) r.FirstVisibleTabs = s.Tabs;
                    if (s.Tabs < r.FinalTabs && i + 1 < list.Count) r.VisibleIncompleteMs += list[i + 1].Ms - s.Ms;
                }
            }
            if (r.FirstShownMs >= 0) results.Add(r); // le finestre rimaste nascoste (precaricate) non contano
        }
        results.Sort((a, b) => a.FirstShownMs.CompareTo(b.FirstShownMs));
        return results.ToArray();
    }
}
'@
}

function Wait-Until([scriptblock]$Condition, [int]$TimeoutMs = 5000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) { if (& $Condition) { return $true }; Start-Sleep -Milliseconds 150 }
    [bool](& $Condition)
}
function Test-UnderTestRoot([string]$Path) {
    $Path -and ($Path -eq $XpxTestRoot -or $Path.StartsWith($XpxTestRoot + '\', [StringComparison]::OrdinalIgnoreCase))
}

# Tutte le schede di Esplora file (una voce per scheda). I percorsi delle finestre dell'utente restano
# in memoria solo per escluderle: non vengono mai stampati.
function Get-ShellTabs {
    $shell = New-Object -ComObject Shell.Application
    @($shell.Windows() | ForEach-Object {
            $item = $_
            $path = try { $item.Document.Folder.Self.Path } catch { $null }
            [pscustomobject]@{ Hwnd = [int64]$item.HWND; Path = $path; Item = $item }
        })
}
# Finestre di prova: almeno una scheda dentro %TEMP%\xpx-test e le altre lì dentro o su cartelle
# virtuali (es. "Questo PC" di una scheda appena aperta). Tutte le altre sono finestre dell'utente.
function Test-TestWindowGroup($Group) {
    @($Group | Where-Object { $_.Path -and -not (Test-UnderTestRoot $_.Path) -and $_.Path -notlike '::*' }).Count -eq 0 -and
    @($Group | Where-Object { Test-UnderTestRoot $_.Path }).Count -gt 0
}
function Get-TestWindows { @(Get-ShellTabs | Group-Object Hwnd | Where-Object { Test-TestWindowGroup $_.Group } | ForEach-Object { [int64]$_.Name }) }
function Get-UserWindowCount { @(Get-ShellTabs | Group-Object Hwnd | Where-Object { -not (Test-TestWindowGroup $_.Group) }).Count }
function Get-UserTabCount { @(Get-ShellTabs | Group-Object Hwnd | Where-Object { -not (Test-TestWindowGroup $_.Group) } | ForEach-Object { $_.Group }).Count }
function Get-TabPaths([int64]$Hwnd) { @(Get-ShellTabs | Where-Object { $_.Hwnd -eq $Hwnd } | ForEach-Object { $_.Path }) }
function Short([string]$Path) { if (Test-UnderTestRoot $Path) { $Path.Substring($XpxTestRoot.Length).TrimStart('\') } elseif ($Path -like '::*') { '<virtuale>' } else { '<altra>' } }

# Le isole XAML della mod (pannello delle schede verticali, barra delle cartelle) portano un contrassegno
# (proprietà di finestra): con la colonna in cima alla finestra il pannello sta alla stessa altezza
# dell'intestazione, quindi non si distinguono più dalla posizione.
if (-not ('XpxProp' -as [type])) {
    Add-Type -Namespace '' -Name XpxProp -MemberDefinition '[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h, string name);'
}
function Test-ModIsland([IntPtr]$H) {
    [XpxProp]::GetProp($H, 'VTabsExplorer.VerticalTabsPanel') -ne [IntPtr]::Zero -or [XpxProp]::GetProp($H, 'VTabsExplorer.NavBar') -ne [IntPtr]::Zero
}
# Isola XAML dell'intestazione (striscia schede, barra degli indirizzi): la più in alto tra le isole
# VISIBILI della finestra che non sono della mod (la tray la cerca nello stesso modo).
function Get-HeaderIsland([int64]$Hwnd) {
    $best = [IntPtr]::Zero; $first = [IntPtr]::Zero; $bestTop = [int]::MaxValue
    $h = [XpxKit]::FindWindowEx([IntPtr]$Hwnd, [IntPtr]::Zero, 'Microsoft.UI.Content.DesktopChildSiteBridge', [IntPtr]::Zero)
    while ($h -ne [IntPtr]::Zero) {
        if (-not (Test-ModIsland $h)) {
            if ($first -eq [IntPtr]::Zero) { $first = $h }
            $r = New-Object XpxKit+RECT; [void][XpxKit]::GetWindowRect($h, [ref]$r)
            if ([XpxKit]::IsWindowVisible($h) -and $r.Top -lt $bestTop) { $bestTop = $r.Top; $best = $h }
        }
        $h = [XpxKit]::FindWindowEx([IntPtr]$Hwnd, $h, 'Microsoft.UI.Content.DesktopChildSiteBridge', [IntPtr]::Zero)
    }
    if ($best -ne [IntPtr]::Zero) { $best } else { $first }
}
# Risposta di una finestra a WM_NCHITTEST in un punto: 1 = HTCLIENT, 2 = HTCAPTION, -1 = HTTRANSPARENT,
# 8/9/20 = riduci/ingrandisci/chiudi, 12 = bordo in alto. Get-NcHit vuole coordinate client della finestra
# principale, Get-NcHitScreen dello schermo (fisiche).
if (-not ('XpxHit' -as [type])) {
    Add-Type -Namespace '' -Name XpxHit -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
'@
}
# Finestra che riceve il mouse vero in un punto (coordinate client della finestra principale): rispetta le
# regioni delle finestre (come il sistema; da un altro processo senza WM_NCHITTEST)
function Get-WindowAt([int64]$Main, [int]$X, [int]$Y) {
    $p = New-Object XpxHit+POINT; $p.X = $X; $p.Y = $Y; [void][XpxHit]::ClientToScreen([IntPtr]$Main, [ref]$p)
    [int64][XpxHit]::WindowFromPoint($p)
}
function Get-WindowAtScreen([int]$X, [int]$Y) { $p = New-Object XpxHit+POINT; $p.X = $X; $p.Y = $Y; [int64][XpxHit]::WindowFromPoint($p) }
# Come Get-WindowAt, ma solo tra le figlie dirette della finestra (le altre finestre sullo schermo, es. quelle dell'utente in
# primo piano sopra la finestra di prova, non contano): la prima nell'ordine Z, visibile, che contiene il punto (coordinate
# client) nel suo rettangolo e nella sua regione; la finestra stessa se nessuna. Le figlie layered contano come piene.
if (-not ('XpxChildAt' -as [type])) {
    Add-Type -Namespace '' -Name XpxChildAt -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
[DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h, uint cmd);
[DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] static extern int GetWindowRgn(IntPtr h, IntPtr rgn);
[DllImport("gdi32.dll")] static extern IntPtr CreateRectRgn(int l, int t, int r, int b);
[DllImport("gdi32.dll")] static extern bool PtInRegion(IntPtr rgn, int x, int y);
[DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr o);
public static IntPtr At(IntPtr main, int x, int y) {
  var p = new POINT { X = x, Y = y }; ClientToScreen(main, ref p);
  for (IntPtr c = GetWindow(main, 5); c != IntPtr.Zero; c = GetWindow(c, 2)) {
    if (!IsWindowVisible(c)) continue;
    RECT r; GetWindowRect(c, out r);
    if (p.X < r.Left || p.X >= r.Right || p.Y < r.Top || p.Y >= r.Bottom) continue;
    IntPtr rgn = CreateRectRgn(0, 0, 0, 0);
    try {
      int kind = GetWindowRgn(c, rgn); // 0 = nessuna regione, 1 = vuota
      if (kind == 1) continue;
      if (kind != 0 && !PtInRegion(rgn, p.X - r.Left, p.Y - r.Top)) continue;
    } finally { DeleteObject(rgn); }
    return c;
  }
  return main;
}
'@
}
function Get-ChildAt([int64]$Main, [int]$X, [int]$Y) { [int64][XpxChildAt]::At([IntPtr]$Main, $X, $Y) }
function Get-ChildAtScreen([int64]$Main, [int]$X, [int]$Y) { $p = New-Object XpxHit+POINT; $p.X = 0; $p.Y = 0; [void][XpxHit]::ClientToScreen([IntPtr]$Main, [ref]$p); Get-ChildAt $Main ($X - $p.X) ($Y - $p.Y) }
function Get-NcHitScreen([int64]$Target, [int]$X, [int]$Y) {
    [int64][XpxHit]::SendMessage([IntPtr]$Target, 0x84, [IntPtr]::Zero, [IntPtr](([int64]($Y -band 0xFFFF) -shl 16) -bor ($X -band 0xFFFF)))
}
function Get-NcHit([int64]$Target, [int64]$Main, [int]$X, [int]$Y) {
    $p = New-Object XpxHit+POINT; $p.X = $X; $p.Y = $Y; [void][XpxHit]::ClientToScreen([IntPtr]$Main, [ref]$p)
    Get-NcHitScreen $Target $p.X $p.Y
}
# Isola del pannello delle schede verticali (0 se non c'è)
function Get-PanelIsland([int64]$Hwnd) {
    $h = [XpxKit]::FindWindowEx([IntPtr]$Hwnd, [IntPtr]::Zero, 'Microsoft.UI.Content.DesktopChildSiteBridge', [IntPtr]::Zero)
    while ($h -ne [IntPtr]::Zero) {
        if ([XpxProp]::GetProp($h, 'VTabsExplorer.VerticalTabsPanel') -ne [IntPtr]::Zero) { return [int64]$h }
        $h = [XpxKit]::FindWindowEx([IntPtr]$Hwnd, $h, 'Microsoft.UI.Content.DesktopChildSiteBridge', [IntPtr]::Zero)
    }
    0
}
# Barra delle schede (UI Automation): nomi da sinistra a destra e scheda attiva. Le schede native ci sono
# anche con le schede verticali attive (la striscia resta viva, solo invisibile).
function Get-TabItems([int64]$Hwnd) {
    $island = Get-HeaderIsland $Hwnd
    $root = if ($island -ne [IntPtr]::Zero) { $XAE::FromHandle($island) } else { $XAE::FromHandle([IntPtr]$Hwnd) }
    # Ordine visivo (coordinata X): l'ordine dell'albero può essere "ruotato" dal riciclo degli elementi
    @($root.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::TabItem))) |
            Sort-Object { $_.Current.BoundingRectangle.X })
}
function Get-TabNames([int64]$Hwnd) { @(Get-TabItems $Hwnd | ForEach-Object { $_.Current.Name }) }
function Get-ActiveTabName([int64]$Hwnd) {
    Get-TabItems $Hwnd | Where-Object { $_.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected } |
        Select-Object -First 1 | ForEach-Object { $_.Current.Name }
}
function Select-TabByName([int64]$Hwnd, [string]$Name) {
    $item = Get-TabItems $Hwnd | Where-Object { $_.Current.Name -eq $Name } | Select-Object -First 1
    $item.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
}

# Apre una nuova finestra (sempre a sé) su una cartella di prova e ne restituisce l'HWND
function Open-TestWindow([string]$Path) {
    $before = @(Get-ShellTabs | ForEach-Object { $_.Hwnd } | Sort-Object -Unique)
    if (-not [XpxKit]::OpenNewWindow($Path)) { throw "Finestra non aperta su $Path" }
    $hwnd = 0
    Wait-Until { $script:found = @(Get-ShellTabs | Where-Object { $before -notcontains $_.Hwnd -and $_.Path -eq $Path }); $script:found.Count -gt 0 } 8000 | Out-Null
    if ($script:found.Count -gt 0) { $hwnd = $script:found[0].Hwnd }
    if (-not $hwnd) { throw "La finestra su $Path non è comparsa" }
    $hwnd
}
# Aggiunge una scheda (comando "nuova scheda" della finestra) e la porta sulla cartella
function Add-TestTab([int64]$Hwnd, [string]$Path) {
    $tab = [XpxKit]::FindWindowEx([IntPtr]$Hwnd, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
    $count = @(Get-TabPaths $Hwnd).Count
    [XpxKit]::PostMessage($tab, 0x0111, [IntPtr]0xA21B, [IntPtr]::Zero) | Out-Null
    if (-not (Wait-Until { @(Get-TabPaths $Hwnd | Where-Object { $_ }).Count -gt $count } 5000)) { throw 'Nuova scheda non comparsa' }
    # La scheda nuova è l'ultima elencata dalla shell per la finestra (non un'altra scheda su "Questo PC")
    $new = @(Get-ShellTabs | Where-Object { $_.Hwnd -eq $Hwnd })[-1]
    $new.Item.Navigate2($Path)
    if (-not (Wait-Until { (Get-TabPaths $Hwnd) -contains $Path } 5000)) { throw "Scheda non arrivata su $Path" }
}
# Nomi delle schede con la finestra ingrandita (tutte in vista, anche se la barra normalmente scorre),
# poi la finestra torna com'era
function Get-TabNamesWide([int64]$Hwnd) {
    $before = Get-Placement $Hwnd
    Set-WindowShow $Hwnd 3
    Start-Sleep -Milliseconds 700
    $names = Get-TabNames $Hwnd
    [XpxKit]::SetWindowPlacement([IntPtr]$Hwnd, [ref]$before) | Out-Null
    Start-Sleep -Milliseconds 400
    $names
}
function Get-ScreenWidth { [XpxKit]::GetSystemMetrics(0) }
# Aggiunge una scheda lasciata nella cartella iniziale (una cartella virtuale: "Questo PC" o "Home")
function Add-StartTab([int64]$Hwnd) {
    $tab = [XpxKit]::FindWindowEx([IntPtr]$Hwnd, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
    $count = @(Get-TabPaths $Hwnd).Count
    [XpxKit]::PostMessage($tab, 0x0111, [IntPtr]0xA21B, [IntPtr]::Zero) | Out-Null
    if (-not (Wait-Until { @(Get-TabPaths $Hwnd | Where-Object { $_ }).Count -gt $count } 5000)) { throw 'Nuova scheda non comparsa' }
}
function Set-WindowShow([int64]$Hwnd, [int]$ShowCmd) {
    $p = Get-Placement $Hwnd; $p.showCmd = $ShowCmd
    [XpxKit]::SetWindowPlacement([IntPtr]$Hwnd, [ref]$p) | Out-Null
}
function Set-XpxOption([string]$Section, [string]$Key, [string]$Value) {
    [XpxKit]::WritePrivateProfileString($Section, $Key, $Value, (Join-Path $XpxData 'settings.ini')) | Out-Null
}
# Chiude l'intera finestra (come la X: WM_CLOSE chiuderebbe solo la scheda attiva) e aspetta che sparisca;
# se il comando va perso lo ripete, annotandolo in $script:XpxCloseRetries
$script:XpxCloseRetries = @()
function Close-Window([int64]$Hwnd, [switch]$NoWait) {
    [XpxKit]::PostMessage([IntPtr]$Hwnd, 0x0112, [IntPtr]0xF060, [IntPtr]::Zero) | Out-Null
    if ($NoWait) { return }
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        if (Wait-Until { -not [XpxKit]::IsWindow([IntPtr]$Hwnd) } 3000) { return }
        $script:XpxCloseRetries += ('{0:X} (tentativo {1}, {2})' -f $Hwnd, $attempt, (Get-Date -Format 'HH:mm:ss.fff'))
        Write-Warning ("La finestra {0:X} non si è chiusa: ripeto il comando." -f $Hwnd)
        [XpxKit]::PostMessage([IntPtr]$Hwnd, 0x0112, [IntPtr]0xF060, [IntPtr]::Zero) | Out-Null
    }
}
function Close-TestWindows {
    foreach ($h in Get-TestWindows) { Close-Window $h -NoWait }
    Wait-Until { @(Get-TestWindows).Count -eq 0 } 8000 | Out-Null
    foreach ($h in Get-TestWindows) { Close-Window $h }
}
function Get-Placement([int64]$Hwnd) {
    $p = New-Object XpxKit+WINDOWPLACEMENT; $p.length = [Runtime.InteropServices.Marshal]::SizeOf($p)
    [XpxKit]::GetWindowPlacement([IntPtr]$Hwnd, [ref]$p) | Out-Null; $p
}
# Porta la finestra in stato normale in quella posizione e ripete finché non resta (appena aperta,
# Esplora file la sistema da sé; e può aprirsi ingrandita se l'ultima chiusa lo era)
# (in PowerShell rcNormal è una copia: il rettangolo va costruito a parte e assegnato intero)
function New-Placement([int]$ShowCmd, [int]$X, [int]$Y, [int]$W, [int]$H) {
    $p = New-Object XpxKit+WINDOWPLACEMENT
    $p.length = [Runtime.InteropServices.Marshal]::SizeOf($p)
    $p.showCmd = $ShowCmd
    $r = New-Object XpxKit+RECT
    $r.Left = $X; $r.Top = $Y; $r.Right = $X + $W; $r.Bottom = $Y + $H
    $p.rcNormal = $r
    $p
}
function Move-TestWindow([int64]$Hwnd, [int]$X, [int]$Y, [int]$W, [int]$H) {
    $ok = Wait-Until {
        $p = New-Placement 1 $X $Y $W $H # SW_SHOWNORMAL
        [XpxKit]::SetWindowPlacement([IntPtr]$Hwnd, [ref]$p) | Out-Null
        Start-Sleep -Milliseconds 300
        $p = Get-Placement $Hwnd
        $p.showCmd -eq 1 -and $p.rcNormal.Left -eq $X -and $p.rcNormal.Top -eq $Y -and $p.rcNormal.Right -eq $X + $W -and $p.rcNormal.Bottom -eq $Y + $H
    } 5000
    if (-not $ok) { throw 'La finestra di prova non resta nella posizione voluta' }
}
# Posizione e stato della prima finestra dell'utente (solo questo: niente percorsi né contenuto)
function Get-UserPlacement {
    $user = Get-ShellTabs | Group-Object Hwnd | Where-Object { -not (Test-TestWindowGroup $_.Group) } | Select-Object -First 1
    if ($user) { Get-Placement ([int64]$user.Name) }
}
# Chiude le finestre di prova; l'ultima prende prima posizione e stato indicati, perché Esplora file apre
# le finestre nuove come l'ultima chiusa (così le prossime finestre dell'utente non cambiano)
function Close-TestWindowsAs($Placement) {
    $windows = @(Get-TestWindows)
    if ($windows.Count -eq 0) { return }
    foreach ($h in $windows | Select-Object -SkipLast 1) { Close-Window $h -NoWait }
    Wait-Until { @(Get-TestWindows).Count -le 1 } 8000 | Out-Null
    $last = $windows[-1]
    if ($Placement) {
        $p = $Placement
        [XpxKit]::SetWindowPlacement([IntPtr]$last, [ref]$p) | Out-Null
        Start-Sleep -Milliseconds 400
    }
    Close-Window $last
    Wait-Until { @(Get-TestWindows).Count -eq 0 } 8000 | Out-Null
}

# Righe della sessione di prova. Lettura con condivisione completa (anche per l'eliminazione: la tray
# la sostituisce mentre la si legge) e qualche nuovo tentativo se il file è occupato in quell'istante.
function Read-DevSessionLines {
    for ($i = 0; $i -lt 20; $i++) {
        try {
            $fs = [IO.File]::Open($XpxDevSession, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
            try { return @((New-Object IO.StreamReader($fs, [Text.Encoding]::UTF8)).ReadToEnd() -split "`r?`n") }
            finally { $fs.Dispose() }
        }
        catch [IO.FileNotFoundException] { return @() }
        catch [IO.IOException] { Start-Sleep -Milliseconds 50 }
    }
    throw "session-dev.txt sempre occupato"
}
# Sessione salvata in modalità prova
function Read-DevSession {
    if (-not (Test-Path $XpxDevSession)) { return @() }
    $windows = @(); $current = $null
    foreach ($line in (Read-DevSessionLines)) {
        if ($line -eq 'window') { $current = [pscustomobject]@{ Tabs = @(); Active = 0; Rect = $null; ShowCmd = 0 }; $windows += $current; continue }
        if (-not $current) { continue }
        if ($line -like 'tab *') { $current.Tabs += (Short $line.Substring(4)) }
        elseif ($line -like 'active *') { $current.Active = [int]$line.Substring(7) }
        elseif ($line -like 'placement *') { $f = $line.Substring(10).Split(' '); $current.ShowCmd = [int]$f[1]; $current.Rect = @([int]$f[6], [int]$f[7], [int]$f[8], [int]$f[9]) }
    }
    , $windows
}
# "[A,*B,C] [D,*E]" (asterisco = scheda attiva)
function Format-DevSession {
    $windows = Read-DevSession
    (@($windows) | ForEach-Object {
            $w = $_; $i = 0
            '[' + (($w.Tabs | ForEach-Object { if ($i++ -eq $w.Active) { "*$_" } else { $_ } }) -join ',') + ']'
        }) -join ' '
}
# Stessa forma per una finestra aperta: nomi delle schede da UI Automation
function Format-Window([int64]$Hwnd) {
    $active = Get-ActiveTabName $Hwnd
    '[' + ((Get-TabNames $Hwnd | ForEach-Object { if ($_ -eq $active) { "*$_" } else { $_ } }) -join ',') + ']'
}

# Pulsante "Torna su": vista dei file della scheda attiva (DirectUIHWND), il suo ScrollPattern e la
# finestra del pulsante (della tray)
function Get-FileView([int64]$Hwnd) {
    $tab = [XpxKit]::FindWindowEx([IntPtr]$Hwnd, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
    if ($tab -eq [IntPtr]::Zero) { return [IntPtr]::Zero }
    $defView = [XpxKit]::FindDescendant($tab, 'SHELLDLL_DefView')
    if ($defView -eq [IntPtr]::Zero) { return [IntPtr]::Zero }
    [XpxKit]::FindWindowEx($defView, [IntPtr]::Zero, 'DirectUIHWND', [IntPtr]::Zero)
}
function Get-ViewScroll([int64]$Hwnd) {
    $view = Get-FileView $Hwnd
    if ($view -eq [IntPtr]::Zero) { return $null }
    $XAE::FromHandle($view).GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern)
}
function Get-BackToTopWindow { [XpxKit]::FindWindowEx([IntPtr]::Zero, [IntPtr]::Zero, 'VTabsExplorer.BackToTop', [IntPtr]::Zero) }
function Test-BackToTopShown { $b = Get-BackToTopWindow; $b -ne [IntPtr]::Zero -and [XpxKit]::IsWindowVisible($b) }
# Clic sul pulsante (messaggi del mouse al centro, senza input reale)
function Invoke-BackToTop {
    $b = Get-BackToTopWindow
    $r = New-Object XpxKit+RECT; [XpxKit]::GetWindowRect($b, [ref]$r) | Out-Null
    $half = [int](($r.Right - $r.Left) / 2)
    $point = [IntPtr](($half -shl 16) -bor $half)
    [XpxKit]::PostMessage($b, 0x0201, [IntPtr]1, $point) | Out-Null # WM_LBUTTONDOWN
    Start-Sleep -Milliseconds 60
    [XpxKit]::PostMessage($b, 0x0202, [IntPtr]0, $point) | Out-Null # WM_LBUTTONUP
}
# Riapri la scheda chiusa (Ctrl+Maiusc+T): chiusura di una scheda, comando della scorciatoia e verifica
# che la combinazione sia riservata, tutto senza premere tasti davvero
function Close-TestTab([int64]$Hwnd, [string]$Name) {
    Select-TabByName $Hwnd $Name
    Wait-Until { (Get-ActiveTabName $Hwnd) -eq $Name } 3000 | Out-Null
    # La scheda attiva è la prima ShellTabWindowClass nell'ordine Z; 0xA021 è il comando di Ctrl+W
    $tab = [XpxKit]::FindWindowEx([IntPtr]$Hwnd, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
    $before = @(Get-TabNames $Hwnd).Count
    [XpxKit]::PostMessage($tab, 0x0111, [IntPtr]0xA021, [IntPtr]::Zero) | Out-Null
    if (-not (Wait-Until { @(Get-TabNames $Hwnd).Count -lt $before } 5000)) { throw "La scheda $Name non si è chiusa" }
    Start-Sleep -Milliseconds 900 # la tray controlla le finestre ogni 500 ms
}
function Invoke-ReopenTab { & $XpxExe --reopen-tab; Start-Sleep -Milliseconds 300 }
# true se Ctrl+Maiusc+T è tenuta da un altro programma: se la tiene la tray, noi non riusciamo a
# registrarla (MOD_CONTROL | MOD_SHIFT, 'T')
function Test-HotkeyHeld {
    $ok = [XpxKit]::RegisterHotKey([IntPtr]::Zero, 9911, 6, 0x54)
    if ($ok) { [XpxKit]::UnregisterHotKey([IntPtr]::Zero, 9911) | Out-Null }
    -not $ok
}
function Get-XpxLog([int]$N = 30) {
    $path = Join-Path $XpxData 'logs\vtabsexplorer.log'
    if (Test-Path $path) { Get-Content $path -Encoding UTF8 -Tail $N }
}
function Start-Tray {
    if (-not (Get-Process VTabsExplorer -ErrorAction SilentlyContinue)) { Start-Process -FilePath $XpxExe -WorkingDirectory (Split-Path $XpxExe); Start-Sleep -Seconds 2 }
}
function Stop-Tray {
    & $XpxExe --exit
    Wait-Until { -not (Get-Process VTabsExplorer -ErrorAction SilentlyContinue) } 8000 | Out-Null
}
