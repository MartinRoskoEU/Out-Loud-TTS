#if VER < EncodeVer(7,0,2)
  #error "Out Loud requires Inno Setup 7.0.2 or newer."
#endif
#ifndef StageRoot
  #error "Use build-installer.ps1 with a verified production stage."
#endif
#ifdef ProductVersion
  #error "ProductVersion must come from ProductVersion.props."
#endif
#ifdef ProductFileVersion
  #error "ProductFileVersion must come from ProductVersion.props."
#endif
#include StageRoot + "\installer-version.iss"
#define RepositoryRoot SourcePath + "..\.."

[Setup]
AppId=OutLoud
AppName=Out Loud
AppVersion={#ProductVersion}
VersionInfoVersion={#ProductFileVersion}
VersionInfoProductVersion={#ProductFileVersion}
VersionInfoProductName=Out Loud
VersionInfoCopyright=Copyright (c) 2026 Nendis
AppPublisher=Nendis
DefaultDirName={autopf}\OutLoud
DefaultGroupName=Out Loud
DisableDirPage=auto
UsePreviousAppDir=yes
UsePreviousTasks=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.22000
SetupIconFile={#RepositoryRoot}\ui\OutLoud.WinUI\Assets\OutLoud.ico
UninstallDisplayIcon={app}\OutLoud.WinUI.exe
UninstallDisplayName=Out Loud
OutputDir={#StageRoot}\output
OutputBaseFilename=OutLoud-Setup
WizardStyle=modern
DisableWelcomePage=no
Compression=lzma2
SolidCompression=yes
SetupLogging=yes
UninstallLogging=yes
CloseApplications=no
RestartApplications=no

[Messages]
ConfirmUninstall=Uninstall Out Loud for all users on this PC?%n%nThis will permanently remove the application and speech engine, shared Out Loud-managed voices and voice registrations, installed models, runtime updates, Settings and verified shared application data.%n%nThe installation-owned signing certificate and private key will also be removed. User-profile data and diagnostic logs, exported backups, unrelated files and certificates, and shared Microsoft runtimes will be kept.%n%nThis cannot be undone. Continue?

[Tasks]
Name: "startmenuicon"; Description: "Create a Start Menu shortcut"; GroupDescription: "Shortcuts:"
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#StageRoot}\bootstrap\OutLoudPrerequisites.exe"; Flags: dontcopy
Source: "{#StageRoot}\payload\bin\OutLoudInstallSigning.exe"; Flags: dontcopy
Source: "{#StageRoot}\prerequisites.ini"; Flags: dontcopy
Source: "{#StageRoot}\runtime-packages.ini"; Flags: dontcopy
Source: "{#StageRoot}\dependencies\vc_redist.x64.exe"; Flags: dontcopy
Source: "{#StageRoot}\payload\OutLoud.WinUI.runtimeconfig.json"; DestName: "OutLoud.runtimeconfig.json"; Flags: dontcopy
Source: "{#StageRoot}\maintenance-input.ini"; Flags: dontcopy
#include StageRoot + "\maintenance-files.iss"

[Icons]
Name: "{group}\Out Loud"; Filename: "{app}\OutLoud.WinUI.exe"; WorkingDir: "{app}"; Tasks: startmenuicon
Name: "{autodesktop}\Out Loud"; Filename: "{app}\OutLoud.WinUI.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[InstallDelete]
Type: files; Name: "{group}\Out Loud.lnk"; Check: not WizardIsTaskSelected('startmenuicon')
Type: files; Name: "{autodesktop}\Out Loud.lnk"; Check: not WizardIsTaskSelected('desktopicon')

[Code]
var
  RuntimeCancelRequested: Boolean;
  InstallationProgressPage: TOutputMarqueeProgressWizardPage;
function InteractiveSetup(): Boolean;
begin
  Result := IsAdmin and not WizardSilent;
end;
function FoundationLifecycleReady(): Boolean;
begin
  Result := InteractiveSetup;
end;
#include "diagnostics.iss"
#include "prerequisites.iss"
#include "responsive-process.iss"
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
  if not InteractiveSetup then begin
    MsgBox('Out Loud setup requires interactive administrator approval.', mbError, MB_OK); Exit;
  end;
  if not CheckOutLoudWindows or not ReadOutLoudPrerequisites(Report) then Exit;
  if not ReadInstalledOutLoud(Report) then begin MsgBox(Report, mbError, MB_OK); Exit; end;
  Result := True;
end;

procedure InitializeWizard();
begin
  InitializeInstallationDirectory;
  InitializeSigningWizard;
  InstallationProgressPage := CreateOutputMarqueeProgressPage('Installing Out Loud',
    'Please wait while Setup prepares and installs Out Loud on your computer.');
  MaintenanceCancelButton := TNewButton.Create(WizardForm);
  { Progress pages hide Inno's navigation buttons. Place the cooperative
    action in the same footer slot, with native dimensions and DPI anchoring. }
  MaintenanceCancelButton.Parent := WizardForm.CancelButton.Parent;
  MaintenanceCancelButton.Caption := SetupMessage(msgButtonCancel);
  MaintenanceCancelButton.SetBounds(WizardForm.CancelButton.Left,
    WizardForm.CancelButton.Top, WizardForm.CancelButton.Width,
    WizardForm.CancelButton.Height);
  MaintenanceCancelButton.Anchors := WizardForm.CancelButton.Anchors;
  MaintenanceCancelButton.OnClick := @RequestMaintenanceCancellation;
  MaintenanceCancelButton.Visible := False;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then begin
    { Finish shares Next's control; reclaim the now-hidden Cancel slot. }
    WizardForm.NextButton.Left := WizardForm.CancelButton.Left +
      WizardForm.CancelButton.Width - WizardForm.NextButton.Width;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrorText: String;
begin
  Result := InteractiveSetup;
  if Result and (CurPageID = wpSelectDir) then begin
    OutLoudDiagnosticReport := '';
    Result := RecoverSharedMaintenance(ErrorText);
    if Result then Result := CheckOutLoudInstallationDirectory(ErrorText);
    if not Result then ShowOutLoudDiagnostic('Out Loud setup', ErrorText, OutLoudDiagnosticReport);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  NeedsRestart := False;
  OutLoudDiagnosticReport := '';
  if not InteractiveSetup then begin Result := 'Interactive approval is required.'; Exit; end;
  InstallationProgressPage.Show;
  try
    InstallationProgressPage.SetText('Checking the installation and Windows speech registration...', '');
    InstallationProgressPage.Animate;
    if not RecoverSharedMaintenance(Result) then Exit;
    if not CheckOutLoudInstallationDirectory(Result) then Exit;
    if not CheckOutLoudRegistrationConflict(Result) then Exit;
    InstallationProgressPage.SetText('Checking and preparing required Microsoft components...', '');
    if not PrepareRuntimePrerequisites(Result) then Exit;
    InstallationProgressPage.SetText('Preparing application files...', '');
    ResponsiveCommandReport := '';
    ResponsiveCommandCancelable := True;
    MaintenanceCancelButton.Visible := True;
    MaintenanceCancelButton.Enabled := True;
    if PrepareSharedMaintenance(Result) then Result := '';
    if RuntimeCancelRequested then Abort;
  finally
    ResponsiveCommandCancelable := False;
    MaintenanceCancelButton.Visible := False;
    InstallationProgressPage.Hide;
    if (Result <> '') and not RuntimeCancelRequested then begin
      ShowOutLoudDiagnostic('Out Loud setup', Result, OutLoudDiagnosticReport);
      Result := 'Setup cannot continue. The full explanation and diagnostic path were shown in the diagnostic window. Resolve the reported problem before trying again.';
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssInstall) or (CurStep = ssPostInstall) then begin
    InstallationProgressPage.Show;
    try
      try
      if CurStep = ssInstall then begin
        InstallationProgressPage.SetText('Verifying, signing and installing Out Loud components...', '');
        InstallationProgressPage.Animate;
        StartSharedMaintenance;
      end else begin
        InstallationProgressPage.SetText('Verifying the completed installation...', '');
        InstallationProgressPage.Animate;
        CompleteSharedMaintenance;
      end;
    except
      InstallationProgressPage.Hide;
      if not MaintenanceCancelled then
        ShowOutLoudDiagnostic('Out Loud setup', GetExceptionMessage, OutLoudDiagnosticReport);
      Abort;
      end;
    finally
      InstallationProgressPage.Hide;
    end;
  end;
end;

procedure DeinitializeSetup();
var
  ErrorText: String;
  Recovered: Boolean;
begin
  if not MaintenanceCommitted and InteractiveSetup then begin
    if MaintenanceAwaitingHost then
      Log('Native installation awaits host completion. Retain recovery records and rerun this setup in its original directory.')
    else begin
      Recovered := True;
      if FileExists(WizardDirValue + '\configuration\maintenance.pending') or FileExists(WizardDirValue + '\.outloud-bootstrap.dat') then
        Recovered := RecoverSharedMaintenance(ErrorText);
      if Recovered then Recovered := CleanupFailedFreshMaintenance(ErrorText);
      if not Recovered then ShowOutLoudDiagnostic('Out Loud setup', 'Setup cleanup is incomplete. Retain protected recovery records: ' + ErrorText, OutLoudDiagnosticReport);
    end;
  end;
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if ResponsiveCommandRunning then begin
    Cancel := False; Confirm := False;
    if ResponsiveCommandCancelable then RequestMaintenanceCancellation(nil);
  end else if MaintenanceExecuting or (MaintenanceHostStarted and not MaintenanceCommitted) then begin
    Cancel := False; Confirm := False;
    MsgBox('Wait for native publication and host completion. Interrupted work requires checked recovery.', mbInformation, MB_OK);
  end else begin
    Confirm := True;
    Cancel := True;
  end;
end;
