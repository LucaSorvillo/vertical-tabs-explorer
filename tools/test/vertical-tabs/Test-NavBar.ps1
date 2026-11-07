# Riquadro di spostamento in alto ([NavBar] Enabled): barra delle cartelle sotto la barra dei comandi.
# Finestra di prova in un explorer.exe SEPARATO (/separate), finestre dell'utente isolate (Enter-VtIsolation).
# Le voci della barra sono le cartelle dell'utente: si confrontano con quelle del riquadro nativo solo nel
# codice (nomi mai stampati: solo conteggi).
# Riordino: Accesso rapido è uno solo per tutto l'utente, quindi si provano 3 cartelle di prova aggiunte e poi
# tolte (le finestre dell'utente le vedono per la durata della prova); l'elenco delle fissate si tiene in
# memoria all'inizio e alla fine deve essere identico. Con -Mouse anche trascinamenti col mouse VERO nella
# finestra di prova (si ferma da solo se il mouse viene mosso da fuori); senza, nessun input reale.
# Riquadro di spostamento spento e riacceso da Visualizza > Mostra (UI Automation) nelle schede della finestra di
# prova: Esplora file salva l'impostazione per le schede nuove, e la prova la rimette com'era alla fine.
# Clic destro: menu nuovo di Windows 11 (comando di prova "navbar context") confrontato con quello del riquadro di
# sistema per la stessa cartella; voce scelta con UI Automation; menu classico ("navbar classic", "Mostra altre
# opzioni") con il sottomenu "Nuovo" aperto con i messaggi del menu (MN_SELECTITEM/MN_OPENHIERARCHY).
param([switch]$Keep, [switch]$Mouse, [switch]$DefineOnly) # -DefineOnly (con il punto: . .\Test-NavBar.ps1 -DefineOnly): solo le funzioni, per le prove a mano
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'TestKit.ps1')
if (-not ('XpxPin.PinKit' -as [type])) {
    # Le fissate di Esplora file (= cartelle aggiunte ad Accesso rapido) con l'interfaccia che usa Windows
    # (IAutomaticDestinationList, vedi NavItems.cpp): solo indici e confronti, i nomi restano qui dentro.
    Add-Type -TypeDefinition @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
namespace XpxPin {
  [ComImport, Guid("E9C5EF8D-FD41-4F72-BA87-EB03BAD5817C"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IAutomaticDestinationList {
    [PreserveSig] int Initialize([MarshalAs(UnmanagedType.LPWStr)] string appId, [MarshalAs(UnmanagedType.LPWStr)] string lnkPath, [MarshalAs(UnmanagedType.LPWStr)] string unused);
    [PreserveSig] int HasList(out int has);
    [PreserveSig] int GetList(int type, uint maxCount, int flags, [In] ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object list);
    [PreserveSig] int AddUsagePoint([MarshalAs(UnmanagedType.IUnknown)] object item);
    [PreserveSig] int PinItem([MarshalAs(UnmanagedType.IUnknown)] object item, int index);
    [PreserveSig] int IsPinned([MarshalAs(UnmanagedType.IUnknown)] object item, out int index);
  }
  [ComImport, Guid("92CA9DCD-5622-4BBA-A805-5E9F541BD8C9"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IObjectArray {
    [PreserveSig] int GetCount(out uint count);
    [PreserveSig] int GetAt(uint index, [In] ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object item);
  }
  [ComImport, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IShellItem {
    [PreserveSig] int BindToHandler(IntPtr bc, [In] ref Guid bhid, [In] ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object ppv);
    [PreserveSig] int GetParent(out IShellItem parent);
    [PreserveSig] int GetDisplayName(uint sigdn, out IntPtr name);
  }
  public static class PinKit {
    static readonly Guid IidShellItem = new Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE");
    static List<string> s_saved;
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] static extern int SHCreateItemFromParsingName(string path, IntPtr bc, [In] ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object item);
    [DllImport("ole32.dll")] static extern void CoTaskMemFree(IntPtr p);
    static IAutomaticDestinationList Open() {
      var list = (IAutomaticDestinationList)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("F0AE1542-F497-484B-A175-A20DB09144BA")));
      int hr = list.Initialize("Microsoft.Windows.Explorer", null, null);
      if (hr != 0) throw new COMException("Initialize", hr);
      return list;
    }
    static List<string> Pinned() {
      var result = new List<string>(); var list = Open(); Guid iid = new Guid("92CA9DCD-5622-4BBA-A805-5E9F541BD8C9"); object o;
      if (list.GetList(0, 1000, 0, ref iid, out o) == 0 && o != null) {
        var array = (IObjectArray)o; uint count; array.GetCount(out count);
        for (uint i = 0; i < count; i++) {
          Guid iidItem = IidShellItem; object e; string name = null;
          if (array.GetAt(i, ref iidItem, out e) == 0 && e != null) { IntPtr p; if (((IShellItem)e).GetDisplayName(0x80028000, out p) == 0) { name = Marshal.PtrToStringUni(p); CoTaskMemFree(p); } }
          result.Add(name);
        }
      }
      Marshal.ReleaseComObject(list); return result;
    }
    public static int Save() { s_saved = Pinned(); return s_saved.Count; }
    public static bool SameAsSaved() {
      var now = Pinned(); if (s_saved == null || now.Count != s_saved.Count) return false;
      for (int i = 0; i < now.Count; i++) if (!string.Equals(now[i], s_saved[i], StringComparison.OrdinalIgnoreCase)) return false;
      return true;
    }
    public static int Count() { return Pinned().Count; }
    // Posizione fra le fissate letta dall'elenco completo (GetList): IsPinned in un processo che vive a lungo
    // (questo) ha dato indici vecchi dopo cambiamenti fatti da altri processi.
    public static int IndexOf(string path) {
      var all = Pinned();
      return all.FindIndex(s => string.Equals(s, path, StringComparison.OrdinalIgnoreCase));
    }
    // Sposta una cartella già fissata prima dell'elemento all'indice dato (PinItem, come NavItems). Solo per le prove.
    public static int MoveTo(string path, int index) {
      var list = Open(); Guid iid = IidShellItem; object item;
      int hr = SHCreateItemFromParsingName(path, IntPtr.Zero, ref iid, out item);
      if (hr == 0) hr = list.PinItem(item, index);
      Marshal.ReleaseComObject(list); return hr;
    }
  }
}
'@
}
if ($Mouse -and -not ('NavMouse' -as [type])) {
    Add-Type -Namespace '' -Name NavMouse -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
[DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
[DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, System.IntPtr extra);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.IntPtr extra);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern System.IntPtr WindowFromPoint(POINT p);
[DllImport("user32.dll")] public static extern System.IntPtr GetAncestor(System.IntPtr h, uint flags);
[DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
'@
}
if (-not ('NavKit' -as [type])) {
    Add-Type -TypeDefinition @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class NavKit {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string c, IntPtr t);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h, string name);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowRgnBox(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr m);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetMenuString(IntPtr m, uint id, StringBuilder s, int n, uint flags);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, UIntPtr size, uint type, uint prot);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr p, IntPtr a, UIntPtr size, uint type);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] buf, UIntPtr n, out UIntPtr w);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] buf, UIntPtr n, out UIntPtr r);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  public static List<IntPtr> Menus(uint pid) {
    var l = new List<IntPtr>();
    EnumWindows((h, p) => { uint q; GetWindowThreadProcessId(h, out q); var sb = new StringBuilder(64); GetClassName(h, sb, 64);
      if (q == pid && sb.ToString() == "#32768" && IsWindowVisible(h)) l.Add(h); return true; }, IntPtr.Zero);
    return l;
  }
  // Voci di primo livello del riquadro di spostamento nativo (figli della radice nascosta), lette dal processo di prova.
  public static List<string> NativeNav(IntPtr tv) {
    var names = new List<string>(); uint pid; GetWindowThreadProcessId(tv, out pid);
    IntPtr p = OpenProcess(0x0038, false, pid); IntPtr mem = VirtualAllocEx(p, IntPtr.Zero, (UIntPtr)4096, 0x3000, 0x04);
    IntPtr root = SendMessage(tv, 0x110A, IntPtr.Zero, IntPtr.Zero);
    IntPtr item = SendMessage(tv, 0x110A, (IntPtr)4, root);
    while (item != IntPtr.Zero) {
      byte[] tvi = new byte[80];
      BitConverter.GetBytes((uint)0x0001).CopyTo(tvi, 0); BitConverter.GetBytes(item.ToInt64()).CopyTo(tvi, 8);
      BitConverter.GetBytes(mem.ToInt64() + 512).CopyTo(tvi, 24); BitConverter.GetBytes(255).CopyTo(tvi, 32);
      UIntPtr n; WriteProcessMemory(p, mem, tvi, (UIntPtr)80, out n);
      SendMessage(tv, 0x113E, IntPtr.Zero, mem);
      byte[] text = new byte[512]; ReadProcessMemory(p, mem + 512, text, (UIntPtr)512, out n);
      string s = Encoding.Unicode.GetString(text); int z = s.IndexOf('\0'); names.Add(z >= 0 ? s.Substring(0, z) : s);
      item = SendMessage(tv, 0x110A, (IntPtr)1, item);
    }
    VirtualFreeEx(p, mem, UIntPtr.Zero, 0x8000); CloseHandle(p); return names;
  }
  [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h, ref RECT p);
  // Centro (schermo) della voce di primo livello del riquadro con quel nome, dopo averla portata in vista
  // (TVM_ENSUREVISIBLE); Left = -1 se non c'è. Usa solo Left/Top del RECT come punto.
  public static RECT ItemCenter(IntPtr tv, string name) {
    var result = new RECT { Left = -1, Top = -1 }; uint pid; GetWindowThreadProcessId(tv, out pid);
    IntPtr p = OpenProcess(0x0038, false, pid); IntPtr mem = VirtualAllocEx(p, IntPtr.Zero, (UIntPtr)4096, 0x3000, 0x04);
    IntPtr root = SendMessage(tv, 0x110A, IntPtr.Zero, IntPtr.Zero);
    IntPtr item = SendMessage(tv, 0x110A, (IntPtr)4, root);
    while (item != IntPtr.Zero) {
      byte[] tvi = new byte[80]; UIntPtr n;
      BitConverter.GetBytes((uint)0x0001).CopyTo(tvi, 0); BitConverter.GetBytes(item.ToInt64()).CopyTo(tvi, 8);
      BitConverter.GetBytes(mem.ToInt64() + 512).CopyTo(tvi, 24); BitConverter.GetBytes(255).CopyTo(tvi, 32);
      WriteProcessMemory(p, mem, tvi, (UIntPtr)80, out n);
      SendMessage(tv, 0x113E, IntPtr.Zero, mem);
      byte[] text = new byte[512]; ReadProcessMemory(p, mem + 512, text, (UIntPtr)512, out n);
      string s = Encoding.Unicode.GetString(text); int z = s.IndexOf('\0'); if (z >= 0) s = s.Substring(0, z);
      if (s == name) {
        SendMessage(tv, 0x1114, IntPtr.Zero, item);
        byte[] rc = new byte[16]; BitConverter.GetBytes(item.ToInt64()).CopyTo(rc, 0);
        WriteProcessMemory(p, mem, rc, (UIntPtr)16, out n);
        if (SendMessage(tv, 0x1104, (IntPtr)1, mem) != IntPtr.Zero) {
          ReadProcessMemory(p, mem, rc, (UIntPtr)16, out n);
          var pt = new RECT { Left = (BitConverter.ToInt32(rc, 0) + BitConverter.ToInt32(rc, 8)) / 2, Top = (BitConverter.ToInt32(rc, 4) + BitConverter.ToInt32(rc, 12)) / 2 };
          ClientToScreen(tv, ref pt); result = pt;
        }
        break;
      }
      item = SendMessage(tv, 0x110A, (IntPtr)1, item);
    }
    VirtualFreeEx(p, mem, UIntPtr.Zero, 0x8000); CloseHandle(p); return result;
  }
  // Altezza della riga di ogni voce di primo livello (TVM_GETITEMRECT): il riquadro di sistema raddoppia la riga
  // delle voci che hanno un divisore sopra (la linea è nella metà alta).
  public static List<int> NativeRowHeights(IntPtr tv) {
    var heights = new List<int>(); uint pid; GetWindowThreadProcessId(tv, out pid);
    IntPtr p = OpenProcess(0x0038, false, pid); IntPtr mem = VirtualAllocEx(p, IntPtr.Zero, (UIntPtr)64, 0x3000, 0x04);
    IntPtr root = SendMessage(tv, 0x110A, IntPtr.Zero, IntPtr.Zero);
    IntPtr item = SendMessage(tv, 0x110A, (IntPtr)4, root);
    while (item != IntPtr.Zero) {
      byte[] rc = new byte[16]; BitConverter.GetBytes(item.ToInt64()).CopyTo(rc, 0); UIntPtr n;
      WriteProcessMemory(p, mem, rc, (UIntPtr)16, out n);
      int h = -1;
      if (SendMessage(tv, 0x1104, IntPtr.Zero, mem) != IntPtr.Zero) { ReadProcessMemory(p, mem, rc, (UIntPtr)16, out n); h = BitConverter.ToInt32(rc, 12) - BitConverter.ToInt32(rc, 4); }
      heights.Add(h);
      item = SendMessage(tv, 0x110A, (IntPtr)1, item);
    }
    VirtualFreeEx(p, mem, UIntPtr.Zero, 0x8000); CloseHandle(p); return heights;
  }
}
'@
}
$script:fails = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { "  OK   $Name" } else { $script:fails++; "  FAIL $Name $Detail" }
}
$modLog = Join-Path $XpxData 'logs\mod.log'
function LogCount { (Get-Content $modLog -Encoding UTF8).Count }
function ModLines([int]$From) { Get-Content $modLog -Encoding UTF8 | Select-Object -Skip $From }
function Get-Rect([IntPtr]$h) { $r = New-Object NavKit+RECT; [void][NavKit]::GetWindowRect($h, [ref]$r); $r }
function Send-ModCommand([int64]$Main, [string]$Command) {
    [IO.File]::WriteAllText((Join-Path $XpxData 'mod-command.txt'), ('{0:x} {1}' -f $Main, $Command), [Text.Encoding]::ASCII)
    Start-Sleep -Milliseconds 1200
}
# Isole della finestra: intestazione (visibile, senza proprietà), colonna, barra
function Get-Islands([int64]$Main) {
    $out = @{ Header = [IntPtr]::Zero; Panel = [IntPtr]::Zero; Bar = [IntPtr]::Zero }
    $h = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, 'Microsoft.UI.Content.DesktopChildSiteBridge', [IntPtr]::Zero)
    while ($h -ne [IntPtr]::Zero) {
        if ([NavKit]::GetProp($h, 'VTabsExplorer.NavBar') -ne [IntPtr]::Zero) { $out.Bar = $h }
        elseif ([NavKit]::GetProp($h, 'VTabsExplorer.VerticalTabsPanel') -ne [IntPtr]::Zero) { $out.Panel = $h }
        elseif ([XpxKit]::IsWindowVisible($h)) { $out.Header = $h }
        $h = [XpxKit]::FindWindowEx([IntPtr]$Main, $h, 'Microsoft.UI.Content.DesktopChildSiteBridge', [IntPtr]::Zero)
    }
    $out
}
# Contenuto della scheda attiva: riquadro nativo (albero), vista dei file e regione
function Get-TabContent([int64]$Main) {
    $tab = [XpxKit]::FindWindowEx([IntPtr]$Main, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
    $dui = [XpxKit]::FindWindowEx([XpxKit]::FindWindowEx($tab, [IntPtr]::Zero, 'DUIViewWndClassName', [IntPtr]::Zero), [IntPtr]::Zero, 'DirectUIHWND', [IntPtr]::Zero)
    $out = @{ Tab = $tab; Tree = [IntPtr]::Zero; View = [IntPtr]::Zero; Region = -1 }
    $s = [XpxKit]::FindWindowEx($dui, [IntPtr]::Zero, 'CtrlNotifySink', [IntPtr]::Zero)
    while ($s -ne [IntPtr]::Zero) {
        $nsc = [XpxKit]::FindWindowEx($s, [IntPtr]::Zero, 'NamespaceTreeControl', [IntPtr]::Zero)
        if ($nsc -ne [IntPtr]::Zero) { $out.Tree = [XpxKit]::FindWindowEx($nsc, [IntPtr]::Zero, 'SysTreeView32', [IntPtr]::Zero) }
        $view = [XpxKit]::FindWindowEx($s, [IntPtr]::Zero, 'SHELLDLL_DefView', [IntPtr]::Zero)
        if ($view -ne [IntPtr]::Zero -and [XpxKit]::IsWindowVisible($s)) { $out.View = $s }
        $s = [XpxKit]::FindWindowEx($dui, $s, 'CtrlNotifySink', [IntPtr]::Zero)
    }
    $box = New-Object NavKit+RECT
    if ([NavKit]::GetWindowRgnBox($tab, [ref]$box) -gt 1) { $out.Region = $box.Left }
    $out
}
# Testi della barra di stato di Esplora file (StatusBarModuleInner, disegnata da DirectUI in fondo al contenuto), da sinistra
function Get-StatusTexts([int64]$Main) {
    $bar = $XAE::FromHandle([IntPtr]$Main).FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::StatusBar)))
    if (-not $bar) { return @() }
    @($bar.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::Text))) |
            ForEach-Object { $r = $_.Current.BoundingRectangle; [pscustomobject]@{ Name = $_.Current.Name; X = $r.X; Right = $r.X + $r.Width } } | Sort-Object X)
}
# Pulsanti della barra (UI Automation): nomi in ordine, senza "Altre cartelle"
function Get-BarButtons([IntPtr]$Bar) {
    if ($Bar -eq [IntPtr]::Zero) { return @() }
    $root = $XAE::FromHandle($Bar)
    @($root.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::Button))) |
            Where-Object { -not $_.Current.IsOffscreen -and $_.Current.BoundingRectangle.Width -gt 0 } | Sort-Object { $_.Current.BoundingRectangle.X })
}
# Nome invisibile (U+2800, spazi): nel riquadro nativo è una voce come le altre, e così deve restare nella barra.
# Ciclo foreach: "return" dentro ForEach-Object non uscirebbe dalla funzione.
function Test-Blank([string]$Name) {
    foreach ($ch in $Name.ToCharArray()) {
        if (-not ([char]::IsWhiteSpace($ch) -or [int]$ch -eq 0x2800 -or [int]$ch -eq 0x3164)) { return $false }
    }
    return $true
}
# Pulsanti dell'elenco "»", in ordine. È un flyout di WinUI in una finestra a parte
# (Microsoft.UI.Content.PopupWindowSiteBridge), che UI Automation mostra come figlia della finestra di prova.
function Get-OverflowButtons([uint32]$ProcessId, [int64]$Main) {
    $popups = @($XAE::FromHandle([IntPtr]$Main).FindAll($XTS::Children, (New-Object Windows.Automation.PropertyCondition($XAE::ClassNameProperty, 'Microsoft.UI.Content.PopupWindowSiteBridge'))))
    $cond = New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::Button)
    @($popups | ForEach-Object { $_.FindAll($XTS::Descendants, $cond) })
}
function Get-OverflowNames([uint32]$ProcessId, [int64]$Main) { @(Get-OverflowButtons $ProcessId $Main | ForEach-Object { $_.Current.Name }) }
# Tutte le voci, barra ed elenco "»", in ordine
function Get-AllNames([int64]$Main) {
    $buttons = @(Get-BarButtons (Get-Islands $Main).Bar)
    $bar = @($buttons | ForEach-Object { $_.Current.Name } | Where-Object { $_ -ne (Ui 'More folders' 'Altre cartelle') })
    $list = @()
    if (@($buttons | Where-Object { $_.Current.Name -eq (Ui 'More folders' 'Altre cartelle') }).Count -gt 0) {
        Send-ModCommand $Main 'navbar more'
        $list = @(Get-OverflowNames $sepPid $Main)
        Send-ModCommand $Main 'navbar closemenu'
    }
    [pscustomobject]@{ Bar = $bar; List = $list; All = @($bar) + @($list) }
}
# Voci [First, End) delle cartelle aggiunte, dal comando di prova "navbar info" (solo indici)
function Get-PinnedRange([int64]$Main) {
    $from = LogCount
    Send-ModCommand $Main 'navbar info'
    $idx = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:" -and $_ -match 'NavBar: voce \d+: \S+ \(aggiunta\)' } |
            ForEach-Object { [void]($_ -match 'NavBar: voce (\d+):'); [int]$Matches[1] })
    if ($idx.Count -eq 0) { return $null }
    [pscustomobject]@{ First = ($idx | Measure-Object -Minimum).Minimum; End = ($idx | Measure-Object -Maximum).Maximum + 1 }
}
# Dopo uno spostamento: la lettura nuova del processo di prova (pubblicata comunque), poi il giro della barra
function Wait-BarUpdate([int]$From, [int]$TimeoutMs = 6000) {
    $ok = Wait-Until { @(ModLines $From | Where-Object { $_ -match "\[\s*$sepPid\:.*NavItems: \d+ voci" }).Count -gt 0 } $TimeoutMs
    Start-Sleep -Milliseconds 800
    $ok
}
# Aggiunta e rimozione da Accesso rapido come in Esplora file: "Aggiungi ad Accesso rapido" sulla cartella,
# "Rimuovi da Accesso rapido" sul suo elemento DENTRO Accesso rapido (sulla cartella stessa non ha effetto).
$shellApp = New-Object -ComObject Shell.Application
# Niente SHCNE_UPDATEDIR dopo: con Accesso rapido cambiato fa rileggere tutto il riquadro nativo di OGNI
# finestra sul suo thread, e una voce lenta (es. di rete) ha bloccato anche la finestra dell'utente per 20 s.
function Add-Pin([string]$Path) {
    $shellApp.Namespace($Path).Self.InvokeVerb('pintohome')
    Wait-Until { [XpxPin.PinKit]::IndexOf($Path) -ge 0 } 5000
}
function Remove-Pin([string]$Path) {
    # Fino a 3 tentativi, ognuno con un oggetto Shell nuovo: una volta l'elenco di Accesso rapido letto dal
    # vecchio non aveva le cartelle di prova e sono rimaste fissate. La cartella deve esistere.
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        if ([XpxPin.PinKit]::IndexOf($Path) -lt 0) { return $true }
        New-Item -ItemType Directory -Force $Path | Out-Null
        $qa = (New-Object -ComObject Shell.Application).Namespace('shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}')
        $item = @($qa.Items() | Where-Object { $_.Path -eq $Path }) | Select-Object -First 1
        if ($item) { $item.InvokeVerb('unpinfromhome') }
        if (Wait-Until { [XpxPin.PinKit]::IndexOf($Path) -lt 0 } 4000) { return $true }
    }
    $false
}
# Ordine delle cartelle di prova nella barra (+ elenco) e in Windows (indici delle fissate). Il riquadro nativo
# no: dopo un cambiamento di Accesso rapido fatto da fuori non si rilegge (resta com'era finché la finestra vive).
function Get-PinOrder([int64]$Main) {
    $all = @((Get-AllNames $Main).All)
    [pscustomobject]@{
        All     = $all
        Bar     = (@($all | Where-Object { $_ -like 'xpx-pin-*' }) -join ',')
        Windows = (@($script:pinPaths | Sort-Object { [XpxPin.PinKit]::IndexOf($_) } | ForEach-Object { Split-Path $_ -Leaf }) -join ',')
    }
}
function Format-Order($O) { "barra $($O.Bar) | Windows $($O.Windows)" }
function Test-Order($O, [string]$Expected) { $O.Bar -eq $Expected -and $O.Windows -eq $Expected }
# Ordine delle cartelle di prova nella sezione "Accesso rapido" della Home (scheda attiva su Home)
function Get-HomeOrder([int64]$Main) {
    $items = @($XAE::FromHandle([IntPtr]$Main).FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::ListItem))) |
            Where-Object { $_.Current.Name -match 'xpx-pin-\d' })
    @($items | Sort-Object { [int]$_.Current.BoundingRectangle.Y }, { [int]$_.Current.BoundingRectangle.X } | ForEach-Object { [void]($_.Current.Name -match '(xpx-pin-\d)'); $Matches[1] } | Select-Object -Unique) -join ','
}

