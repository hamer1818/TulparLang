; TulparLang Windows installer (Inno Setup 6 script)
;
; Build with: iscc.exe /DAppVersion=2.1.0.42 /DSourceBinary=path\to\tulpar.exe installer\tulpar.iss
;
; CI sets both /D values; running locally without them falls back to a
; "0.0.0-dev" stamp and the build-windows/tulpar.exe path so a developer
; can `iscc installer\tulpar.iss` after `cmake --build`.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif

#ifndef SourceBinary
  #define SourceBinary "..\build-windows\tulpar.exe"
#endif

#ifndef SourceRuntimeLib
  #define SourceRuntimeLib "..\build-windows\libtulpar_runtime.a"
#endif

#define AppName        "TulparLang"
#define AppPublisher   "TulparLang Project"
#define AppURL         "https://tulparlang.dev"
#define AppExeName     "tulpar.exe"
#define UninstallID    "{{8B4A6E2C-4A57-4A8E-9F6F-3B0DBC9C7E1A}"

[Setup]
; Per-user install (no admin prompt). Users without admin rights — the
; majority of corporate Windows machines — can install without IT help.
; Going per-machine would require RequireAdmin and DefaultDirName under
; {commonpf64}; we deliberately mirror the behaviour of install.ps1.
AppId={#UninstallID}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={localappdata}\Programs\Tulpar
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=tulpar-setup-windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesEnvironment=yes
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExeName}
VersionInfoVersion=2.1.0.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} compiler installer
VersionInfoProductName={#AppName}

[Languages]
; Turkish first since the project is Turkish-led; English second as
; lingua franca. User picks at install time.
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; Default = checked. Unchecking still installs the binary, just leaves
; PATH untouched (advanced user / portable install scenario).
Name: "addtopath"; Description: "{code:PathTaskDescription}"; GroupDescription: "{code:EnvGroupDescription}"; Flags: checkedonce

[Files]
Source: "{#SourceBinary}";     DestDir: "{app}"; Flags: ignoreversion
; libtulpar_runtime.a is consumed at link time by `tulpar build` / the
; default AOT pipeline. The AOT linker probes the directory containing
; tulpar.exe (see src/aot/aot_pipeline.cpp:build_link_search_dirs)
; before falling back to the dev-tree build dirs, so dropping the
; archive next to tulpar.exe is enough.
Source: "{#SourceRuntimeLib}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Start Menu entries. We add both a launcher (opens REPL — most useful
; thing a non-CLI user can do with the binary directly) and a docs link.
Name: "{group}\{#AppName} REPL"; Filename: "{app}\{#AppExeName}"; Parameters: "--repl"; WorkingDir: "{userdocs}"
Name: "{group}\{#AppName} Web Sitesi"; Filename: "{#AppURL}"
Name: "{group}\Kaldır {#AppName}"; Filename: "{uninstallexe}"

[Run]
; Optional: open the website on completion so the user lands on docs
; immediately. Unchecked by default — kept opt-in to avoid being noisy.
Filename: "{#AppURL}"; Description: "{code:VisitWebsiteDescription}"; Flags: nowait postinstall skipifsilent shellexec unchecked

[Code]
const
  EnvironmentKey = 'Environment';

// Localise a few strings the [Tasks]/[Run] sections need at parse time.
// Inno Setup evaluates these via the {code:Func} expression so the same
// .iss compiles to a Turkish OR English wizard depending on the runtime
// language pick — without duplicating the entire Tasks block.
function PathTaskDescription(Param: string): string;
begin
  if ActiveLanguage() = 'turkish' then
    Result := 'Tulpar''i kullanici PATH''ine ekle (oneriliyor)'
  else
    Result := 'Add Tulpar to user PATH (recommended)';
end;

function EnvGroupDescription(Param: string): string;
begin
  if ActiveLanguage() = 'turkish' then
    Result := 'Ortam:'
  else
    Result := 'Environment:';
end;

function VisitWebsiteDescription(Param: string): string;
begin
  if ActiveLanguage() = 'turkish' then
    Result := 'tulparlang.dev''i ac'
  else
    Result := 'Open tulparlang.dev';
end;

// PATH manipulation. We touch HKCU\Environment\Path (user-scope) so no
// admin prompt is needed and we don't pollute the system PATH. The
// `ChangesEnvironment=yes` directive in [Setup] makes Inno Setup
// broadcast WM_SETTINGCHANGE so newly opened terminals see the update
// immediately — existing terminals still need a restart.
procedure EnvAddPath(Path: string);
var
  Paths: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Paths) then
    Paths := '';

  // Idempotent: bail out if already present (case-insensitive, with
  // boundary semicolons so a substring match doesn't false-positive).
  if Pos(';' + Uppercase(Path) + ';', ';' + Uppercase(Paths) + ';') > 0 then
    Exit;

  if (Paths <> '') and (Paths[Length(Paths)] <> ';') then
    Paths := Paths + ';';
  Paths := Paths + Path;

  RegWriteStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Paths);
end;

procedure EnvRemovePath(Path: string);
var
  Paths: string;
  P, L: Integer;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Paths) then
    Exit;

  P := Pos(';' + Uppercase(Path) + ';', ';' + Uppercase(Paths) + ';');
  if P = 0 then
    Exit;

  // P is the position in the boundary-padded string (1-based); the real
  // start in Paths is P-1, but we also want to eat the leading separator
  // when it isn't the very first entry.
  L := Length(Path) + 1;
  if P = 1 then
    Delete(Paths, 1, L)
  else
    Delete(Paths, P - 1, L);

  // Trim a stray trailing semicolon left behind when we removed the last
  // entry from a path that ended with one.
  if (Paths <> '') and (Paths[Length(Paths)] = ';') then
    Delete(Paths, Length(Paths), 1);

  RegWriteStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Paths);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    EnvAddPath(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Always remove on uninstall — we put it there, we take it back. No
  // "leave PATH alone" task on uninstall to avoid a stale entry that
  // points at a deleted directory.
  if CurUninstallStep = usPostUninstall then
    EnvRemovePath(ExpandConstant('{app}'));
end;
