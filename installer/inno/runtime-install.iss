{ Offline VC acquisition uses the established pinned native verifier and vendor launcher. }
function PrepareRuntimePrerequisites(var ErrorText: String): Boolean;
var
  Report, Policy, PackagePath, Parameters, InstallReport, VendorLog, Action: String;
  ExitCode: Integer;
begin
  Result := False;
  if not ReadOutLoudPrerequisites(Report) then begin ErrorText := 'Cannot reliably detect runtime requirements.'; Exit; end;
  Action := GetIniString('results', 'vc_action', 'block', Report);
  if Action = 'skip' then begin Result := True; Exit; end;
  if Action <> 'install' then begin ErrorText := 'VC runtime detection refused installation: ' + PrerequisiteDescription(Report, 'vc'); Exit; end;
  try
    ExtractTemporaryFile('runtime-packages.ini');
    ExtractTemporaryFile('vc_redist.x64.exe');
    Policy := ExpandConstant('{tmp}\runtime-packages.ini');
    PackagePath := ExpandConstant('{tmp}\vc_redist.x64.exe');
    RuntimeReportSerial := RuntimeReportSerial + 1;
    InstallReport := ExpandConstant('{tmp}\vc-install-') + IntToStr(RuntimeReportSerial) + '.ini';
    VendorLog := ExpandConstant('{tmp}\vc-vendor-') + IntToStr(RuntimeReportSerial) + '.log';
    Parameters := '--ensure-package "' + Policy + '" vc "' + PackagePath +
      '" --requirements "' + ExpandConstant('{tmp}\prerequisites.ini') +
      '" --runtime-config "' + ExpandConstant('{tmp}\OutLoud.runtimeconfig.json') +
      '" --consented --log "' + VendorLog + '" --report "' + InstallReport + '"';
    if not Exec(ExpandConstant('{tmp}\OutLoudPrerequisites.exe'), Parameters, ExpandConstant('{tmp}'),
      SW_SHOWNORMAL, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) or
      (GetIniString('results', 'installation', '', InstallReport) <> 'verified') then
      RaiseException('The bundled VC x64 redistributable did not complete verified installation. A requested restart must be performed manually before retrying. Report: ' + InstallReport);
    Result := CheckOutLoudPrerequisites;
  except
    ErrorText := GetExceptionMessage;
  end;
end;
