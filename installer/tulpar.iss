; TulparLang Windows installer (Inno Setup 6 script)
;
; Build with: iscc.exe /DAppVersion=2.1.0.42 /DSourceBinary=path\to\tulpar.exe installer\tulpar.iss
;
; CI sets the /D values; running locally without them falls back to a
; "0.0.0-dev" stamp and the build-windows/tulpar.exe path so a developer
; can `iscc installer\tulpar.iss` after `cmake --build`.
;
; On Windows, tulpar.exe pulls five non-system DLLs out of MSYS2's
; mingw64 runtime that don't ship with stock Windows:
;   * libwinpthread-1.dll, zlib1.dll, libzstd.dll — MinGW + LLVM
;     transitive deps (PR #52, PR #54).
;   * libssl-3-x64.dll, libcrypto-3-x64.dll — OpenSSL 3, linked into
;     tulpar.exe so `tulpar update`, `tulpar pkg install`, and the
;     embedded http_client can talk HTTPS. Discovered missing on a
;     fresh Win10 box: the binary refused to start with
;     STATUS_DLL_NOT_FOUND when these two were absent.
; The installer bundles all five next to tulpar.exe so a fresh install
; on a box without MSYS2 / Git PATH doesn't fail at launch.
; (libgcc_s_seh-1.dll and libstdc++-6.dll are folded into tulpar.exe
; via -static-libgcc/-static-libstdc++ so they don't need bundling.)

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif

#ifndef SourceBinary
  #define SourceBinary "..\build-windows\tulpar.exe"
#endif

#ifndef SourceRuntimeLib
  #define SourceRuntimeLib "..\build-windows\libtulpar_runtime.a"
#endif

#ifndef SourceMingwBin
  #define SourceMingwBin "C:\msys64\mingw64\bin"
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
; MinGW / LLVM runtime DLLs that don't ship with Windows. tulpar.exe
; lists each in its import table; without them the launcher dies with
; STATUS_DLL_NOT_FOUND on the very first invocation. Sitting next to
; tulpar.exe is enough — Windows resolves DLLs from the executable's
; own directory before walking PATH.
Source: "{#SourceMingwBin}\libwinpthread-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceMingwBin}\zlib1.dll";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceMingwBin}\libzstd.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceMingwBin}\libssl-3-x64.dll";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceMingwBin}\libcrypto-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion

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

// Post-install smoke test: run `tulpar.exe --version` and surface failure
// loudly. STATUS_DLL_NOT_FOUND used to slip past the installer because
// nothing exercised the binary before the success screen — users only
// found out later when their first `tulpar` call died silently from
// CMD with a popup that PowerShell suppresses entirely. We can't read
// the binary's stdout from Inno Setup's Exec (it has no pipe-capture),
// but the Win32 ResultCode tells us whether the process even loaded:
// STATUS_DLL_NOT_FOUND is 0xC0000135 = -1073741515 in signed 32-bit,
// and the same value for STATUS_ENTRYPOINT_NOT_FOUND is 0xC0000139.
function SmokeTestBinaryFailed(out NtStatus: Integer; out ErrorDetail: string): Boolean;
var
  ResultCode: Integer;
  ExePath: string;
begin
  ExePath := ExpandConstant('{app}\{#AppExeName}');
  // Exec returns false only if CreateProcess itself failed; a binary
  // that loads but exits non-zero still returns true with ResultCode
  // set to the process exit code. A failure to find a DLL aborts the
  // process before main() runs and bubbles up here as the NTSTATUS
  // returned to the parent.
  if not Exec(ExePath, '--version', ExpandConstant('{app}'),
              SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    NtStatus := -1;
    ErrorDetail := 'CreateProcess failed (exit ' + IntToStr(ResultCode) + ')';
    Result := True;
    Exit;
  end;
  NtStatus := ResultCode;
  if ResultCode = 0 then
  begin
    Result := False;
    Exit;
  end;
  // Non-zero — translate the well-known NTSTATUS values to something
  // the user can act on. Anything else falls through with the raw code.
  case ResultCode of
    -1073741515: ErrorDetail := 'STATUS_DLL_NOT_FOUND (0xC0000135) — bir DLL eksik / yuklenemedi.';
    -1073741511: ErrorDetail := 'STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139) — bir DLL eski surum / uyumsuz.';
    -1073741701: ErrorDetail := 'STATUS_INVALID_IMAGE_FORMAT (0xC000007B) — ikili 32/64-bit uyumsuzlugu.';
  else
    ErrorDetail := 'tulpar.exe exit code ' + IntToStr(ResultCode);
  end;
  Result := True;
end;

function SmokeTestFailureMessage(NtStatus: Integer; Detail: string): string;
begin
  if ActiveLanguage() = 'turkish' then
    Result := 'Kurulum tamamlandi ama tulpar.exe baslatilamadi.' + #13#10 + #13#10 +
              'Hata: ' + Detail + #13#10 + #13#10 +
              'Eksik DLL veya bozuk bir kurulumla karsi karsiyasiniz.' + #13#10 +
              'Lutfen kurulumu tekrar calistirin; sorun devam ederse' + #13#10 +
              'github.com/hamer1818/TulparLang/issues uzerinden bildirin' + #13#10 +
              've bu hata kodunu paylasin: ' + IntToStr(NtStatus)
  else
    Result := 'Install completed but tulpar.exe failed to start.' + #13#10 + #13#10 +
              'Error: ' + Detail + #13#10 + #13#10 +
              'A bundled DLL is likely missing or corrupted.' + #13#10 +
              'Re-run the installer; if the problem persists, file a' + #13#10 +
              'report at github.com/hamer1818/TulparLang/issues with' + #13#10 +
              'this error code: ' + IntToStr(NtStatus);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  NtStatus: Integer;
  Detail: string;
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('addtopath') then
      EnvAddPath(ExpandConstant('{app}'));
    // Smoke test runs after PATH wiring so the user gets a single
    // coherent failure message either way. We don't abort the install
    // if it fails — the files are already on disk, and aborting would
    // leave a half-installed state that's worse than a warning. A loud
    // MsgBox is the right level: it can't be missed and it tells the
    // user exactly what to do.
    if SmokeTestBinaryFailed(NtStatus, Detail) then
      MsgBox(SmokeTestFailureMessage(NtStatus, Detail), mbCriticalError, MB_OK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Always remove on uninstall — we put it there, we take it back. No
  // "leave PATH alone" task on uninstall to avoid a stale entry that
  // points at a deleted directory.
  if CurUninstallStep = usPostUninstall then
    EnvRemovePath(ExpandConstant('{app}'));
end;
