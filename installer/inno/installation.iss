{ One machine-wide instance, discovered by the stable Inno AppId, never by CLSID. }
const
  OutLoudUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OutLoud_is1';
var
  ExistingInstallationDirectory: String;

function InstallationDiagnosticLog: String;
var
  Source, Snapshot: String;
begin
  { Inno's log survives setup cleanup. Never write into the installation directory. }
  Source := ExpandConstant('{log}');
  Result := Source;
  if Source = '' then begin
    Result := 'Setup logging is unavailable.';
    Exit;
  end;
  Snapshot := AddBackslash(GetEnv('TEMP')) + 'OutLoud-Setup-{#ProductVersion}-' +
    ExtractFileName(RemoveBackslashUnlessRoot(ExpandConstant('{tmp}'))) + '-' +
    IntToStr(RuntimeReportSerial) + '.log';
  try
    { FailIfExists prevents replacement of any pre-existing file or link. }
    if (GetEnv('TEMP') <> '') and CopyFile(Source, Snapshot, True) then
      Result := Snapshot
    else
      Log('OutLoud inspection: diagnostic snapshot unavailable; retained setup log: ' + Source);
  except
    Log('OutLoud inspection: diagnostic snapshot failed: ' + GetExceptionMessage);
  end;
end;

function InspectionOutputValue(const Lines: TArrayOfString; const Key: String): String;
var
  I: Integer;
begin
  Result := '';
  for I := 0 to GetArrayLength(Lines) - 1 do
    if Pos(Key + '=', Lines[I]) = 1 then begin
      Result := Copy(Lines[I], Length(Key) + 2, Length(Lines[I]));
      Exit;
    end;
end;

procedure LogInspectionOutput(const Stream: String; const Lines: TArrayOfString);
var
  I: Integer;
begin
  for I := 0 to GetArrayLength(Lines) - 1 do
    Log('OutLoud inspection ' + Stream + ': ' + Lines[I]);
end;

function ReadInstalledOutLoud(var ErrorText: String): Boolean;
var
  Directory: String;
begin
  Result := False;
  ExistingInstallationDirectory := '';
  if RegKeyExists(HKLM64, OutLoudUninstallKey) then begin
    if not RegQueryStringValue(HKLM64, OutLoudUninstallKey, 'InstallLocation', Directory) or
      (Directory = '') or not DirExists(Directory) then begin
      ErrorText := 'The existing Out Loud installer record has no accessible installation directory. Checked repair is required; setup will not create a second installation.';
      Exit;
    end;
    ExistingInstallationDirectory := RemoveBackslashUnlessRoot(ExpandFileName(Directory));
  end;
  Result := True;
end;

procedure InitializeInstallationDirectory;
begin
  if ExistingInstallationDirectory <> '' then begin
    WizardForm.DirEdit.Text := ExistingInstallationDirectory;
    WizardForm.DirEdit.Enabled := False;
    WizardForm.DirBrowseButton.Enabled := False;
  end;
end;

function CheckOutLoudInstallationDirectory(var ErrorText: String): Boolean;
var
  Directory, Helper, Report, Phase, Category, Reason, FailurePhase: String;
  ExitCode: Integer;
  Output: TExecOutput;
  Launched: Boolean;
begin
  Result := False;
  Phase := 'installer_record';
  try
    if not ReadInstalledOutLoud(ErrorText) then RaiseException(ErrorText);
    Phase := 'directory_selection';
    Directory := RemoveBackslashUnlessRoot(ExpandFileName(WizardDirValue));
    WizardForm.DirEdit.Text := Directory; { Application files and native checks use the same resolved path. }
    if (ExistingInstallationDirectory <> '') and not SameText(Directory, ExistingInstallationDirectory) then
      RaiseException('Update Out Loud in its recorded installation directory. Relocation and parallel installations are unsupported.');
    if (ExistingInstallationDirectory = '') and FileExists(Directory + '\configuration\signing.ini') and
      not FileExists(Directory + '\configuration\maintenance.pending') then
      RaiseException('This directory contains ownership state without an Out Loud installer record. Checked recovery is required; setup will not adopt it.');
    Phase := 'helper_extraction';
    ExtractTemporaryFile('OutLoudInstallSigning.exe');
    Helper := ExpandConstant('{tmp}\OutLoudInstallSigning.exe');
    RuntimeReportSerial := RuntimeReportSerial + 1;
    Report := ExpandConstant('{tmp}\installation-inspect-') + IntToStr(RuntimeReportSerial) + '.ini';
    Log('OutLoud inspection: version={#ProductVersion}; directory=' + Directory);
    Log('OutLoud inspection: helper=' + Helper + '; report=' + Report);
    Phase := 'report_preparation';
    if FileExists(Report) then RaiseException('Installation inspection report already exists.');
    Phase := 'helper_execution';
    ExitCode := -1;
    Launched := ExecAndCaptureOutput(Helper, '--installation-inspect "' + Directory + '" "' + Report + '"',
      ExpandConstant('{tmp}'), SW_SHOWNORMAL, ewWaitUntilTerminated, ExitCode, Output);
    if not Launched then begin
      Log('OutLoud inspection: helper_launch_failed; win32_error=' + IntToStr(ExitCode));
      RaiseException('The installation inspection helper could not start. Windows error ' +
        IntToStr(ExitCode) + ': ' + SysErrorMessage(ExitCode));
    end;
    Log('OutLoud inspection: helper_exit_code=' + IntToStr(ExitCode));
    LogInspectionOutput('stdout', Output.StdOut);
    LogInspectionOutput('stderr', Output.StdErr);
    if Output.Error then begin
      Log('OutLoud inspection: helper_output_capture_failed');
      RaiseException('Setup could not completely capture the installation inspection helper output.');
    end;
    if ExitCode <> 0 then begin
      Category := InspectionOutputValue(Output.StdErr, 'inspection_category');
      Reason := InspectionOutputValue(Output.StdErr, 'inspection_reason');
      FailurePhase := InspectionOutputValue(Output.StdErr, 'inspection_phase');
      if Category = 'validation' then begin
        Phase := 'security_validation';
        RaiseException('The installation directory failed security or compatibility validation. Reason: ' +
          Reason + '; phase: ' + FailurePhase + '. The diagnostic log identifies the path and security details.');
      end else if Category = 'reporting' then begin
        Phase := 'helper_reporting';
        RaiseException('The installation inspection helper could not generate its report. Reason: ' +
          Reason + '; phase: ' + FailurePhase + '.');
      end else
        RaiseException('The installation inspection helper failed with exit code ' + IntToStr(ExitCode) + '.');
    end;
    Phase := 'report_verification';
    if not FileExists(Report) then
      RaiseException('The installation inspection helper returned success but its report is missing.');
    Log('OutLoud inspection: report_schema=' + GetIniString('results', 'schema', '', Report) +
      '; report_location=' + GetIniString('results', 'location', '', Report));
    if (GetIniString('results', 'schema', '', Report) <> '1') or
      (GetIniString('results', 'location', '', Report) <> 'secure_candidate') then
      RaiseException('The installation inspection report is invalid or does not confirm a secure destination.');
    Log('OutLoud inspection: secure_candidate verified');
    Result := True;
  except
    ErrorText := GetExceptionMessage;
    if (Phase = 'helper_extraction') or (Phase = 'helper_execution') then
      ErrorText := 'Installation inspection execution failed (' + Phase + '): ' + ErrorText;
    Log('OutLoud inspection: setup_phase=' + Phase + '; failure=' + ErrorText);
    ErrorText := ErrorText + #13#10#13#10 + 'Diagnostic log (copy it before closing Windows Sandbox):' + #13#10 +
      InstallationDiagnosticLog;
  end;
end;
