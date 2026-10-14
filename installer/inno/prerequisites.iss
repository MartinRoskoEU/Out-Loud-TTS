{ A03.3 read-only detection, reused unchanged by the A03.4 acquisition workflow. }
var
  RuntimeReportSerial: Integer;
function CheckOutLoudWindows(): Boolean;
var
  Version: TWindowsVersion;
begin
  GetWindowsVersionEx(Version);
  Result := Version.NTPlatform and (Version.Major = 10) and
    (Version.Minor = 0) and (Version.Build >= 22000) and
    (Version.ProductType = VER_NT_WORKSTATION) and
    (ProcessorArchitecture = paX64);
  if not Result then
    MsgBox('Out Loud supports only Windows 11 on native x64 client systems. ' +
      'Windows 10, ARM64, 32-bit Windows and Windows Server are unsupported.', mbError, MB_OK);
end;

function PrerequisiteDescription(const Report, Key: String): String;
begin
  Result := GetIniString('results', Key, 'detection_failed', Report) + ': ' +
    GetIniString('results', Key + '_detail', 'No reliable diagnostic returned.', Report);
end;

function ReadOutLoudPrerequisites(var Report: String): Boolean;
var
  Parameters: String;
  ExitCode: Integer;
begin
  Result := False;
  try
    ExtractTemporaryFile('OutLoudPrerequisites.exe');
    ExtractTemporaryFile('prerequisites.ini');
    ExtractTemporaryFile('OutLoud.runtimeconfig.json');
    RuntimeReportSerial := RuntimeReportSerial + 1;
    Report := ExpandConstant('{tmp}\outloud-prerequisite-results-') + IntToStr(RuntimeReportSerial) + '.ini';
    { The detector uses CREATE_NEW. An existing report is an error, never reused. }
    if FileExists(Report) then
      RaiseException('Prerequisite report already exists.');
    Parameters := '--requirements "' + ExpandConstant('{tmp}\prerequisites.ini') +
      '" --runtime-config "' + ExpandConstant('{tmp}\OutLoud.runtimeconfig.json') +
      '" --report "' + Report + '"';
    if not Exec(ExpandConstant('{tmp}\OutLoudPrerequisites.exe'), Parameters,
      ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
      RaiseException('Cannot execute the prerequisite detector.');
    if not FileExists(Report) or (GetIniString('results', 'schema', '', Report) <> '1') then
      RaiseException('The prerequisite detector did not return a valid report.');
    Result := ((ExitCode = 0) or (ExitCode = 1)) and
      (GetIniString('results', 'windows', '', Report) = 'compatible');
  except
    MsgBox('Out Loud prerequisite detection failed: ' + GetExceptionMessage, mbError, MB_OK);
    Result := False;
  end;
end;

function CheckOutLoudPrerequisites(): Boolean;
var
  Report: String;
begin
  Result := ReadOutLoudPrerequisites(Report);
  if Result then
    Result := (GetIniString('results', 'vc', '', Report) = 'compatible') and
      (GetIniString('results', 'dotnet', '', Report) = 'compatible');
  if not Result then
    MsgBox('Out Loud prerequisite check:' + #13#10 +
      'Visual C++ x64: ' + PrerequisiteDescription(Report, 'vc') + #13#10 +
      '.NET Desktop x64: ' + PrerequisiteDescription(Report, 'dotnet') + #13#10 +
      'Both prerequisites must be compatible before native publication.', mbError, MB_OK);
end;
