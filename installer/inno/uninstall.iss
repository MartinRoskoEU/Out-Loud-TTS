{ A03.11: native payload/data authority; Inno retains its own host authority. }
var
  UninstallRoot, UninstallHelper, UninstallReport: String;
  UninstallReportSerial: Integer;
  UninstallNativeComplete: Boolean;

#include "responsive-uninstall.iss"

{ Inno handles post-uninstall exceptions and can otherwise show success/exit 0.
  Exit only this uninstaller after reporting a finalization failure. Protected
  recovery data and its private helper copy remain; no client is terminated. }
procedure ExitIncompleteOutLoudUninstall(Code: Cardinal);
  external 'ExitProcess@kernel32.dll stdcall';

function UninstallReadablePath(const Value: String): String;
var
  Index, LowByte, HighByte: Integer;
begin
  Result := '';
  if Length(Value) mod 4 <> 0 then Exit;
  Index := 1;
  try
    while Index <= Length(Value) do begin
      LowByte := StrToInt('$' + Copy(Value, Index, 2));
      HighByte := StrToInt('$' + Copy(Value, Index + 2, 2));
      Result := Result + Chr(LowByte + HighByte * 256);
      Index := Index + 4;
    end;
  except
    Result := '(path could not be decoded)';
  end;
end;

function ReadUninstallInventoryField(const Text: String; var Cursor: Integer): String;
var
  Colon, LengthValue: Integer;
