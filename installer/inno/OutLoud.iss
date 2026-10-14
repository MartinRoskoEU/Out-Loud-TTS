; Public release installer. Compile only a fully verified current stage.
#if VER < EncodeVer(7,0,2)
  #error "Out Loud requires Inno Setup 7.0.2 or newer."
#endif

#ifndef StageRoot
  #error "StageRoot must reference a validated production stage."
#endif
#ifdef ProductVersion
  #error "ProductVersion is generated from ProductVersion.props; do not pass a compiler override."
#endif
#ifdef ProductFileVersion
  #error "ProductFileVersion is generated; do not pass a compiler override."
#endif
#ifdef ProductName
  #error "ProductName is generated; do not pass a compiler override."
#endif
#ifdef ProductCopyright
  #error "ProductCopyright is generated; do not pass a compiler override."
#endif
#ifdef InstallerBaseFilename
  #error "InstallerBaseFilename is generated; do not pass a compiler override."
#endif
#ifdef EngineClsid
  #error "EngineClsid is generated from sapi/src/identity.hpp; do not override it."
#endif
#include StageRoot + "\installer-version.iss"
#define RepositoryRoot SourcePath + "..\.."

[Setup]
AppId=OutLoud
AppName={#ProductName}
AppVersion={#ProductVersion}
VersionInfoVersion={#ProductFileVersion}
VersionInfoProductVersion={#ProductFileVersion}
VersionInfoProductName={#ProductName}
VersionInfoTextVersion={#ProductFileVersion}
VersionInfoProductTextVersion={#ProductVersion}
VersionInfoCopyright={#ProductCopyright}
AppPublisher=Nendis
DefaultDirName={autopf}\OutLoud
DisableDirPage=auto
UsePreviousAppDir=yes
DefaultGroupName=Out Loud
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.22000
SetupIconFile={#RepositoryRoot}\ui\OutLoud.VoiceManager\Assets\OutLoud.ico
UninstallDisplayIcon={app}\bin\OutLoud.VoiceManager.exe
UninstallDisplayName={#ProductName}
OutputDir={#StageRoot}\..\compiler-output
OutputBaseFilename={#InstallerBaseFilename}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableWelcomePage=no
LicenseFile={#StageRoot}\payload\licenses\OutLoud-Installation-Terms.txt
CloseApplications=no
RestartApplications=no
; LicenseFile is generated from complete offline documents during validated staging.

[Messages]
WizardLicense=Licenses and acceptable use
LicenseAccepted=I &accept the licenses and acknowledge the Kyutai use policy
LicenseNotAccepted=I &do not accept
WindowsVersionNotSupported=Out Loud requires Windows 11 on a native x64 client system. Windows 10, ARM64, 32-bit Windows and Windows Server are unsupported.

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
; Read-only bootstrap, extracted temporarily; never installed into {app}.
Source: "{#StageRoot}\bootstrap\OutLoudPrerequisites.exe"; Flags: dontcopy
Source: "{#StageRoot}\prerequisites.ini"; Flags: dontcopy
Source: "{#StageRoot}\runtime-packages.ini"; Flags: dontcopy
Source: "{#StageRoot}\payload\bin\OutLoud.VoiceManager.runtimeconfig.json"; DestName: "OutLoud.runtimeconfig.json"; Flags: dontcopy
Source: "{#StageRoot}\payload\bin\OutLoudInstallSigning.exe"; Flags: dontcopy
Source: "{#StageRoot}\signing-policy.ini"; Flags: dontcopy
Source: "{#StageRoot}\registration-policy.ini"; Flags: dontcopy
Source: "{#StageRoot}\maintenance-input.ini"; Flags: dontcopy
; Explicit files, never a recursive source wildcard or a harvested user library.
#include StageRoot + "\maintenance-files.iss"

[Icons]
Name: "{group}\Out Loud"; Filename: "{app}\bin\OutLoud.VoiceManager.exe"; WorkingDir: "{app}\bin"; IconFilename: "{app}\bin\OutLoud.VoiceManager.exe"; IconIndex: 0
Name: "{autodesktop}\Out Loud"; Filename: "{app}\bin\OutLoud.VoiceManager.exe"; WorkingDir: "{app}\bin"; IconFilename: "{app}\bin\OutLoud.VoiceManager.exe"; IconIndex: 0; Tasks: desktopicon

[Code]
#include "prerequisites.iss"
#include "consent.iss"
#include "runtime-install.iss"
#include "installation.iss"
#include "signing.iss"
#include "registration.iss"
#include "maintenance.iss"
#include "uninstall.iss"

function InitializeSetup(): Boolean;
var
  Report: String;
begin
  Result := False;
  if WizardSilent then begin
    Log('Silent setup is unsupported: interactive license and Kyutai use-policy acceptance is mandatory.');
    Exit;
  end;
  if not CheckOutLoudWindows then
    Exit;
  if not ReadOutLoudPrerequisites(Report) then
    Exit;
  if not ReadInstalledOutLoud(Report) then begin
    MsgBox(Report, mbError, MB_OK);
    Exit;
  end;
  Result := IsAdmin;
end;

procedure InitializeWizard();
begin
  InitializeProductConsent;
  InitializeRuntimeWizard;
  InitializeInstallationDirectory;
  InitializeSigningWizard;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrorText: String;
begin
  Result := not WizardSilent;
  if Result and (CurPageID <> wpWelcome) then
    Result := ProductConsentSatisfied;
  if Result and (CurPageID = wpSelectDir) then begin
    Result := RecoverSharedMaintenance(ErrorText);
    if Result then Result := CheckOutLoudInstallationDirectory(ErrorText);
    if not Result then MsgBox(ErrorText, mbError, MB_OK);
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  if PageID = wpLicense then begin Result := False; Exit; end;
  Result := (PageID = RuntimeConsentPage.ID) and RuntimeInitiallyCompatible;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  NeedsRestart := False; { Never request an automatic reboot or autorun resume. }
  if not CheckProductConsent then begin
    Result := 'Interactive acceptance of licenses and acknowledgment of the Kyutai policy are required.';
    Exit;
  end;
  if not RecoverSharedMaintenance(Result) then Exit;
  if not CheckOutLoudInstallationDirectory(Result) then Exit;
  if not CheckOutLoudRegistrationConflict(Result) then Exit;
  if not PrepareRuntimePrerequisites(Result) then Exit;
  if PrepareSharedMaintenance(Result) then Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then StartSharedMaintenance;
  if CurStep = ssPostInstall then begin
    CompleteSharedMaintenance;
  end;
end;

procedure DeinitializeSetup;
var
  ErrorText: String;
  Recovered: Boolean;
begin
  if not MaintenanceCommitted and ProductConsentSatisfied then begin
    if MaintenanceAwaitingHost then
      Log('Native installation retained awaiting Inno host completion. Rerun this setup in the same directory; no rollback beneath changed host metadata is attempted.')
    else begin
      Recovered := True;
      if FileExists(WizardDirValue + '\configuration\maintenance.pending') or
        FileExists(WizardDirValue + '\.outloud-bootstrap.dat') then
        Recovered := RecoverSharedMaintenance(ErrorText);
      if Recovered then Recovered := CleanupFailedFreshMaintenance(ErrorText);
      if not Recovered then begin
        Log('Failed setup retained protected recovery records: ' + ErrorText);
        MsgBox('Failed setup could not safely remove all installation artifacts from ' + WizardDirValue +
          '. Preserve the remaining recovery records and rerun setup after reviewing this refusal: ' + ErrorText, mbError, MB_OK);
      end;
    end;
  end;
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if MaintenanceExecuting or MaintenanceHostStarted then begin
    Cancel := False; Confirm := False;
    MsgBox('Native publication or Inno host completion is in progress. Wait for setup to finish. An interrupted host can be resumed by rerunning this setup.', mbInformation, MB_OK);
  end else if RuntimeExecuting then begin
    RuntimeCancelRequested := True;
    { Do not kill a system installer or release its pinned executable mid-operation. }
    Cancel := False;
    Confirm := False;
    MsgBox('A Microsoft runtime installer is running. Cancel in its progress window if offered, or wait for it to finish. Out Loud installation will then stop.', mbInformation, MB_OK);
  end else begin
    Confirm := False;
    Cancel := MsgBox('Cancel Out Loud setup? Any Microsoft runtimes already installed will remain.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
    if Cancel then RuntimeCancelRequested := True;
  end;
end;

function GetCustomSetupExitCode(): Integer;
begin
  Result := 0;
  if not MaintenanceCommitted then Result := 2;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and not MaintenanceCommitted then begin
    WizardForm.FinishedHeadingLabel.Caption := 'Out Loud setup needs recovery';
    WizardForm.FinishedLabel.Caption := 'Complete installation has not been verified. Preserve the protected recovery records and rerun this setup in the same directory. Shared Microsoft runtimes remain installed.';
  end;
end;

{ Native uninstall precedes Inno host removal; no wildcard delete sections. }
