{ A03.11: native payload/data authority; Inno retains its own host authority. }
var
  UninstallRoot, UninstallHelper, UninstallReport: String;
  UninstallReportSerial: Integer;
  UninstallNativeComplete: Boolean;

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
  if Copy(Text, 1, 20) <> 'OutLoud.Inventory.2' + #10 then RaiseException('Native inventory schema is unsupported.');
  Cursor := 21;
  for Index := 1 to 6 do ReadUninstallInventoryField(Text, Cursor);
  Count := StrToInt(ReadUninstallInventoryField(Text, Cursor));
  if (Count <= 0) or (Count > 64) then RaiseException('Native inventory count is invalid.');
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

function RunOutLoudUninstall(Command, Expected: String; var ErrorText: String): Boolean;
var
  ExitCode, Remaining, Index: Integer;
  Parameters: String;
begin
  Result := False;
  UninstallReportSerial := UninstallReportSerial + 1;
  UninstallReport := ExpandConstant('{tmp}\outloud-uninstall-') + IntToStr(UninstallReportSerial) + '.ini';
  try
    if FileExists(UninstallReport) then RaiseException('Uninstall report already exists.');
    Parameters := Command + ' "' + UninstallRoot + '"';
    if Command = '--uninstall' then Parameters := Parameters + ' --confirmed-complete';
    if not Exec(UninstallHelper, Parameters + ' "' + UninstallReport + '"',
      ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then
      RaiseException('Out Loud removal stopped: ' + GetIniString('results', 'reason', 'native cleanup refused or failed', UninstallReport));
    if (GetIniString('results', 'schema', '', UninstallReport) <> '1') or
      (GetIniString('results', 'uninstall', '', UninstallReport) <> Expected) then
      RaiseException('Out Loud cleanup completion could not be verified.');
    Result := True;
  except
    ErrorText := GetExceptionMessage + #13#10#13#10 +
      'Close Voice Manager and applications using Out Loud, including World of Warcraft, and retry. No application is closed automatically.' + #13#10#13#10 +
      'Preserve .outloud-uninstall.dat and maintenance journals. Cleanup is not atomic; some components may already have been removed. ' +
      'Report: ' + UninstallReport;
    if FileExists(UninstallRoot + '\configuration\uninstall.failure.ini') then
      ErrorText := ErrorText + #13#10 + 'Retained report: ' + UninstallRoot + '\configuration\uninstall.failure.ini';
    if GetIniString('results', 'blocked_resource', '', UninstallReport) <> '' then
      ErrorText := ErrorText + #13#10 + 'Resource preserved for review: ' + UninstallReadablePath(GetIniString('results', 'blocked_resource', '', UninstallReport));
    if GetIniString('results', 'certificate_source', '', UninstallReport) <> '' then
      ErrorText := ErrorText + #13#10 + 'Certificate store: ' + UninstallReadablePath(GetIniString('results', 'certificate_source', '', UninstallReport)) +
        '; Windows error: ' + GetIniString('results', 'certificate_source_error', 'unknown', UninstallReport);
    Remaining := StrToIntDef(GetIniString('results', 'remaining_files', '0', UninstallReport), 0);
    ErrorText := ErrorText + #13#10 + 'Inventory state: ' + GetIniString('results', 'inventory', 'unverified', UninstallReport) +
      '. Remaining recorded files: ' + IntToStr(Remaining);
    for Index := 0 to Remaining - 1 do begin
      if Index >= 12 then Break;
      ErrorText := ErrorText + #13#10 + UninstallReadablePath(GetIniString('results', 'remaining' + IntToStr(Index), '', UninstallReport));
    end;
    ErrorText := ErrorText + #13#10 + 'COM: ' + GetIniString('results', 'registration', 'unknown', UninstallReport) +
      '; voice dependencies: ' + GetIniString('results', 'remaining_voice_dependencies', 'unknown', UninstallReport) +
      '; Personal certificate: ' + GetIniString('results', 'certificate_my', 'unknown', UninstallReport) +
      '; Root certificate: ' + GetIniString('results', 'certificate_root', 'unknown', UninstallReport) +
      '; private key: ' + GetIniString('results', 'private_key', 'unknown', UninstallReport);
    Log(ErrorText);
  end;
end;

function InitializeUninstall(): Boolean;
var
  SourceHelper, ErrorText, HelperReport, HelperHash: String;
  ExitCode: Integer;
begin
  Result := False;
  { Never allow removing only the Inno host while native cleanup is gated. }
  if not FoundationLifecycleReady then begin
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
  if not RunOutLoudUninstall('--uninstall-check', 'ready', ErrorText) then begin
    MsgBox(ErrorText, mbError, MB_OK); Exit;
  end;
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ErrorText, SavedReport: String;
begin
  if CurUninstallStep = usUninstall then begin
    if MsgBox('Completely uninstall Out Loud?' + #13#10#13#10 +
      'ALL managed voices, the complete managed voice library and metadata, settings, logs, caches and application data will be permanently deleted, ' +
      'together with the engine, Voice Manager, models and installation-owned signing certificate/private key.' + #13#10#13#10 +
      'Independently exported backups and shared Microsoft runtimes are preserved. This cannot be undone.',
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2) <> IDYES then Abort;
    { Abort from usUninstall stops Inno before it processes host removal. }
    if not RunOutLoudUninstall('--uninstall', 'awaiting_host', ErrorText) then begin
      MsgBox(ErrorText, mbError, MB_OK); Abort;
    end;
    UninstallNativeComplete := True;
  end;
  if (CurUninstallStep = usPostUninstall) and UninstallNativeComplete then begin
    if not RunOutLoudUninstall('--uninstall-finalize', 'complete', ErrorText) then begin
      { Inno has already processed its host. Preserve recovery state and a durable report. }
      SavedReport := UninstallRoot + '\configuration\uninstall.failure.ini';
      MsgBox(ErrorText + #13#10 + 'The Inno host may already be removed. Checked native finalization is required. Recovery report: ' + SavedReport + #13#10 +
        'Preserved recovery helper: ' + UninstallHelper, mbError, MB_OK);
      ExitIncompleteOutLoudUninstall(2);
    end;
    { Empty-only removal; never recurse over the chosen directory. }
    if DirExists(UninstallRoot) and not RemoveDir(UninstallRoot) then begin
      MsgBox('Native components were removed, but the installation directory remains: ' + UninstallRoot + #13#10 +
        'It may contain a remaining Inno host file or unrelated content. It was preserved; inspect it before deletion.', mbError, MB_OK);
      ExitIncompleteOutLoudUninstall(2);
    end;
  end;
end;

function UninstallNeedRestart(): Boolean;
begin
  Result := False; { No deferred deletions or automatic reboot. }
end;
