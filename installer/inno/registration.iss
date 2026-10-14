{ A03.7. Checked HKLM64 registration, still unreachable behind lifecycle gates. }
var
  RegistrationVerified: Boolean;

function CheckOutLoudRegistrationConflict(var ErrorText: String): Boolean;
var
  Helper, Report, Status: String;
  ExitCode: Integer;
begin
  Result := False;
  try
    ExtractTemporaryFile('OutLoudInstallSigning.exe');
    Helper := ExpandConstant('{tmp}\OutLoudInstallSigning.exe');
    RuntimeReportSerial := RuntimeReportSerial + 1;
    Report := ExpandConstant('{tmp}\registration-inspect-') + IntToStr(RuntimeReportSerial) + '.ini';
    if FileExists(Report) then RaiseException('Registration inspection report already exists.');
    if not Exec(Helper, '--registration-inspect "' + ExpandConstant('{app}') + '" "' + Report + '"',
      ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) or
      not FileExists(Report) or (GetIniString('results', 'schema', '', Report) <> '1') then
      RaiseException('Cannot safely inspect the existing SAPI engine registration.');
    Status := GetIniString('results', 'registration', '', Report);
    if (Status <> 'absent') and (Status <> 'owned_candidate') then
      RaiseException('Conflicting or malformed SAPI engine registration (' + Status + '). No registration will be overwritten.');
    Result := True;
  except
    ErrorText := GetExceptionMessage;
  end;
end;

function PrepareOutLoudRegistration(var ErrorText: String): Boolean;
var
  Helper, Report, SigningPolicy, RegistrationPolicy, Detector, SigningState: String;
  ExitCode: Integer;
begin
  Result := False;
  RegistrationVerified := False;
  if not FoundationLifecycleReady then begin
    ErrorText := 'SAPI registration is disabled until production transaction recovery is implemented and validated.';
    Exit;
  end;
  if not SigningVerified or not CheckProductConsent or not CheckOutLoudPrerequisites then begin
    ErrorText := 'Verified signing, accepted licenses and compatible runtimes are required before SAPI registration.';
    Exit;
  end;
  if not CheckOutLoudRegistrationConflict(ErrorText) then Exit;
  try
    ExtractTemporaryFile('registration-policy.ini');
    ExtractTemporaryFile('signing-policy.ini');
    ExtractTemporaryFile('OutLoudPrerequisites.exe');
    ExtractTemporaryFile('prerequisites.ini');
    Helper := ExpandConstant('{tmp}\OutLoudInstallSigning.exe');
    SigningPolicy := ExpandConstant('{tmp}\signing-policy.ini');
    RegistrationPolicy := ExpandConstant('{tmp}\registration-policy.ini');
    Detector := ExpandConstant('{tmp}\OutLoudPrerequisites.exe');
    SigningState := ExpandConstant('{app}\configuration\signing.ini');
    RuntimeReportSerial := RuntimeReportSerial + 1;
    Report := ExpandConstant('{tmp}\registration-result-') + IntToStr(RuntimeReportSerial) + '.ini';
    if FileExists(Report) then RaiseException('Registration result report already exists.');
    if not Exec(Helper, '--register "' + ExpandConstant('{app}') + '" "' + SigningPolicy + '" "' +
      RegistrationPolicy + '" "' + Detector + '" --consented "' + Report + '"',
      ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then
      RaiseException('SAPI registration was not verified. Setup cannot complete; pending registration state requires checked recovery.');
    if not FileExists(Report) or (GetIniString('results', 'schema', '', Report) <> '1') or
      (GetIniString('results', 'registration', '', Report) <> 'verified') or
      (GetIniString('results', 'hive', '', Report) <> 'HKLM') or
      (GetIniString('results', 'view', '', Report) <> '64') or
      (GetIniString('results', 'clsid', '', Report) <> '{#EngineClsid}') or
      (GetIniString('results', 'installation_id', '', Report) <> GetIniString('ownership', 'installation_id', '', SigningState)) or
      (GetIniString('results', 'certificate_sha256', '', Report) <> GetIniString('ownership', 'certificate_sha256', '', SigningState)) or
      (GetIniString('results', 'signed_sha256', '', Report) <> GetSHA256OfFile(ExpandConstant('{app}\bin\OutLoudPocketSAPI.dll'))) then
      RaiseException('SAPI engine post-registration identity was not verified. Setup cannot complete.');
    RegistrationVerified := True;
    Result := True;
  except
    ErrorText := GetExceptionMessage;
  end;
end;