begin
  Colon := Cursor;
  while (Colon <= Length(Text)) and (Text[Colon] <> ':') do begin
    if (Text[Colon] < '0') or (Text[Colon] > '9') or (Colon - Cursor >= 6) then RaiseException('Invalid native inventory framing.');
    Colon := Colon + 1;
  end;
  if (Colon > Length(Text)) or (Colon = Cursor) then RaiseException('Truncated native inventory.');
  LengthValue := StrToInt(Copy(Text, Cursor, Colon - Cursor));
  if (LengthValue > 131072) or (Colon + LengthValue + 1 > Length(Text)) or
    (Text[Colon + LengthValue + 1] <> #10) then RaiseException('Invalid native inventory length.');
  Result := Copy(Text, Colon + 1, LengthValue);
  Cursor := Colon + LengthValue + 2;
end;

function UninstallInventoryHelperHash(): String;
var
  Raw: AnsiString;
  Text, Path, Hash: String;
  Cursor, Count, Index: Integer;
begin
  Result := '';
  if not LoadStringFromFile(UninstallRoot + '\configuration\inventory.dat', Raw) then RaiseException('Native inventory is inaccessible.');
  Text := String(Raw);
  if Copy(Text, 1, 20) <> 'OutLoud.Inventory.3' + #10 then RaiseException('Native inventory schema is unsupported.');
  Cursor := 21;
  for Index := 1 to 6 do ReadUninstallInventoryField(Text, Cursor);
  Count := StrToInt(ReadUninstallInventoryField(Text, Cursor));
  if (Count <= 0) or (Count > 4096) then RaiseException('Native inventory count is invalid.');
  for Index := 1 to Count do begin
    Path := ReadUninstallInventoryField(Text, Cursor);
    if ReadUninstallInventoryField(Text, Cursor) <> '1' then RaiseException('Native inventory file stamp is absent.');
    Hash := ReadUninstallInventoryField(Text, Cursor);
    ReadUninstallInventoryField(Text, Cursor); { physical file identity }
    ReadUninstallInventoryField(Text, Cursor); { bytes }
    if Path = 'bin/OutLoudInstallSigning.exe' then begin
      if Result <> '' then RaiseException('Duplicate maintenance helper inventory entry.');
      Result := Hash;
    end;
  end;
  if (Cursor <> Length(Text) + 1) or (Length(Result) <> 64) then RaiseException('Native inventory is malformed.');
end;

function UninstallFailureDescription(const Reason: String): String;
begin
  if Reason = 'uninstall_unverified_voice_path' then
    Result := 'A voice registration points outside Out Loud''s verified shared voice library. It was preserved because its ownership could not be verified. You do not need to delete voices or models manually. Keep the diagnostic report for support.'
  else if (Reason = 'uninstall_unowned_voice_dependency') or (Reason = 'uninstall_voice_schema_changed') then
    Result := 'An existing voice registration could not be verified as Out Loud-managed. It was preserved to protect other applications and users. You do not need to delete voices or models manually. Keep the diagnostic report for support.'
  else if (Reason = 'uninstall_foreign_voice_dependency') or (Reason = 'uninstall_foreign_com_override') then
    Result := 'An Out Loud-related speech registration was found in a Windows location this installation does not manage, possibly for another user. It was preserved. Keep the diagnostic report for support.'
  else if Reason = 'uninstall_directory_not_empty_preserved' then
    Result := 'Out Loud could not finish removing a folder because files or subfolders remain inside it. The remaining content was kept. Keep the diagnostic report for support.'
  else if (Pos('in_use', Reason) > 0) or (Reason = 'maintenance_resources_active_close_clients') or
    (Reason = 'maintenance_resource_in_use_or_denied') or (Reason = 'uninstall_plan_locked') then
    Result := 'Out Loud files are still in use or Windows has denied access. Close Out Loud, World of Warcraft and other speech applications in all signed-in accounts, then retry. No application is closed automatically.'
  else if (Reason = 'uninstall_maintenance_recovery_required') or (Reason = 'uninstall_legacy_recovery_required') then
    Result := 'An earlier Out Loud update has not finished. Complete or recover that update before uninstalling. Keep the protected recovery records.'
  else if (Pos('uninstall_unbound_', Reason) = 1) or (Pos('uninstall_unknown_', Reason) = 1) or
    (Pos('uninstall_unverified_', Reason) = 1) or (Reason = 'uninstall_new_resource_preserved') then
    Result := 'Out Loud found files or application data whose ownership or integrity could not be verified. They were preserved instead of being deleted. Keep the diagnostic report for support.'
  else if (Pos('certificate', Reason) > 0) or (Pos('private_key', Reason) > 0) or (Pos('uninstall_key_', Reason) = 1) then
    Result := 'Windows could not safely verify or remove the Out Loud installation''s signing certificate or private key. Keep the diagnostic report for support; do not remove other certificates manually.'
  else
    Result := 'Out Loud could not complete its safety checks or cleanup. Keep the diagnostic report for support. You do not need to delete voices or models manually.';
end;

function RunOutLoudUninstall(Command, Expected: String; var ErrorText: String): Boolean;
var
  ExitCode, Remaining, Index: Integer;
  Parameters, Reason, DiagnosticText, VoiceName, VoiceToken, PreviousStatus: String;
  PreviousStyle: TNewProgressBarStyle;
  PreviousPosition: Longint;
  ShowProgress, Launched: Boolean;
  Output: TArrayOfString;
begin
  Result := False;
  UninstallReportSerial := UninstallReportSerial + 1;
  UninstallReport := ExpandConstant('{tmp}\outloud-uninstall-') + IntToStr(UninstallReportSerial) + '.ini';
  OutLoudDiagnosticReport := UninstallReport;
  { Inno destroys UninstallProgressForm before usPostUninstall. Finalization
    must run without reading or writing any of its controls. }
  ShowProgress := Command <> '--uninstall-finalize';
  if ShowProgress then begin
    PreviousStyle := UninstallProgressForm.ProgressBar.Style;
    PreviousPosition := UninstallProgressForm.ProgressBar.Position;
    PreviousStatus := UninstallProgressForm.StatusLabel.Caption;
    UninstallProgressForm.ProgressBar.State := npbsNormal;
    UninstallProgressForm.ProgressBar.Style := npbstMarquee;
    if Command = '--uninstall-check' then
      UninstallProgressForm.StatusLabel.Caption := 'Checking Out Loud files and shared data...'
    else
      UninstallProgressForm.StatusLabel.Caption := 'Removing Out Loud components and shared data...';
  end;
  try
    try
    if FileExists(UninstallReport) then RaiseException('Uninstall report already exists.');
    Parameters := Command + ' "' + UninstallRoot + '"';
    if Command = '--uninstall' then Parameters := Parameters + ' --confirmed-complete';
    Log('Out Loud native uninstall: command=' + Command + '; helper=' + UninstallHelper + '; report=' + UninstallReport);
    ExitCode := -1;
    if FileExists(UninstallReport + '.output.log') then RaiseException('Uninstall output record already exists.');
    Launched := ExecUninstallResponsive(UninstallHelper, Parameters + ' "' + UninstallReport + '"',
      ExpandConstant('{tmp}'), ExitCode);
    Log('Out Loud native uninstall: launched=' + IntToStr(Ord(Launched)) + '; exit_code=' + IntToStr(ExitCode));
    if LoadStringsFromFile(UninstallReport + '.output.log', Output) then
      LogInspectionOutput('uninstall output', Output)
    else if Launched and (ExitCode = 0) then
      RaiseException('The cleanup helper output could not be read. Keep the report for checked recovery.');
    if not Launched then
      RaiseException('The Out Loud cleanup helper could not start. Windows error ' + IntToStr(ExitCode) + ': ' + SysErrorMessage(ExitCode));
    if ExitCode <> 0 then
      RaiseException('Out Loud removal stopped: ' + GetIniString('results', 'reason', 'native cleanup refused or failed', UninstallReport));
    if (GetIniString('results', 'schema', '', UninstallReport) <> '1') or
      (GetIniString('results', 'uninstall', '', UninstallReport) <> Expected) then
      RaiseException('Out Loud cleanup completion could not be verified.');
    Result := True;
  except
    DiagnosticText := GetExceptionMessage + #13#10 + 'Operation: ' + Command + #13#10 + 'Report: ' + UninstallReport;
    VoiceToken := GetIniString('results', 'blocked_voice', '', UninstallReport);
    if GetIniString('results', 'blocked_voice_encoding', '', UninstallReport) = 'utf16le-hex' then
      VoiceToken := UninstallReadablePath(VoiceToken);
    if VoiceToken <> '' then
      DiagnosticText := DiagnosticText + #13#10 + 'Voice token: ' + VoiceToken + #13#10 +
        'Voice field: ' + GetIniString('results', 'voice_field', '', UninstallReport) + #13#10 +
        'Registered path: ' + UninstallReadablePath(GetIniString('results', 'voice_actual', '', UninstallReport)) + #13#10 +
        'Expected path: ' + UninstallReadablePath(GetIniString('results', 'voice_expected', '', UninstallReport));
    if FileExists(UninstallRoot + '\configuration\uninstall.failure.ini') then
      DiagnosticText := DiagnosticText + #13#10 + 'Retained report: ' + UninstallRoot + '\configuration\uninstall.failure.ini';
    if GetIniString('results', 'blocked_resource', '', UninstallReport) <> '' then
      DiagnosticText := DiagnosticText + #13#10 + 'Resource preserved for review: ' + UninstallReadablePath(GetIniString('results', 'blocked_resource', '', UninstallReport));
    if GetIniString('results', 'certificate_source', '', UninstallReport) <> '' then
      DiagnosticText := DiagnosticText + #13#10 + 'Certificate store: ' + UninstallReadablePath(GetIniString('results', 'certificate_source', '', UninstallReport)) +
        '; Windows error: ' + GetIniString('results', 'certificate_source_error', 'unknown', UninstallReport);
    Remaining := StrToIntDef(GetIniString('results', 'remaining_files', '0', UninstallReport), 0);
    DiagnosticText := DiagnosticText + #13#10 + 'Inventory state: ' + GetIniString('results', 'inventory', 'unverified', UninstallReport) +
      '. Remaining recorded files: ' + IntToStr(Remaining);
    for Index := 0 to Remaining - 1 do begin
      if Index >= 12 then Break;
      DiagnosticText := DiagnosticText + #13#10 + UninstallReadablePath(GetIniString('results', 'remaining' + IntToStr(Index), '', UninstallReport));
    end;
    DiagnosticText := DiagnosticText + #13#10 + 'COM: ' + GetIniString('results', 'registration', 'unknown', UninstallReport) +
      '; voice dependencies: ' + GetIniString('results', 'remaining_voice_dependencies', 'unknown', UninstallReport) +
      '; Personal certificate: ' + GetIniString('results', 'certificate_my', 'unknown', UninstallReport) +
      '; Root certificate: ' + GetIniString('results', 'certificate_root', 'unknown', UninstallReport) +
      '; private key: ' + GetIniString('results', 'private_key', 'unknown', UninstallReport);
    Reason := GetIniString('results', 'reason', '', UninstallReport);
    ErrorText := UninstallFailureDescription(Reason);
    if (Reason = 'uninstall_directory_not_empty_preserved') and
      (GetIniString('results', 'blocked_resource', '', UninstallReport) <> '') then
      ErrorText := ErrorText + #13#10#13#10 + 'Folder: ' + UninstallReadablePath(GetIniString('results', 'blocked_resource', '', UninstallReport));
    VoiceName := GetIniString('results', 'blocked_voice_name', '', UninstallReport);
    if VoiceName <> '' then
      ErrorText := ErrorText + #13#10#13#10 + 'Voice: ' + UninstallReadablePath(VoiceName);
    if (Command = '--uninstall-check') or
      ((Command = '--uninstall') and (GetIniString('results', 'inventory', '', UninstallReport) = 'preflight_incomplete')) then
      ErrorText := ErrorText + #13#10#13#10 + 'No files were removed during this safety check.'
    else
      ErrorText := ErrorText + #13#10#13#10 + 'Removal is incomplete. Keep the diagnostic report and recovery records before retrying.';
    ErrorText := ErrorText + #13#10#13#10 + 'Uninstall log: ' + ExpandConstant('{log}');
    if FileExists(UninstallRoot + '\configuration\uninstall.failure.ini') then begin
      OutLoudDiagnosticReport := UninstallRoot + '\configuration\uninstall.failure.ini';
      ErrorText := ErrorText + #13#10 + 'Diagnostic report: ' + UninstallRoot + '\configuration\uninstall.failure.ini'
    end else if FileExists(UninstallReport) then
      ErrorText := ErrorText + #13#10 + 'Diagnostic report (copy before closing this window): ' + UninstallReport
    else begin
      OutLoudDiagnosticReport := ExpandConstant('{log}');
      ErrorText := ErrorText + #13#10 + 'The native report was not created. The uninstall log contains the helper launch result and any captured error output.';
    end;
    Log(DiagnosticText);
    Log(ErrorText);
    end;
  finally
    if ShowProgress then begin
      UninstallProgressForm.ProgressBar.Style := PreviousStyle;
      UninstallProgressForm.ProgressBar.Position := PreviousPosition;
      if Result then UninstallProgressForm.StatusLabel.Caption := PreviousStatus
      else begin
        UninstallProgressForm.ProgressBar.State := npbsError;
        UninstallProgressForm.StatusLabel.Caption := 'Removal stopped. See the diagnostic report.';
      end;
    end;
  end;
end;

function InitializeUninstall(): Boolean;
var
  SourceHelper, ErrorText, HelperReport, HelperHash: String;
  ExitCode: Integer;
begin
  Result := False;
  { Never allow removing only the Inno host while native cleanup is gated. }
  { WizardSilent is setup-only; uninstall checks its own mode below. }
  if not IsAdmin then begin
    MsgBox('Administrator privileges are required for complete Out Loud uninstallation.', mbError, MB_OK);
    Exit;
  end;
  if UninstallSilent then begin
    MsgBox('Complete Out Loud uninstallation requires interactive confirmation.', mbError, MB_OK);
    Exit;
  end;
  if not ReadInstalledOutLoud(ErrorText) then begin MsgBox(ErrorText, mbError, MB_OK); Exit; end;
  UninstallRoot := ExistingInstallationDirectory;
  if (UninstallRoot = '') or not SameText(UninstallRoot, RemoveBackslashUnlessRoot(ExpandFileName(ExpandConstant('{app}')))) then begin
    MsgBox('This uninstaller does not match the recorded Out Loud installation directory.', mbError, MB_OK);
    Exit;
  end;
  SourceHelper := UninstallRoot + '\bin\OutLoudInstallSigning.exe';
  UninstallHelper := ExpandConstant('{tmp}\OutLoudUninstallMaintenance.exe');
  HelperReport := ExpandConstant('{tmp}\outloud-uninstall-helper.ini');
  try
    HelperHash := UninstallInventoryHelperHash;
    if not SameText(GetSHA256OfFile(SourceHelper), HelperHash) then RaiseException('The installed maintenance helper differs from its native inventory.');
  except
    MsgBox(GetExceptionMessage, mbError, MB_OK); Exit;
  end;
  if FileExists(UninstallHelper) or FileExists(HelperReport) or
#if VER >= EncodeVer(7,1,0)
    not CopyFile(SourceHelper, UninstallHelper, True) then begin
#else
    not FileCopy(SourceHelper, UninstallHelper, True) then begin
#endif
    MsgBox('The installation-owned maintenance helper could not be preserved in the protected uninstall temporary directory.', mbError, MB_OK);
    Exit;
  end;
  if not SameText(GetSHA256OfFile(UninstallHelper), HelperHash) then begin
    MsgBox('The preserved maintenance helper differs from its native inventory.', mbError, MB_OK); Exit;
  end;
  { Both files are checked against the current native inventory, including development updates. }
  if not Exec(UninstallHelper, '--uninstall-helper-check "' + UninstallRoot + '" "' + UninstallHelper + '" "' + HelperReport + '"',
    ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) or
    (GetIniString('results', 'uninstall', '', HelperReport) <> 'helper_verified') then begin
    MsgBox('The maintenance helper ownership could not be verified. The installation is preserved for checked recovery.', mbError, MB_OK);
    Exit;
  end;
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ErrorText: String;
begin
  if CurUninstallStep = usUninstall then begin
    { Inno's standard confirmation already explains complete, all-user removal. }
    if not RunOutLoudUninstall('--uninstall-check', 'ready', ErrorText) then begin
      ShowOutLoudDiagnostic('Out Loud uninstall', ErrorText, OutLoudDiagnosticReport); Abort;
    end;
    { Abort from usUninstall stops Inno before it processes host removal. }
    if not RunOutLoudUninstall('--uninstall', 'awaiting_host', ErrorText) then begin
      ShowOutLoudDiagnostic('Out Loud uninstall', ErrorText, OutLoudDiagnosticReport); Abort;
    end;
    UninstallNativeComplete := True;
    UninstallCompletionForm.Position := poDesigned;
    UninstallCompletionForm.Left := UninstallProgressForm.Left;
    UninstallCompletionForm.Top := UninstallProgressForm.Top;
  end;
  if (CurUninstallStep = usPostUninstall) and UninstallNativeComplete then begin
    UninstallProgressAvailable := False;
    UninstallCompletionForm.Show;
    try
      if not RunOutLoudUninstall('--uninstall-finalize', 'complete', ErrorText) then begin
        { Inno has already processed its host. Preserve recovery state and a durable report. }
        RaiseException(ErrorText + #13#10 + 'The Inno host may already be removed. Checked native finalization is required. Preserve the remaining recovery records in: ' + UninstallRoot);
      end;
      { Empty-only removal; never recurse over the chosen directory. }
      if DirExists(UninstallRoot) and not RemoveDir(UninstallRoot) then begin
        OutLoudDiagnosticReport := UninstallRoot;
        RaiseException('Native components were removed, but the installation directory remains: ' + UninstallRoot + #13#10 +
          'It may contain a remaining Inno host file or unrelated content. It was preserved; inspect it before deletion.');
      end;
      UninstallCompletionForm.Hide;
    except
      { Inno catches post-uninstall script exceptions and can still show success.
        Include unexpected script failures in the existing nonzero-exit path. }
      ErrorText := GetExceptionMessage;
      UninstallCompletionForm.Hide;
      Log('Out Loud uninstall finalization failed: ' + ErrorText);
      if Pos(ExpandConstant('{log}'), ErrorText) = 0 then
        ErrorText := ErrorText + #13#10#13#10 + 'Uninstall log: ' + ExpandConstant('{log}');
      try
        ShowOutLoudDiagnostic('Out Loud uninstall', ErrorText + #13#10#13#10 +
          'Preserved recovery helper: ' + UninstallHelper, OutLoudDiagnosticReport);
      finally
        ExitIncompleteOutLoudUninstall(2);
      end;
    end;
  end;
end;

function UninstallNeedRestart(): Boolean;
begin
  Result := False; { No deferred deletions or automatic reboot. }
end;