# --- Mouse vero (-Mouse; come Test-Mouse: movimenti assoluti, stop se il mouse viene mosso da fuori)
# Guardia: l'utente usa il PC durante le prove (sessione remota). Se la finestra di prova non è più in primo piano
# (o sotto il punto c'è un'altra finestra) l'input finirebbe nelle SUE finestre: un Esc nel terminale di Claude
# Code ha interrotto il lavoro. Ogni movimento, pressione ed Esc controlla prima; se no rilascia il tasto e lancia
# un errore "GUARDIA:" (le prove col mouse rimaste si saltano, le altre continuano).
$script:LastCursor = $null
$script:GuardWindow = [IntPtr]::Zero
function Test-OurForeground {
    $fg = [NavMouse]::GetForegroundWindow()
    $fg -ne [IntPtr]::Zero -and ($fg -eq $script:GuardWindow -or [NavMouse]::GetAncestor($fg, 3) -eq $script:GuardWindow) # GA_ROOTOWNER: anche i popup
}
function Assert-Ours([int]$X, [int]$Y, [string]$What) {
    if ($script:GuardWindow -eq [IntPtr]::Zero) { return }
    $ok = Test-OurForeground
    if ($ok -and $X -ge 0) {
        $p = New-Object NavMouse+POINT; $p.X = $X; $p.Y = $Y
        $h = [NavMouse]::WindowFromPoint($p)
        $ok = $h -ne [IntPtr]::Zero -and [NavMouse]::GetAncestor($h, 3) -eq $script:GuardWindow
    }
    if (-not $ok) {
        [NavMouse]::mouse_event(0x04, 0, 0, 0, [IntPtr]::Zero)
        throw "GUARDIA: finestra di prova non in primo piano o coperta ($What): nessun input mandato."
    }
}
function Get-Cursor { $p = New-Object NavMouse+POINT; [void][NavMouse]::GetCursorPos([ref]$p); $p }
function Move-To([int]$X, [int]$Y, [int]$Tolerance = 6) {
    if ($script:LastCursor) {
        $c = Get-Cursor
        if ([math]::Abs($c.X - $script:LastCursor.X) -gt $Tolerance -or [math]::Abs($c.Y - $script:LastCursor.Y) -gt $Tolerance) {
            [NavMouse]::mouse_event(0x04, 0, 0, 0, [IntPtr]::Zero)
            throw ("GUARDIA: mouse mosso dall'esterno (atteso {0},{1}, trovato {2},{3})." -f $script:LastCursor.X, $script:LastCursor.Y, $c.X, $c.Y)
        }
    }
    Assert-Ours -1 -1 'movimento'
    $vx = [NavMouse]::GetSystemMetrics(76); $vy = [NavMouse]::GetSystemMetrics(77)
    $vw = [NavMouse]::GetSystemMetrics(78); $vh = [NavMouse]::GetSystemMetrics(79)
    $nx = [int][math]::Round(($X - $vx) * 65535.0 / ($vw - 1)); $ny = [int][math]::Round(($Y - $vy) * 65535.0 / ($vh - 1))
    [NavMouse]::mouse_event(0x0001 -bor 0x8000 -bor 0x4000, $nx, $ny, 0, [IntPtr]::Zero)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    do { Start-Sleep -Milliseconds 2; $c = Get-Cursor } while (($c.X -ne $X -or $c.Y -ne $Y) -and $sw.ElapsedMilliseconds -lt 400)
    $script:LastCursor = $c
}
function Mouse-Down { $c = Get-Cursor; Assert-Ours $c.X $c.Y 'pressione'; [NavMouse]::mouse_event(0x02, 0, 0, 0, [IntPtr]::Zero) }
function Mouse-Up { [NavMouse]::mouse_event(0x04, 0, 0, 0, [IntPtr]::Zero) }
function Glide([int]$X1, [int]$Y1, [int]$X2, [int]$Y2, [int]$Steps = 20) {
    for ($i = 1; $i -le $Steps; $i++) { Move-To ([int]($X1 + ($X2 - $X1) * $i / $Steps)) ([int]($Y1 + ($Y2 - $Y1) * $i / $Steps)); Start-Sleep -Milliseconds 15 }
}
function Hold([int]$X, [int]$Y, [int]$Ms) { for ($t = 0; $t -lt $Ms; $t += 100) { Start-Sleep -Milliseconds 100; Move-To $X $Y } }
function Press-Escape { Assert-Ours -1 -1 'Esc'; [NavMouse]::keybd_event(0x1B, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 40; [NavMouse]::keybd_event(0x1B, 0, 2, [IntPtr]::Zero) }
function Center($Element) { $r = $Element.Current.BoundingRectangle; @([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) }
function Find-BarButton([int64]$Main, [string]$Name) { Get-BarButtons (Get-Islands $Main).Bar | Where-Object { $_.Current.Name -eq $Name } | Select-Object -First 1 }
# Cartelle delle schede della finestra (dalla shell) e loro numero
function Get-ActivePath([int64]$Main) {
    $tabs = @(Get-ShellTabs | Where-Object { $_.Hwnd -eq $Main })
    [pscustomobject]@{ Paths = @($tabs | ForEach-Object { $_.Path }); Count = $tabs.Count }
}

# --- Visualizza > Mostra > Riquadro di spostamento nella finestra di prova (UI Automation: nessun input reale).
# Esplora file lo applica alla sola scheda attiva e lo salva nel registro per le schede nuove (di tutte le finestre
# aperte dopo): la prova legge il valore all'inizio e lo rimette com'era alla fine.
$sizerKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Modules\GlobalSettings\Sizer'
function Get-SizerBytes { (Get-ItemProperty $sizerKey -Name PageSpaceControlSizer -ErrorAction SilentlyContinue).PageSpaceControlSizer }
function Find-MenuItem([int64]$Main, [string]$Name) {
    $c = New-Object Windows.Automation.AndCondition((New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, $Name)),
        (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::MenuItem)))
    $e = $XAE::FromHandle([IntPtr]$Main).FindFirst($XTS::Descendants, $c)
    if ($e) { return $e }
    # Il menu può essere una finestra a sé del processo di prova
    foreach ($t in $XAE::RootElement.FindAll($XTS::Children, (New-Object Windows.Automation.PropertyCondition($XAE::ProcessIdProperty, [int]$sepPid)))) {
        $e = $t.FindFirst($XTS::Descendants, $c)
        if ($e) { return $e }
    }
    $null
}
function Test-BarVisible([int64]$Main) { $b = (Get-Islands $Main).Bar; $b -ne [IntPtr]::Zero -and [XpxKit]::IsWindowVisible($b) }
# Millisecondi finché la barra è (o non è) visibile; -1 se non succede entro il tempo
function Wait-BarVisible([int64]$Main, [bool]$Visible, [int]$TimeoutMs = 3000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        if ((Test-BarVisible $Main) -eq $Visible) { return $sw.ElapsedMilliseconds }
        Start-Sleep -Milliseconds 5
    }
    if ((Test-BarVisible $Main) -eq $Visible) { $sw.ElapsedMilliseconds } else { -1 }
}
# Cambia il riquadro della scheda attiva: stato di prima ('On'/'Off'; $null se il menu non si trova) e ms finché
# la barra lo segue (-1 se non lo segue)
function Switch-NavPane([int64]$Main) {
    $out = [pscustomobject]@{ Before = $null; Ms = -1 }
    $view = $XAE::FromHandle([IntPtr](Get-HeaderIsland $Main)).FindFirst($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::NameProperty, 'Visualizza')))
    $menu = $null
    if (-not $view -or -not $view.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$menu)) { return $out }
    try {
        $menu.Expand()
        if (-not (Wait-Until { $script:showItem = Find-MenuItem $Main 'Mostra'; $null -ne $script:showItem } 3000)) { return $out }
        $script:showItem.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
        if (-not (Wait-Until { $script:paneItem = Find-MenuItem $Main 'Riquadro di spostamento'; $null -ne $script:paneItem } 3000)) { return $out }
        $toggle = $script:paneItem.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
        $out.Before = "$($toggle.Current.ToggleState)"
        $toggle.Toggle()
        $out.Ms = Wait-BarVisible $Main ($out.Before -eq 'Off')
    }
    finally {
        try { $menu.Collapse() } catch { }
        Start-Sleep -Milliseconds 400
    }
    $out
}
# --- Menu contestuale nuovo (XAML): voci (MenuItem) dei menu aperti nel processo di prova, con nome e AutomationId
# (il verbo, o un numero per le estensioni). Menu classico (#32768): voci lette dalla sua HMENU.
function Get-MenuItems([int64]$Main) {
    $roots = @($XAE::FromHandle([IntPtr]$Main)) + @($XAE::RootElement.FindAll($XTS::Children, (New-Object Windows.Automation.PropertyCondition($XAE::ProcessIdProperty, [int]$sepPid))))
    $seen = @{}
    foreach ($r in $roots) {
        foreach ($menu in $r.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::Menu)))) {
            foreach ($e in $menu.FindAll($XTS::Descendants, (New-Object Windows.Automation.PropertyCondition($XAE::ControlTypeProperty, [Windows.Automation.ControlType]::MenuItem)))) {
                $key = "$($e.Current.AutomationId)|$($e.Current.Name)"
                if (-not $seen.ContainsKey($key)) { $seen[$key] = 1; [pscustomobject]@{ Name = $e.Current.Name; Id = $e.Current.AutomationId; Element = $e } }
            }
        }
    }
}
function Find-MenuItemById([int64]$Main, [string]$Id) { Get-MenuItems $Main | Where-Object { $_.Id -eq $Id } | Select-Object -First 1 -ExpandProperty Element }
# Nome di una scheda da stampare: quelli di prova e di sistema così come sono, gli altri no
function Short-TabName([string]$Name) { if ($Name -in 'A', 'B', 'C', 'D', 'Home', 'Questo PC', 'Galleria', 'This PC', 'Gallery' -or $Name -like 'xpx-pin-*') { $Name } else { '<altra>' } }
function Read-ClassicMenu([IntPtr]$MenuWindow) {
    $hmenu = [NavKit]::SendMessage($MenuWindow, 0x01E1, [IntPtr]::Zero, [IntPtr]::Zero)
    $n = [NavKit]::GetMenuItemCount($hmenu)
    @(for ($k = 0; $k -lt $n; $k++) { $sb = New-Object Text.StringBuilder 128; [void][NavKit]::GetMenuString($hmenu, $k, $sb, 128, 0x400); $sb.ToString() -replace '&', '' })
}
# Righe del log della prova sul riquadro (acceso/spento) dalla riga From, senza il prefisso
function Get-NavPaneLines([int]$From) {
    @(ModLines $From | Where-Object { $_ -match "\[\s*$sepPid\:.*riquadro di spostamento (spento|acceso) nella scheda attiva" } | ForEach-Object { $_ -replace '^.*WindowLayout: riquadro di spostamento ', '' })
}

