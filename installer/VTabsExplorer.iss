; Installer for Vertical Tabs for File Explorer (VTabsExplorer) - Inno Setup 6
; Build:  "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" installer\VTabsExplorer.iss   (after build.ps1)
; Per-user install: no administrator rights needed.

#define AppName "Vertical Tabs for File Explorer"
#define AppShortName "VTabsExplorer"
#define AppVersion "1.5.0"
#define AppPublisher "LucaSorvillo"
#define AppRepo "https://github.com/LucaSorvillo/vertical-tabs-explorer"
#define AppSite "https://lucasorvillo.github.io/vertical-tabs-explorer/"
#define AppExe "VTabsExplorer.exe"
#define AppMod "VTabsExplorerMod.dll"
#define BinDir "..\build\bin"

[Setup]
AppId={{2BD4CA10-B40C-4D81-8298-3D1A751ADF15}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppSite}
AppSupportURL={#AppRepo}/issues
AppUpdatesURL={#AppRepo}/releases
AppCopyright=Copyright (c) 2026 {#AppPublisher} - MIT License
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription={#AppName} setup
DefaultDirName={localappdata}\Programs\{#AppShortName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 11 22H2 or later (File Explorer tabs)
MinVersion=10.0.22621
OutputDir=..\dist
OutputBaseFilename={#AppShortName}-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no
UninstallDisplayIcon={app}\{#AppExe}
SetupIconFile=..\src\VTabsExplorer.ico
UninstallDisplayName={#AppName}
LicenseFile=..\LICENSE
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[CustomMessages]
english.AutoStart=Start {#AppName} when Windows starts (needed to reopen your folders after a restart)
italian.AutoStart=Avvia {#AppName} all'avvio di Windows (serve a riaprire le cartelle dopo un riavvio)
english.RemoveData=Do you also want to delete the settings and saved data (last session, closed tabs, bar groups, tab colors)?
italian.RemoveData=Vuoi eliminare anche le impostazioni e i dati salvati (ultima sessione, schede chiuse, gruppi della barra, colori delle schede)?
english.TrayNote=The app runs in the notification area (tray): right-click its icon for the options.
italian.TrayNote=L'app resta nell'area di notifica (tray): clic destro sulla sua icona per le opzioni.
english.VerticalTabs=Turn on the vertical tabs (column on the left)
italian.VerticalTabs=Attiva le schede verticali (colonna a sinistra)
english.NavBar=Turn on the folders bar (navigation pane at the top, like Chrome's bookmarks bar)
italian.NavBar=Attiva la barra delle cartelle (riquadro di spostamento in alto, come la barra dei segnalibri di Chrome)
english.Features=Features (you can change them later from the tray menu):
italian.Features=Funzioni (si possono cambiare dopo dal menu della tray):

[Tasks]
Name: "autostart"; Description: "{cm:AutoStart}"
Name: "verticaltabs"; Description: "{cm:VerticalTabs}"; GroupDescription: "{cm:Features}"
Name: "navbar"; Description: "{cm:NavBar}"; GroupDescription: "{cm:Features}"

[Dirs]
Name: "{localappdata}\{#AppShortName}"; Flags: uninsneveruninstall

[INI]
; The same settings the tray menu writes. Off by default in the app itself: the installer asks.
Filename: "{localappdata}\{#AppShortName}\settings.ini"; Section: "VerticalTabs"; Key: "Enabled"; String: "1"; Tasks: verticaltabs
Filename: "{localappdata}\{#AppShortName}\settings.ini"; Section: "VerticalTabs"; Key: "Suspended"; String: "0"; Tasks: verticaltabs
Filename: "{localappdata}\{#AppShortName}\settings.ini"; Section: "NavBar"; Key: "Enabled"; String: "1"; Tasks: navbar

[Files]
Source: "{#BinDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\{#AppMod}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "{#AppName}"

[Registry]
; Same value the app writes from its tray menu ("Avvia con Windows")
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#AppShortName}"; ValueData: """{app}\{#AppExe}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--exit"; Flags: runhidden waituntilterminated; RunOnceId: "StopTray"

[Code]
// Before installing over a running copy: ask the tray to exit, so its files can be replaced.
procedure StopTray();
var
  ResultCode: Integer;
  Exe: String;
begin
  Exe := ExpandConstant('{app}\{#AppExe}');
  if FileExists(Exe) then
  begin
    Exec(Exe, '--exit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(1500);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopTray();
  Result := '';
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + CustomMessage('TrayNote');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Data: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    Data := ExpandConstant('{localappdata}\{#AppShortName}');
    if DirExists(Data) and (not UninstallSilent) then
      if MsgBox(CustomMessage('RemoveData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(Data, True, True, True);
  end;
end;
