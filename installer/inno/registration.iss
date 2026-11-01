function VoiceRegistrationPreflightMessage(Status: String): String;
begin
  if Status = 'unverified' then
    Result := 'An existing Out Loud voice registration is incomplete or does not match the supported format. Setup cannot safely use or replace it.'
  else if Status = 'unsupported_scope' then
    Result := 'An Out Loud voice is registered for an individual user or in an unsupported Windows speech location. Setup cannot safely combine it with this machine-wide installation.'
  else
    Result := 'Windows speech registrations could not be fully checked. Setup cannot safely continue.';
  Result := Result + #13#10#13#10 + 'No voice registrations were changed by this check.';
end;

function CheckOutLoudRegistrationConflict(var ErrorText: String): Boolean;
var
  Helper, Report, Status, VoiceStatus: String;
  ExitCode: Integer;
begin
  Result := False;
  try
    ExtractTemporaryFile('OutLoudInstallSigning.exe');
    Helper := ExpandConstant('{tmp}\OutLoudInstallSigning.exe');
    RuntimeReportSerial := RuntimeReportSerial + 1;
    Report := ExpandConstant('{tmp}\registration-inspect-') + IntToStr(RuntimeReportSerial) + '.ini';
    OutLoudDiagnosticReport := Report;
    if FileExists(Report) then RaiseException('Registration inspection report already exists.');
    if not Exec(Helper, '--registration-inspect "' + ExpandConstant('{app}') + '" "' + Report + '"',
      ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) or
      not FileExists(Report) or (GetIniString('results', 'schema', '', Report) <> '1') then
      RaiseException('Windows speech registrations could not be checked. Setup cannot safely continue. No voice registrations were changed.');
    Log('Windows speech registration inspection report: ' + Report);
    VoiceStatus := GetIniString('results', 'voice_registration', '', Report);
    if VoiceStatus <> 'clear' then
      RaiseException(VoiceRegistrationPreflightMessage(VoiceStatus) + #13#10#13#10 + 'Diagnostic report: ' + Report);
    Status := GetIniString('results', 'registration', '', Report);
    if (Status <> 'absent') and (Status <> 'owned_candidate') then
      RaiseException('Windows already has a conflicting or incomplete Out Loud speech engine registration. Setup cannot safely replace it. No registration was overwritten.' + #13#10#13#10 + 'Diagnostic report: ' + Report);
    Result := True;
  except
    ErrorText := GetExceptionMessage;
  end;
end;
