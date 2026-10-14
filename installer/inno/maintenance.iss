{ Inno owns consent, prerequisites, shortcuts and its own uninstall metadata.
  The native controller alone owns product publication/signing/COM/recovery. }
var
  MaintenanceAwaitingHost, MaintenanceCommitted, MaintenancePrepared, MaintenanceExecuting, MaintenanceHostStarted: Boolean;
  MaintenanceInput, MaintenanceParameters, MaintenanceRoot, MaintenanceLastState: String;

function RunMaintenanceCommand(Parameters, Expected: String; var ErrorText: String): Boolean;
var
  Helper, Report: String;
  ExitCode: Integer;
begin
  Result := False;
  if not FoundationLifecycleReady then begin
    ErrorText := 'Administrator privileges are required for shared native maintenance.';
    Exit;
  end;
  try
    ExtractTemporaryFile('OutLoudInstallSigning.exe');
    Helper := ExpandConstant('{tmp}\OutLoudInstallSigning.exe');
    RuntimeReportSerial := RuntimeReportSerial + 1;
    Report := ExpandConstant('{tmp}\maintenance-') + IntToStr(RuntimeReportSerial) + '.ini';
    if FileExists(Report) then RaiseException('Maintenance report already exists.');
    if not Exec(Helper, Parameters + ' "' + Report + '"', ExpandConstant('{tmp}'),
      SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then
      RaiseException('Native maintenance refused or failed: ' + GetIniString('results', 'reason', 'see protected recovery state', Report) +
        '. Preserve any remaining .outloud-bootstrap.dat, .outloud-fresh-cleanup.dat and protected configuration recovery records. Report: ' + Report);
    MaintenanceLastState := GetIniString('results', 'maintenance', '', Report);
    if (GetIniString('results', 'schema', '', Report) <> '1') or
      ((Expected <> 'reconciled') and (MaintenanceLastState <> Expected)) or
      ((Expected = 'reconciled') and (MaintenanceLastState <> 'rolled_back') and
        (MaintenanceLastState <> 'awaiting_host') and (MaintenanceLastState <> 'committed')) then
      RaiseException('Native maintenance did not verify the requested completion state.');
    Result := True;
  except
    ErrorText := GetExceptionMessage;
  end;
end;

procedure MaterializeMaintenanceFile(TemporaryName, Relative: String);
var
  Destination: String;
begin
  Destination := MaintenanceInput + '\' + Relative;
  if FileExists(Destination) then RaiseException('Maintenance candidate already exists.');
  if not ForceDirectories(ExtractFileDir(Destination)) then RaiseException('Cannot create temporary candidate directory.');
  ExtractTemporaryFile(TemporaryName);
  if not RenameFile(ExpandConstant('{tmp}\') + TemporaryName, Destination) then
    RaiseException('Cannot materialize maintenance candidate.');
end;

function PrepareSharedMaintenance(var ErrorText: String): Boolean;
var
  Operation, InstalledVersion, Parameters: String;
begin
  Result := False;
  if not FoundationLifecycleReady then begin
    ErrorText := 'Administrator privileges are required to prepare installation.';
    Exit;
  end;
  if not CheckProductConsent or not CheckOutLoudPrerequisites then begin
    ErrorText := 'Accepted licenses and compatible prerequisites are required.';
    Exit;
  end;
  try
    if MaintenanceAwaitingHost or MaintenancePrepared then begin
      if not SameText(MaintenanceRoot, WizardDirValue) then RaiseException('Resume maintenance in its original directory.');
      Result := True; Exit;
    end;
    MaintenanceInput := ExpandConstant('{tmp}\maintenance-input');
    if DirExists(MaintenanceInput) then RaiseException('Temporary maintenance directory already exists.');
    if not CreateDir(MaintenanceInput) then RaiseException('Cannot create maintenance input directory.');
#include StageRoot + "\maintenance-copy.iss"
    MaterializeMaintenanceFile('OutLoudPrerequisites.exe', 'bootstrap\OutLoudPrerequisites.exe');
    MaterializeMaintenanceFile('prerequisites.ini', 'prerequisites.ini');
    MaterializeMaintenanceFile('maintenance-input.ini', 'maintenance-input.ini');
    Operation := 'fresh';
    if ExistingInstallationDirectory <> '' then begin
      if not RegQueryStringValue(HKLM64, OutLoudUninstallKey, 'DisplayVersion', InstalledVersion) then
        RaiseException('The installed product version cannot be verified.');
      if InstalledVersion = '{#ProductVersion}' then Operation := 'repair'
      else Operation := 'release';
    end;
    Parameters := '--maintain ' + Operation + ' Package "' + WizardDirValue + '" "' +
      MaintenanceInput + '" "' + MaintenanceInput + '\maintenance-input.ini" --consented';
    MaintenanceParameters := Parameters;
    MaintenanceRoot := WizardDirValue;
    MaintenancePrepared := True;
    Result := True;
  except
    ErrorText := GetExceptionMessage;
  end;
end;

function CleanupFailedFreshMaintenance(var ErrorText: String): Boolean;
begin
  Result := True;
  if not ProductConsentSatisfied or (ExistingInstallationDirectory <> '') or MaintenanceAwaitingHost or MaintenanceCommitted then Exit;
  if FileExists(WizardDirValue + '\.outloud-fresh-cleanup.dat') or
    (DirExists(WizardDirValue + '\configuration\maintenance') and
      not FileExists(WizardDirValue + '\configuration\signing.ini')) then begin
    Result := RunMaintenanceCommand('--maintenance-fresh-cleanup "' + WizardDirValue + '"', 'fresh_removed', ErrorText);
    if Result then Log('Verified failed fresh-install artifacts and application directory removed.');
  end;
end;

function RecoverSharedMaintenance(var ErrorText: String): Boolean;
begin
  Result := True;
  if MaintenanceAwaitingHost then begin
    Result := SameText(MaintenanceRoot, WizardDirValue);
    if not Result then ErrorText := 'Resume maintenance in its original directory.';
    Exit;
  end;
  if FileExists(WizardDirValue + '\configuration\maintenance.pending') or
    FileExists(WizardDirValue + '\.outloud-bootstrap.dat') then begin
    Result := RunMaintenanceCommand('--maintenance-reconcile "' + WizardDirValue + '"', 'reconciled', ErrorText);
    if Result and (MaintenanceLastState = 'awaiting_host') then begin
      MaintenanceAwaitingHost := True;
      MaintenanceRoot := WizardDirValue;
      WizardForm.DirEdit.Enabled := False;
      WizardForm.DirBrowseButton.Enabled := False;
    end;
  end;
  if Result and not MaintenanceAwaitingHost then Result := CleanupFailedFreshMaintenance(ErrorText);
end;

procedure StartSharedMaintenance;
var
  ErrorText: String;
begin
  if RuntimeCancelRequested or not CheckProductConsent then Abort;
  WizardForm.CancelButton.Enabled := False;
  MaintenanceHostStarted := True;
  if MaintenanceAwaitingHost then begin
    if not RunMaintenanceCommand('--maintenance-reconcile "' + MaintenanceRoot + '"', 'awaiting_host', ErrorText) then RaiseException(ErrorText);
    Exit;
  end;
  if not MaintenancePrepared then RaiseException('Native maintenance input was not prepared.');
  MaintenanceExecuting := True;
  try
    if not RunMaintenanceCommand(MaintenanceParameters, 'awaiting_host', ErrorText) then RaiseException(ErrorText);
    MaintenanceAwaitingHost := True;
  finally
    MaintenanceExecuting := False;
  end;
end;

procedure CompleteSharedMaintenance;
var
  ErrorText: String;
begin
  if not MaintenanceAwaitingHost then RaiseException('Native publication must finish before Inno completion.');
  if not RunMaintenanceCommand('--maintenance-finalize "' + WizardDirValue + '"', 'committed', ErrorText) then
    RaiseException(ErrorText);
  MaintenanceCommitted := True;
  MaintenanceAwaitingHost := False;
end;