if ($DefineOnly) { return }
$trayWnd = [NavKit]::FindWindow('Shell_TrayWnd', [IntPtr]::Zero)
$shellPid = 0; [void][NavKit]::GetWindowThreadProcessId($trayWnd, [ref]$shellPid)
$settingsFile = Join-Path $XpxData 'settings.ini'
# Prima del try: con un'altra prova aperta (dev-filter.txt) la pulizia del finally rimetterebbe le SUE impostazioni.
if (Test-Path $XpxDevFilter) { throw 'dev-filter.txt già presente: prima chiudere l''altra prova' }
$settingsBefore = [IO.File]::ReadAllBytes($settingsFile)
$userWindows = Get-UserWindowCount
$userTabs = Get-UserTabCount
$T = $XpxTestRoot
$sepPid = 0
$script:pinPaths = @()
$pinsSaved = $false
$sizerSaved = $false
$sizerBefore = $null
try {
    Enter-VtIsolation # le finestre dell'utente restano come sono per tutta la prova
    Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    # Nessun gruppo (file dei gruppi del processo di prova, vedi Test-NavGroups.ps1): le voci sono quelle del riquadro
    Remove-Item -LiteralPath (Join-Path $XpxData 'navbar-groups-test.json') -ErrorAction SilentlyContinue
    'A', 'B', 'C' | ForEach-Object { New-Item -ItemType Directory -Force (Join-Path $T $_) | Out-Null }
    Stop-Tray
    Remove-Item -LiteralPath $XpxDevSession, $XpxDevArm -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($XpxDevFilter, $T, (New-Object Text.UTF8Encoding $false))
    Set-XpxOption 'Session' 'RestoreOnOpen' '0'
    Set-XpxOption 'VerticalTabs' 'SkipShellProcess' '1' # prima di Enabled: la tray non entra mai nel processo principale
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Set-XpxOption 'NavBar' 'Enabled' '1'
    Start-Tray
    Wait-Until { (Get-XpxLog 3) -match 'Modalità prova' } 6000 | Out-Null

    $logStart = LogCount
    Start-Process explorer.exe -ArgumentList "/separate,`"$(Join-Path $T 'A')`""
    Wait-Until { @(Get-TestWindows).Count -ge 1 } 8000 | Out-Null
    $w = @(Get-TestWindows)[0]
    Add-VtTestWindow $w
    [void][NavKit]::GetWindowThreadProcessId([IntPtr]$w, [ref]$sepPid)
    if ($sepPid -eq $shellPid -or $sepPid -eq 0) { throw "la finestra di prova non è in un processo separato ($sepPid): mi fermo" }
    "finestra di prova {0:x} nel processo separato $sepPid" -f $w
    Move-TestWindow $w 80 60 1100 700
    Add-TestTab $w (Join-Path $T 'B')
    Add-TestTab $w (Join-Path $T 'C')
    Start-Sleep -Milliseconds 800
    & $XpxExe --inject-pid $sepPid
    Wait-Until { (ModLines $logStart) -match "\[\s*$sepPid\:.*NavBar: \d+ voci" } 20000 | Out-Null
    Start-Sleep -Milliseconds 1500
    $scale = [NavKit]::GetDpiForWindow([IntPtr]$w) / 96.0

    "--- controlli"
    $isl = Get-Islands $w
    $hr = Get-Rect $isl.Header; $br = Get-Rect $isl.Bar; $pr = Get-Rect $isl.Panel
    Check 'barra sotto la barra dei comandi, larga quanto l''intestazione' ($isl.Bar -ne [IntPtr]::Zero -and $br.Top -eq $hr.Bottom -and $br.Left -eq $hr.Left -and $br.Right -eq $hr.Right) "intestazione $($hr.Left),$($hr.Top)-$($hr.Right),$($hr.Bottom) barra $($br.Left),$($br.Top)-$($br.Right),$($br.Bottom)"
    Check '  alta 34 DIP' ([math]::Abs(($br.Bottom - $br.Top) - 34 * $scale) -le 1) "$($br.Bottom - $br.Top) px"
    Check 'colonna delle schede in cima alla finestra, intestazione (riga del titolo nascosta) e barra dal suo bordo' ($pr.Top -gt $hr.Top -and $pr.Top -lt $hr.Bottom -and $hr.Left -eq $pr.Right -and $br.Left -eq $pr.Right) "colonna $($pr.Left),$($pr.Top)-$($pr.Right),$($pr.Bottom), intestazione da y=$($hr.Top)"
    $c = Get-TabContent $w
    $vr = Get-Rect $c.View; $tr = Get-Rect $c.Tree
    Check 'vista dei file a destra della colonna, sotto la barra' ($vr.Left -eq $pr.Right -and $vr.Top -eq $br.Bottom) "vista $($vr.Left),$($vr.Top) colonna fino a x=$($pr.Right)"
    Check 'riquadro di spostamento nativo nascosto (fuori a sinistra del contenuto, che non ha regione)' ($c.Region -lt 0 -and $tr.Right -le $pr.Right) "regione $($c.Region), albero fino a x=$($tr.Right), colonna fino a x=$($pr.Right)"
    # Barra di stato ("11 elementi | 1 elemento selezionato"): DirectUI la disegna in fondo a tutto il contenuto, dal bordo
    # sinistro; prima, con il contenuto spostato a sinistra della larghezza del riquadro, il testo finiva sotto la colonna
    $status = @(Get-StatusTexts $w)
    $wr = Get-Rect ([IntPtr]$w)
    Check 'barra di stato: il testo ("... elementi") si vede, dal bordo della colonna' ($status.Count -gt 0 -and $status[0].X -ge $pr.Right -and $status[0].Right -le $wr.Right -and $status[0].Name -match 'element') "$(($status | ForEach-Object { '"{0}" x={1}' -f $_.Name, [int]$_.X }) -join ', '); colonna fino a x=$($pr.Right)"

    # Voci: barra + menu "»" = riquadro nativo, tutte e nello stesso ordine (gli indici dei comandi di prova sono questi)
    $nativeAll = @([NavKit]::NativeNav($c.Tree))
    $buttons = @(Get-BarButtons $isl.Bar)
    $inBar = @($buttons | ForEach-Object { $_.Current.Name } | Where-Object { $_ -ne (Ui 'More folders' 'Altre cartelle') })
    $hasMore = @($buttons | Where-Object { $_.Current.Name -eq (Ui 'More folders' 'Altre cartelle') }).Count -gt 0
    Send-ModCommand $w 'navbar more'
    $menu = @(Get-OverflowNames $sepPid $w)
    Send-ModCommand $w 'navbar closemenu'
    # Tutte le voci così come sono nel riquadro, comprese le cartelle con il nome invisibile
    $all = @($inBar) + @($menu)
    $same = ($all.Count -eq $nativeAll.Count) -and (-not (Compare-Object $all $nativeAll -SyncWindow 0))
    Check 'voci della barra e del menu » = riquadro di spostamento nativo, tutte e in ordine' $same "barra $($inBar.Count) + menu $($menu.Count) = $($all.Count), riquadro $($nativeAll.Count)"
    $blankBar = @($all | Where-Object { Test-Blank $_ }).Count
    $blankNative = @($nativeAll | Where-Object { Test-Blank $_ }).Count
    Check '  cartelle con il nome invisibile riportate come voci, come nel riquadro' ($blankBar -eq $blankNative) "barra $blankBar, riquadro $blankNative"
    Check '  » presente perché non ci stanno tutte' ($hasMore -and $menu.Count -gt 0) "nella barra $($inBar.Count)"
    # Divisori come nel riquadro di sistema: le voci con la riga doppia sono quelle con la linea sopra
    $heights = @([NavKit]::NativeRowHeights($c.Tree))
    $minHeight = ($heights | Where-Object { $_ -gt 0 } | Measure-Object -Minimum).Minimum
    $nativeDividers = (@(for ($k = 1; $k -lt $heights.Count; $k++) { if ($heights[$k] -gt $minHeight * 1.5) { $k } })) -join ','
    $from = LogCount
    Send-ModCommand $w 'navbar info'
    $infoLine = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*divisori prima delle voci: " }) | Select-Object -Last 1
    $barDividers = if ("$infoLine" -match 'divisori prima delle voci: (.+)\.\s*$') { $Matches[1] -replace ' ', '' } else { '?' }
    Check 'divisori come nel riquadro di sistema (prima e dopo le cartelle aggiunte)' ($nativeDividers -and $barDividers -eq $nativeDividers) "barra $barDividers, riquadro $nativeDividers"

    # Clic su un pulsante (UI Automation: come un clic del mouse) e voci aperte con i comandi di prova
    $gallery = $buttons | Where-Object { $_.Current.Name -eq (Ui 'Gallery' 'Galleria') } | Select-Object -First 1
    $before = (Get-ActivePath $w).Paths
    if ($gallery) { $gallery.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
    $ok = Wait-Until { (Get-ActivePath $w).Paths -contains '::{E88865EA-0E1C-4E20-9AA6-EDCD0212C87C}' } 6000
    Check 'clic su "Galleria": la scheda attiva va lì' ($gallery -and $ok -and (Get-ActivePath $w).Count -eq 3) (((Get-ActivePath $w).Paths | ForEach-Object { if ($_ -like '::*') { $_ } else { Short $_ } }) -join ' | ')
    $pc = [array]::IndexOf($nativeAll, (Ui 'This PC' 'Questo PC'))
    Send-ModCommand $w "navbar open $pc"
    $ok = Wait-Until { (Get-ActivePath $w).Paths -contains '::{20D04FE0-3AEA-1069-A2D8-08002B30309D}' } 6000
    Check '"Questo PC" (dal menu »): si apre nella scheda attiva' ($pc -ge 0 -and $ok -and (Get-ActivePath $w).Count -eq 3) "voce $pc"
    $homeIndex = [array]::IndexOf($nativeAll, 'Home')
    Send-ModCommand $w "navbar newtab $homeIndex"
    $ok = Wait-Until { (Get-ActivePath $w).Count -eq 4 -and ((Get-ActivePath $w).Paths -contains '::{F874310E-B6B7-47DC-BC84-B9E6B38F5903}') } 8000
    Check 'clic centrale su "Home": scheda nuova nella stessa finestra' $ok "$((Get-ActivePath $w).Count) schede"

    # Menu classico della shell (quello di Maiusc+clic destro e di "Mostra altre opzioni") di Home: voci, poi Esc
    [IO.File]::WriteAllText((Join-Path $XpxData 'mod-command.txt'), ('{0:x} navbar classic {1}' -f $w, $homeIndex), [Text.Encoding]::ASCII)
    $menus = $null
    $open = Wait-Until { $script:menus = [NavKit]::Menus([uint32]$sepPid); $script:menus.Count -gt 0 } 5000
    $items = @()
    foreach ($m in $script:menus) {
        $hmenu = [NavKit]::SendMessage($m, 0x01E1, [IntPtr]::Zero, [IntPtr]::Zero)
        $n = [NavKit]::GetMenuItemCount($hmenu)
        $items += for ($i = 0; $i -lt $n; $i++) { $sb = New-Object Text.StringBuilder 128; [void][NavKit]::GetMenuString($hmenu, $i, $sb, 128, 0x400); $sb.ToString() -replace '&', '' }
        [void][NavKit]::PostMessage($m, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero)
    }
    Check 'menu classico della voce (Apri, Apri in una nuova scheda...)' ($open -and ($items -contains 'Apri in una nuova scheda') -and @($items | Where-Object { $_ }).Count -ge 3) (($items | Where-Object { $_ }) -join ' | ')
    Check '  si chiude con Esc' (Wait-Until { [NavKit]::Menus([uint32]$sepPid).Count -eq 0 } 3000)

    # --- Riordino delle cartelle aggiunte: 3 cartelle di prova aggiunte in fondo ad Accesso rapido, tolte alla fine
    $script:pinPaths = @('xpx-pin-1', 'xpx-pin-2', 'xpx-pin-3' | ForEach-Object { Join-Path $T "pins\$_" })
    foreach ($p in $script:pinPaths) { New-Item -ItemType Directory -Force $p | Out-Null }
    $savedCount = [XpxPin.PinKit]::Save()
    $pinsSaved = $true
    $from = LogCount
    $added = @($script:pinPaths | ForEach-Object { Add-Pin $_ })
    $idx = @($script:pinPaths | ForEach-Object { [XpxPin.PinKit]::IndexOf($_) })
    $arrived = Wait-Until { @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*NavBar: $($nativeAll.Count + 3) voci" }).Count -gt 0 } 8000
    Start-Sleep -Milliseconds 500
    # In fondo al GRUPPO delle cartelle aggiunte, in ordine (come le mostra il riquadro). La posizione fra tutte le
    # fissate di Windows può cadere prima di file o cartelle virtuali che il riquadro non mostra: non si controlla.
    $range = Get-PinnedRange $w
    $all = @((Get-AllNames $w).All)
    $tail = if ($range) { ($all[($range.End - 3)..($range.End - 1)]) -join ',' } else { '?' }
    Check 'riordino: tre cartelle di prova aggiunte in fondo alle cartelle aggiunte, in ordine' ($arrived -and @($added | Where-Object { -not $_ }).Count -eq 0 -and $tail -eq 'xpx-pin-1,xpx-pin-2,xpx-pin-3') "in fondo al gruppo: $tail; posizioni fra le fissate di Windows $($idx -join ',') (prima $savedCount)"
    # In testa alle cartelle aggiunte, con il comando di prova (lo stesso spostamento di un trascinamento)
    for ($k = 0; $k -lt 3 -and $range; $k++) {
        $all = @((Get-AllNames $w).All)
        $from = LogCount
        Send-ModCommand $w "navbar move $([array]::IndexOf($all, "xpx-pin-$($k + 1)")) $($range.First + $k)"
        Wait-BarUpdate $from | Out-Null
    }
    $o = Get-PinOrder $w
    $idx = @($script:pinPaths | ForEach-Object { [XpxPin.PinKit]::IndexOf($_) })
    Check '  in testa con "navbar move": stesso ordine nella barra e in Windows' ($arrived -and (Test-Order $o 'xpx-pin-1,xpx-pin-2,xpx-pin-3') -and ($idx -join ',') -eq '0,1,2') "$(Format-Order $o); indici $($idx -join ',')"
    $from = LogCount
    Send-ModCommand $w "navbar move $([array]::IndexOf($o.All, 'xpx-pin-3')) $([array]::IndexOf($o.All, 'xpx-pin-1'))"
    Wait-BarUpdate $from | Out-Null
    $o = Get-PinOrder $w
    Check '  la terza prima della prima: 3,1,2 nella barra e in Windows' (Test-Order $o 'xpx-pin-3,xpx-pin-1,xpx-pin-2') (Format-Order $o)
    $moveLines = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*cartella aggiunta spostata" })
    Check '  spostamento in Windows riuscito (PinItem)' ($moveLines.Count -eq 1 -and $moveLines[0] -match ': 0x00000000,') (($moveLines | ForEach-Object { $_ -replace '^.*NavItems: ', '' }) -join ' / ')
    # La Home (aperta dopo) mostra lo stesso ordine nella sezione Accesso rapido
    Send-ModCommand $w "navbar open $([array]::IndexOf($o.All, (Ui 'This PC' 'Questo PC')))"
    Send-ModCommand $w "navbar open $([array]::IndexOf($o.All, 'Home'))"
    $homeOrder = ''
    Wait-Until { $script:homeOrder = Get-HomeOrder $w; $script:homeOrder -eq 'xpx-pin-3,xpx-pin-1,xpx-pin-2' } 8000 | Out-Null
    Check '  la Home aperta dopo ha lo stesso ordine in Accesso rapido' ($script:homeOrder -eq 'xpx-pin-3,xpx-pin-1,xpx-pin-2') "Home: $($script:homeOrder)"

    # --- Clic destro su una cartella aggiunta: il menu nuovo di Windows 11, come nel riquadro (stesse voci)
    $all = @((Get-AllNames $w).All)
    $pin1 = [array]::IndexOf($all, 'xpx-pin-1')
    $from = LogCount
    Send-ModCommand $w "navbar context $pin1"
    Wait-Until { @(Get-MenuItems $w).Count -gt 0 } 3000 | Out-Null
    Start-Sleep -Milliseconds 500
    $ours = @(Get-MenuItems $w)
    $oursIds = @($ours | ForEach-Object { $_.Id })
    $logged = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*menu nuovo della voce $pin1\." }).Count -eq 1
    Check 'clic destro: il menu nuovo di Windows 11 (Apri in una nuova scheda, Rimuovi da Accesso rapido, Nuovo...)' ($logged -and @('opennewtab', 'unpinfromhome', 'New', 'properties', 'expandtoclassic' | Where-Object { $oursIds -notcontains $_ }).Count -eq 0 -and $oursIds -notcontains 'open' -and $oursIds -notcontains 'link') "$($ours.Count) voci: $($oursIds -join ', ')"
    Send-ModCommand $w 'navbar closemenu'
    Check '  si chiude ("navbar closemenu")' (Wait-Until { @(Get-MenuItems $w).Count -eq 0 } 3000)
    # Una voce scelta dal menu nuovo della barra (la scheda nuova serve anche dopo: il suo riquadro nativo è nato con
    # le cartelle di prova già aggiunte, quello delle schede di prima non le ha)
    $count = (Get-ActivePath $w).Count
    $from = LogCount
    Send-ModCommand $w "navbar context $pin1"
    $script:menuItem = $null
    if (Wait-Until { $script:menuItem = Find-MenuItemById $w 'opennewtab'; $null -ne $script:menuItem } 3000) { $script:menuItem.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
    $ok = Wait-Until { (Get-ActivePath $w).Count -gt $count } 6000
    $invoked = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*comando \d+ \(opennewtab\) del menu della voce $pin1`: 0x00000000" }).Count -eq 1
    Check '  "Apri in una nuova scheda" scelta dal menu: scheda nuova sulla cartella' ($ok -and $invoked -and ((Get-ActivePath $w).Paths -contains $script:pinPaths[0])) "$((Get-ActivePath $w).Count) schede; eseguito: $invoked"
    Start-Sleep -Milliseconds 1500
    # Lo stesso menu del riquadro di sistema (nascosto dalla barra, aperto sulla stessa cartella con WM_CONTEXTMENU), nella
    # scheda nuova (si apre in secondo piano: la si porta davanti), il cui riquadro ha la cartella di prova
    $tabItem = Get-TabItems $w | Where-Object { $_.Current.Name -eq 'xpx-pin-1' } | Select-Object -First 1
    if ($tabItem) { $tabItem.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select(); Wait-Until { (Get-ActiveTabName $w) -eq 'xpx-pin-1' } 3000 | Out-Null; Start-Sleep -Milliseconds 1200 }
    $c = Get-TabContent $w
    $pt = [NavKit]::ItemCenter($c.Tree, 'xpx-pin-1')
    "  striscia: $(@(Get-TabNames $w).Count) schede, attiva '$(Short-TabName (Get-ActiveTabName $w))'; riquadro della scheda attiva con la cartella di prova: $($pt.Left -ge 0)"
    $native = @()
    if ($pt.Left -ge 0) {
        [void][NavKit]::PostMessage($c.Tree, 0x007B, $c.Tree, [IntPtr](($pt.Top -shl 16) -bor ($pt.Left -band 0xFFFF)))
        Wait-Until { @(Get-MenuItems $w).Count -gt 0 } 4000 | Out-Null
        Start-Sleep -Milliseconds 600
        $native = @(Get-MenuItems $w)
    }
    # Verbi uguali, salvo quelli propri dell'albero: "rename" (rinomina nell'albero) ed "expandNSC"/"collapseNSC"
    # (Espandi/Comprimi, per le cartelle con sottocartelle); le estensioni (id numerici, diversi fra i due menu) per nome,
    # salvo "Apri con" di un'estensione installata che a volte non si carica nemmeno fuori da Esplora file.
    $verbsOurs = @($ours | Where-Object { $_.Id -notmatch '^\d+$' } | ForEach-Object { $_.Id })
    $verbsNative = @($native | Where-Object { $_.Id -notmatch '^\d+$' -and $_.Id -notmatch '^(rename|expandNSC|collapseNSC)$' } | ForEach-Object { $_.Id })
    $verbsOnlyNative = @($verbsNative | Where-Object { $verbsOurs -notcontains $_ })
    $verbsOnlyOurs = @($verbsOurs | Where-Object { $verbsNative -notcontains $_ })
    $extOurs = @($ours | Where-Object { $_.Id -match '^\d+$' } | ForEach-Object { $_.Name })
    $extNative = @($native | Where-Object { $_.Id -match '^\d+$' } | ForEach-Object { $_.Name })
    $onlyNative = @($extNative | Where-Object { $extOurs -notcontains $_ })
    $onlyOurs = @($extOurs | Where-Object { $extNative -notcontains $_ })
    Check '  stesse voci del menu del riquadro di sistema (salvo quelle dell''albero: Rinomina, Espandi)' ($native.Count -gt 0 -and $verbsOnlyNative.Count -eq 0 -and $verbsOnlyOurs.Count -eq 0 -and $onlyOurs.Count -eq 0 -and @($onlyNative | Where-Object { $_ -ne 'Apri con' }).Count -eq 0) "barra $($ours.Count), riquadro $($native.Count); verbi solo nel riquadro: $($verbsOnlyNative -join ', '), solo nella barra: $($verbsOnlyOurs -join ', '); estensioni solo nel riquadro: $($onlyNative -join ', '), solo nella barra: $($onlyOurs -join ', '); rename nel riquadro: $(@($native | Where-Object { $_.Id -eq 'rename' }).Count)"
    # Il menu del riquadro si chiude con "Apri in una nuova scheda" (scheda nuova), NON con "Mostra altre opzioni": nel
    # riquadro di sistema, con il menu classico letto da fuori, chiude Esplora file anche senza la mod.
    $count = (Get-ActivePath $w).Count
    $item = Find-MenuItemById $w 'opennewtab'
    if ($item) { $item.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
    Wait-Until { (Get-ActivePath $w).Count -gt $count } 6000 | Out-Null
    Start-Sleep -Milliseconds 1200
    # "Mostra altre opzioni": il menu classico, con le voci della cartella e del suo sfondo (Nuovo), senza "Crea collegamento"
    Send-ModCommand $w "navbar context $pin1"
    $script:menuItem = $null
    if (Wait-Until { $script:menuItem = Find-MenuItemById $w 'expandtoclassic'; $null -ne $script:menuItem } 3000) { $script:menuItem.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
    $open = Wait-Until { $script:menus = @([NavKit]::Menus([uint32]$sepPid)); $script:menus.Count -gt 0 } 5000
    $items = if ($open) { @(Read-ClassicMenu $script:menus[0]) } else { @() }
    foreach ($m in [NavKit]::Menus([uint32]$sepPid)) { [void][NavKit]::PostMessage($m, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) }
    Check '  "Mostra altre opzioni": il menu classico (con Nuovo, senza Crea collegamento)' ($open -and $items -contains 'Nuovo' -and $items -contains 'Apri in una nuova scheda' -and $items -notcontains 'Crea collegamento') "$($items.Count) voci"
    Wait-Until { [NavKit]::Menus([uint32]$sepPid).Count -eq 0 } 3000 | Out-Null
    # Menu classico, "Nuovo" > "Cartella": sottomenu e comando del menu "di sfondo", passati dall'unione dei menu
    # (sottomenu aperto con i messaggi del menu, Invio mandato alla sua finestra: nessun input reale)
    $before = @(Get-ChildItem -LiteralPath $script:pinPaths[0] -Directory).Count
    [IO.File]::WriteAllText((Join-Path $XpxData 'mod-command.txt'), ('{0:x} navbar classic {1}' -f $w, $pin1), [Text.Encoding]::ASCII)
    $sub = @()
    if (Wait-Until { $script:menus = @([NavKit]::Menus([uint32]$sepPid)); $script:menus.Count -gt 0 } 5000) {
        $mainMenu = $script:menus[0]
        $pos = [array]::IndexOf(@(Read-ClassicMenu $mainMenu), 'Nuovo')
        if ($pos -ge 0) {
            [void][NavKit]::SendMessage($mainMenu, 0x01E5, [IntPtr]$pos, [IntPtr]::Zero)   # MN_SELECTITEM
            [void][NavKit]::SendMessage($mainMenu, 0x01E3, [IntPtr]::Zero, [IntPtr]::Zero) # MN_OPENHIERARCHY
            if (Wait-Until { $script:menus = @([NavKit]::Menus([uint32]$sepPid)); $script:menus.Count -gt 1 } 4000) {
                $subWindow = @($script:menus | Where-Object { $_ -ne $mainMenu })[0]
                $sub = @(Read-ClassicMenu $subWindow)
                $k = [array]::IndexOf($sub, 'Cartella')
                if ($k -ge 0) {
                    [void][NavKit]::SendMessage($subWindow, 0x01E5, [IntPtr]$k, [IntPtr]::Zero)
                    [void][NavKit]::PostMessage($subWindow, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero) # Invio
                }
            }
        }
    }
    $created = Wait-Until { @(Get-ChildItem -LiteralPath $script:pinPaths[0] -Directory).Count -gt $before } 6000
    foreach ($m in [NavKit]::Menus([uint32]$sepPid)) { [void][NavKit]::PostMessage($m, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) }
    Check '  menu classico, "Nuovo" > "Cartella": cartella creata dentro la cartella aggiunta' ($created -and $sub -contains 'Cartella') "sottomenu: $($sub.Count) voci; creata: $created"
    Wait-Until { [NavKit]::Menus([uint32]$sepPid).Count -eq 0 } 3000 | Out-Null
    Start-Sleep -Milliseconds 800
    # Via le schede aperte dai menu (sulla cartella di prova): la striscia delle schede torna com'era per le prove dopo
    "  striscia prima della pulizia: $((Get-TabNames $w | ForEach-Object { Short-TabName $_ }) -join ', ')"
    for ($k = 0; $k -lt 4 -and @((Get-ActivePath $w).Paths | Where-Object { $_ -eq $script:pinPaths[0] }).Count -gt 0; $k++) {
        $tabItem = Get-TabItems $w | Where-Object { $_.Current.Name -eq 'xpx-pin-1' } | Select-Object -First 1
        if (-not $tabItem) { "  scheda xpx-pin-1 non trovata nella striscia"; break }
        $tabItem.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Start-Sleep -Milliseconds 800
        $tab = [XpxKit]::FindWindowEx([IntPtr]$w, [IntPtr]::Zero, 'ShellTabWindowClass', [IntPtr]::Zero)
        [void][XpxKit]::PostMessage($tab, 0x0111, [IntPtr]0xA021, [IntPtr]::Zero) # Ctrl+W (comando, non tasto)
        Start-Sleep -Milliseconds 1200
    }
    "  schede dopo le prove del menu: $((Get-TabNames $w | ForEach-Object { Short-TabName $_ }) -join ', ')"

    if ($Mouse) {
      "--- mouse vero nella finestra di prova (non toccare il mouse per circa 40 secondi)"
      $script:GuardWindow = [IntPtr]$w
      try {
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w)
        Start-Sleep -Milliseconds 500
        # 1) Nella barra: la prima trascinata oltre la metà della seconda → 3,2,1; nessuna cartella aperta
        $pathsBefore = (Get-ActivePath $w).Paths -join '|'
        $b1 = Find-BarButton $w 'xpx-pin-1'; $b2 = Find-BarButton $w 'xpx-pin-2'
        $from = LogCount
        if ($b1 -and $b2) {
            $s = Center $b1; $r2 = $b2.Current.BoundingRectangle; $tx = [int]($r2.Right - 3)
            Move-To $s[0] $s[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 100
            try { Glide $s[0] $s[1] $tx $s[1]; Hold $tx $s[1] 300 } finally { Mouse-Up }
            Wait-BarUpdate $from | Out-Null
        }
        $o = Get-PinOrder $w
        Check 'mouse: trascinata nella barra dopo la vicina: 3,2,1 ovunque' ($b1 -and $b2 -and (Test-Order $o 'xpx-pin-3,xpx-pin-2,xpx-pin-1')) (Format-Order $o)
        Check '  trascinare non apre la cartella' (((Get-ActivePath $w).Paths -join '|') -eq $pathsBefore)
        # 2) Un clic senza trascinare apre ancora la cartella
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $b3 = Find-BarButton $w 'xpx-pin-3'
        if ($b3) { $c3 = Center $b3; Move-To $c3[0] $c3[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 60; Mouse-Up }
        Check 'mouse: un clic (senza trascinare) apre ancora la cartella' ($b3 -and (Wait-Until { (Get-ActivePath $w).Paths -contains $script:pinPaths[2] } 5000))
        # 3) Esc durante il trascinamento annulla
        Start-Sleep -Milliseconds 600
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $b3 = Find-BarButton $w 'xpx-pin-3'
        $from = LogCount
        if ($b3) {
            $c3 = Center $b3
            Move-To $c3[0] $c3[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 100
            try { Glide $c3[0] $c3[1] ($c3[0] + 150) $c3[1] 10; Start-Sleep -Milliseconds 200; Press-Escape; Start-Sleep -Milliseconds 300 } finally { Mouse-Up }
            Start-Sleep -Milliseconds 1500
        }
        $lines = @(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:" })
        $o = Get-PinOrder $w
        Check 'mouse: Esc durante il trascinamento annulla (ordine invariato)' ($b3 -and (@($lines | Where-Object { $_ -match 'annullato \(Esc\)' }).Count -eq 1) -and -not ($lines -match 'spostata') -and (Test-Order $o 'xpx-pin-3,xpx-pin-2,xpx-pin-1')) (Format-Order $o)
        # 4) Dall'elenco » alla barra: la seconda va nell'elenco (in cima: in fondo sarebbe fuori dalla parte
        #    visibile, l'elenco scorre), poi si trascina prima della terza
        $from = LogCount
        Send-ModCommand $w "navbar move $([array]::IndexOf($o.All, 'xpx-pin-2')) $((Get-AllNames $w).Bar.Count + 3)"
        Wait-BarUpdate $from | Out-Null
        $inList = (Get-AllNames $w).List -contains 'xpx-pin-2'
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w)
        Send-ModCommand $w 'navbar more'
        $lb = Get-OverflowButtons $sepPid $w | Where-Object { $_.Current.Name -eq 'xpx-pin-2' } | Select-Object -First 1
        $b3 = Find-BarButton $w 'xpx-pin-3'
        $from = LogCount
        $closed = $false
        if ($lb -and $b3) {
            $s = Center $lb; $r3 = $b3.Current.BoundingRectangle; $tx = [int]($r3.X + 3); $ty = [int]($r3.Y + $r3.Height / 2)
            Move-To $s[0] $s[1] 12; Start-Sleep -Milliseconds 200; Mouse-Down; Start-Sleep -Milliseconds 100
            try { Glide $s[0] $s[1] $tx $ty 25; Hold $tx $ty 300 } finally { Mouse-Up }
            Wait-BarUpdate $from | Out-Null
            $closed = @(Get-OverflowButtons $sepPid $w).Count -eq 0
        }
        $o = Get-PinOrder $w
        Check 'mouse: dall''elenco » alla barra, prima della vicina: 2,3,1 ovunque' ($inList -and $lb -and (Test-Order $o 'xpx-pin-2,xpx-pin-3,xpx-pin-1')) "nell'elenco prima: $inList; $(Format-Order $o)"
        Check '  l''elenco si chiude dopo averla lasciata nella barra' $closed
        # 5) Dalla barra all'elenco: fermi su » l'elenco si apre; lasciata nella metà bassa della sua prima voce
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w); Start-Sleep -Milliseconds 300
        $b1 = Find-BarButton $w 'xpx-pin-1'; $more = Find-BarButton $w (Ui 'More folders' 'Altre cartelle')
        $from = LogCount
        $firstName = $null
        if ($b1 -and $more) {
            $s = Center $b1; $m = Center $more
            Move-To $s[0] $s[1] 12; Start-Sleep -Milliseconds 150; Mouse-Down; Start-Sleep -Milliseconds 100
            try {
                Glide $s[0] $s[1] $m[0] $m[1] 25
                Hold $m[0] $m[1] 1600
                $first = Get-OverflowButtons $sepPid $w | Select-Object -First 1
                if ($first) {
                    $firstName = $first.Current.Name
                    $r = $first.Current.BoundingRectangle; $tx = [int]($r.X + 40); $ty = [int]($r.Y + $r.Height * 0.75)
                    Glide $m[0] $m[1] $tx $ty 15; Hold $tx $ty 300
                }
            }
            finally { Mouse-Up }
            Wait-BarUpdate $from | Out-Null
        }
        [void][NavMouse]::SetForegroundWindow([IntPtr]$w)
        $o = Get-PinOrder $w
        $p1 = [array]::IndexOf($o.All, 'xpx-pin-1')
        Check 'mouse: fermi su » l''elenco si apre e la voce si lascia lì dentro (dopo la prima dell''elenco)' ($null -ne $firstName -and $p1 -gt 0 -and $o.All[$p1 - 1] -eq $firstName -and (Test-Order $o 'xpx-pin-2,xpx-pin-3,xpx-pin-1')) "elenco aperto: $($null -ne $firstName); $(Format-Order $o)"
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

    # --- Riquadro di spostamento spento da Visualizza > Mostra: la barra sparisce, come il riquadro. Esplora file lo
    #     applica scheda per scheda (e lo salva per le schede nuove): la barra segue la scheda attiva.
    #     Schede A e B: le uniche che hanno ancora il loro nome (le altre sono andate su Galleria, Home...).
    #     Finestra più larga: con la colonna in cima l'intestazione è più stretta della finestra e "Visualizza"
    #     finirebbe nel menu "Altre opzioni" della barra dei comandi.
    Move-TestWindow $w 40 40 ([math]::Min(1500, (Get-ScreenWidth) - 80)) 700
    Start-Sleep -Milliseconds 1200
    $sizerBefore = Get-SizerBytes
    $sizerSaved = $true
    New-Item -ItemType Directory -Force (Join-Path $T 'D') | Out-Null
    Select-TabByName $w 'B'
    Start-Sleep -Milliseconds 1200
    $from = LogCount
    $sw = Switch-NavPane $w
    Start-Sleep -Milliseconds 400
    $isl = Get-Islands $w; $c = Get-TabContent $w
    $hr = Get-Rect $isl.Header; $pr = Get-Rect $isl.Panel; $vr = Get-Rect $c.View
    $lines = @(Get-NavPaneLines $from)
    Check 'riquadro spento da Visualizza > Mostra: la barra sparisce subito' ($sw.Before -eq 'On' -and $sw.Ms -ge 0 -and $lines.Count -eq 1 -and $lines[0] -match '^spento.*\(subito\)') "menu: $($sw.Before), nascosta dopo $($sw.Ms) ms; log: $($lines -join ' / ')"
    Check '  vista dei file subito sotto l''intestazione, dal bordo della colonna (sempre in cima), niente regione' ($pr.Top -gt $hr.Top -and $vr.Top -eq $hr.Bottom -and $vr.Left -eq $pr.Right -and $c.Region -lt 0 -and -not [XpxKit]::IsWindowVisible($c.Tree)) "colonna da y=$($pr.Top), vista $($vr.Left),$($vr.Top), intestazione fino a y=$($hr.Bottom), regione $($c.Region)"
    $from = LogCount
    Send-ModCommand $w 'navbar info'
    $info = "$(@(ModLines $from | Where-Object { $_ -match "\[\s*$sepPid\:.*riquadro di spostamento della scheda attiva" }) | Select-Object -Last 1)"
    Check '  "navbar info": spento, barra nascosta' ($info -match 'spento \(barra nascosta\)') ($info -replace '^.*NavBar: ', '')
    # Le altre schede hanno ancora il riquadro acceso (come in Esplora file): con loro la barra torna
    $from = LogCount
    Select-TabByName $w 'A'
    $ms = Wait-BarVisible $w $true
    Start-Sleep -Milliseconds 700
    $isl = Get-Islands $w; $c = Get-TabContent $w
    $lines = @(Get-NavPaneLines $from)
    Check 'scheda A (riquadro acceso): la barra torna e il riquadro nativo è di nuovo fuori vista' ($ms -ge 0 -and $c.Region -lt 0 -and (Get-Rect $c.Tree).Right -le (Get-Rect $isl.Panel).Right -and (Get-Rect $c.View).Top -eq (Get-Rect $isl.Bar).Bottom -and $lines.Count -eq 1 -and $lines[0] -match '^acceso.*\(subito\)') "dopo $ms ms, albero fino a x=$((Get-Rect $c.Tree).Right); log: $($lines -join ' / ')"
    $from = LogCount
    Select-TabByName $w 'B'
    $ms = Wait-BarVisible $w $false
    Start-Sleep -Milliseconds 700
    $lines = @(Get-NavPaneLines $from)
    Check '  di nuovo B (spento): la barra sparisce' ($ms -ge 0 -and $lines.Count -eq 1 -and $lines[0] -match '^spento.*\(subito\)') "dopo $ms ms; log: $($lines -join ' / ')"
    # Scheda nuova: Esplora file le dà l'impostazione salvata (spento) e la barra non deve comparire nemmeno un attimo
    $from = LogCount
    Add-TestTab $w (Join-Path $T 'D')
    Start-Sleep -Milliseconds 1500
    $c = Get-TabContent $w
    $lines = @(Get-NavPaneLines $from)
    Check 'scheda nuova (Esplora file la apre col riquadro spento): la barra non compare' ($c.Tree -ne [IntPtr]::Zero -and -not [XpxKit]::IsWindowVisible($c.Tree) -and -not (Test-BarVisible $w) -and $lines.Count -eq 0) "riquadro nativo visibile: $([XpxKit]::IsWindowVisible($c.Tree)); barra visibile: $(Test-BarVisible $w); log: $($lines -join ' / ')"
    # Riacceso da Visualizza: la barra torna, il riquadro nativo di nuovo nascosto, tutto sotto la barra
    $from = LogCount
    $sw = Switch-NavPane $w
    Start-Sleep -Milliseconds 700
    $isl = Get-Islands $w; $c = Get-TabContent $w
    $br = Get-Rect $isl.Bar; $pr = Get-Rect $isl.Panel; $vr = Get-Rect $c.View; $tr = Get-Rect $c.Tree
    $lines = @(Get-NavPaneLines $from)
    Check 'riacceso da Visualizza: la barra torna subito' ($sw.Before -eq 'Off' -and $sw.Ms -ge 0 -and $lines.Count -eq 1 -and $lines[0] -match '^acceso.*\(subito\)') "menu: $($sw.Before), visibile dopo $($sw.Ms) ms; log: $($lines -join ' / ')"
    Check '  vista dei file sotto la barra, barra dal bordo della colonna, riquadro nativo fuori vista' ($br.Left -eq $pr.Right -and $vr.Top -eq $br.Bottom -and $vr.Left -eq $pr.Right -and $c.Region -lt 0 -and $tr.Right -le $pr.Right) "vista $($vr.Left),$($vr.Top), colonna $($pr.Right),$($pr.Top), barra da x=$($br.Left) fino a y=$($br.Bottom), albero fino a x=$($tr.Right)"
    Select-TabByName $w 'B'
    Wait-BarVisible $w $false | Out-Null
    $sw = Switch-NavPane $w
    Check '  riacceso anche in B: barra di nuovo in tutte le schede' ($sw.Before -eq 'Off' -and $sw.Ms -ge 0) "menu: $($sw.Before), visibile dopo $($sw.Ms) ms"

    # Finestra stretta: meno voci nella barra, le altre nel menu
    Move-TestWindow $w 80 60 760 700
    Start-Sleep -Milliseconds 1500
    $narrow = @(Get-BarButtons (Get-Islands $w).Bar | Where-Object { $_.Current.Name -ne (Ui 'More folders' 'Altre cartelle') }).Count
    Check 'finestra più stretta: meno voci nella barra' ($narrow -gt 0 -and $narrow -lt $inBar.Count) "$narrow invece di $($inBar.Count)"
    Move-TestWindow $w 80 60 1100 700
    Start-Sleep -Milliseconds 1500
    Check '  più larga: tornano' (@(Get-BarButtons (Get-Islands $w).Bar | Where-Object { $_.Current.Name -ne (Ui 'More folders' 'Altre cartelle') }).Count -eq $inBar.Count)

    # Spenta: tutto come prima (riquadro nativo, contenuto sotto l'intestazione)
    $before = LogCount
    Set-XpxOption 'NavBar' 'Enabled' '0'
    Wait-Until { (ModLines $before) -match 'barra delle cartelle tolta' } 6000 | Out-Null
    Start-Sleep -Milliseconds 1000
    $isl = Get-Islands $w; $c = Get-TabContent $w
    $pr = Get-Rect $isl.Panel; $vr = Get-Rect $c.View; $tr = Get-Rect $c.Tree
    $hr = Get-Rect $isl.Header
    Check 'barra spenta: niente barra, colonna sempre in cima, vista subito sotto l''intestazione' ($isl.Bar -eq [IntPtr]::Zero -and $pr.Top -gt $hr.Top -and $vr.Top -eq $hr.Bottom) "colonna da y=$($pr.Top), vista da y=$($vr.Top)"
    Check '  riquadro di spostamento di nuovo visibile, senza regione' ($c.Region -lt 0 -and $tr.Left -ge $pr.Right -and $vr.Left -gt $tr.Right) "albero da x=$($tr.Left), vista da x=$($vr.Left)"
    $before = LogCount
    Set-XpxOption 'NavBar' 'Enabled' '1'
    Wait-Until { (ModLines $before) -match 'NavBar: \d+ voci' } 8000 | Out-Null
    Start-Sleep -Milliseconds 1000
    $isl = Get-Islands $w; $c = Get-TabContent $w
    Check '  riaccesa: di nuovo la barra e il riquadro nascosto' ($isl.Bar -ne [IntPtr]::Zero -and $c.Region -lt 0 -and (Get-Rect $c.Tree).Right -le (Get-Rect $isl.Panel).Right -and (Get-Rect $c.View).Left -eq (Get-Rect $isl.Panel).Right)
    $status = @(Get-StatusTexts $w)
    Check '  barra di stato di nuovo con il testo in vista' ($status.Count -gt 0 -and $status[0].X -ge (Get-Rect $isl.Panel).Right) "$(($status | ForEach-Object { '"{0}" x={1}' -f $_.Name, [int]$_.X }) -join ', ')"

    # Senza schede verticali: barra a tutta larghezza, vista dei file dal bordo sinistro
    $before = LogCount
    Set-XpxOption 'VerticalTabs' 'Enabled' '0'
    Wait-Until { (ModLines $before) -match 'schede verticali disattivate' } 6000 | Out-Null
    Start-Sleep -Milliseconds 1000
    $isl = Get-Islands $w; $c = Get-TabContent $w
    $hr = Get-Rect $isl.Header; $br = Get-Rect $isl.Bar; $vr = Get-Rect $c.View
    Check 'schede verticali spente: barra sotto l''intestazione, vista dei file dal bordo sinistro' ($br.Top -eq $hr.Bottom -and $vr.Left -eq $hr.Left -and $vr.Top -eq $br.Bottom -and $c.Region -lt 0 -and (Get-Rect $c.Tree).Right -le $hr.Left) "vista $($vr.Left),$($vr.Top) intestazione da x=$($hr.Left), albero fino a x=$((Get-Rect $c.Tree).Right)"
    $status = @(Get-StatusTexts $w)
    Check '  barra di stato con il testo in vista' ($status.Count -gt 0 -and $status[0].X -ge $hr.Left) "$(($status | ForEach-Object { '"{0}" x={1}' -f $_.Name, [int]$_.X }) -join ', ')"
    $before = LogCount
    Set-XpxOption 'VerticalTabs' 'Enabled' '1'
    Wait-Until { (ModLines $before) -match 'schede verticali attive' } 6000 | Out-Null
    Start-Sleep -Milliseconds 1000
    $isl = Get-Islands $w
    $pr = Get-Rect $isl.Panel; $br = Get-Rect $isl.Bar
    Check '  riaccese: colonna in cima, barra dal suo bordo' ($pr.Top -gt (Get-Rect $isl.Header).Top -and $br.Top -eq (Get-Rect $isl.Header).Bottom -and $br.Left -eq $pr.Right) "colonna $($pr.Left),$($pr.Top)-$($pr.Right), barra da x=$($br.Left)"

    "--- log della mod (barra ed errori):"
    ModLines $logStart | Where-Object { $_ -match "\[\s*$sepPid\:" -and $_ -match 'NavBar|NavItems|errore|Errore' -and $_ -notmatch 'comando di prova' } | Select-Object -Last 12 | ForEach-Object { '  ' + ($_ -replace '^2026-\d\d-\d\d ', '') }
}
finally {
    # Cartelle di prova fuori da Accesso rapido (anche se la prova si è fermata a metà); le fissate dell'utente
    # devono essere identiche a quelle lette all'inizio (stessi elementi, stesso ordine).
    if ($script:pinPaths.Count -gt 0) {
        $removed = @($script:pinPaths | ForEach-Object { Remove-Pin $_ })
        $same = $pinsSaved -and [XpxPin.PinKit]::SameAsSaved()
        Check 'cartelle di prova tolte; le fissate dell''utente identiche a prima (elementi e ordine)' (@($removed | Where-Object { -not $_ }).Count -eq 0 -and $same) "tolte $(@($removed | Where-Object { $_ }).Count) su 3, identiche: $same"
    }
    if (-not $Keep) {
        Close-TestWindows
        Start-Sleep -Milliseconds 800
        if ($sepPid -and $sepPid -ne $shellPid) {
            $p = Get-Process -Id $sepPid -ErrorAction SilentlyContinue
            if ($p) { Stop-Process -Id $sepPid -Force -ErrorAction SilentlyContinue }
        }
        Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
    }
    # Impostazione "Riquadro di spostamento" salvata da Esplora file: com'era prima della prova (dopo aver chiuso il
    # processo di prova, che la riscrive cambiando il riquadro)
    if ($sizerSaved) {
        if ((@(Get-SizerBytes) -join ',') -ne (@($sizerBefore) -join ',')) {
            if ($sizerBefore) { Set-ItemProperty $sizerKey -Name PageSpaceControlSizer -Value ([byte[]]$sizerBefore) -Type Binary }
            else { Remove-ItemProperty $sizerKey -Name PageSpaceControlSizer -ErrorAction SilentlyContinue }
        }
        Check 'impostazione "Riquadro di spostamento" di Esplora file come prima della prova' ((@(Get-SizerBytes) -join ',') -eq (@($sizerBefore) -join ','))
    }
    # I riordini passano dai gruppi (navbar-groups-test.json nel processo di prova): via anche questo
    Remove-Item -LiteralPath $XpxDevFilter, (Join-Path $XpxData 'mod-command.txt'), (Join-Path $XpxData 'dump-tree.txt'), (Join-Path $XpxData 'navbar-groups-test.json') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($settingsFile, $settingsBefore)
    Exit-VtIsolation
    Stop-Tray; Start-Tray
    $shellOk = [bool](Get-Process -Id $shellPid -ErrorAction SilentlyContinue)
    "--- processo della barra intatto: $shellOk; errori: $script:fails$(if ($script:mouseSkipped) { '; ATTENZIONE: prove col mouse in parte saltate (finestra di prova non in primo piano)' })"
    "processi explorer: " + ((Get-Process explorer | ForEach-Object { "$($_.Id)" }) -join ', ') + " (barra: $shellPid); finestre dell'utente invariate: " + ((Get-UserWindowCount) -eq $userWindows -and (Get-UserTabCount) -eq $userTabs)
}
