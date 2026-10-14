{ One machine-wide instance, discovered by the stable Inno AppId, never by CLSID. }
const
  OutLoudUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\OutLoud_is1';
var
  ExistingInstallationDirectory: String;

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
  Directory, Helper, Report: String;
  ExitCode: Integer;
begin
  Result := False;
  try
    if not ReadInstalledOutLoud(ErrorText) then Exit;
    Directory := RemoveBackslashUnlessRoot(ExpandFileName(WizardDirValue));
    WizardForm.DirEdit.Text := Directory; { Application files and native checks use the same resolved path. }
    if (ExistingInstallationDirectory <> '') and not SameText(Directory, ExistingInstallationDirectory) then
      RaiseException('Update Out Loud in its recorded installation directory. Relocation and parallel installations are unsupported.');
    if (ExistingInstallationDirectory = '') and FileExists(Directory + '\configuration\signing.ini') and
      not FileExists(Directory + '\configuration\maintenance.pending') then
      RaiseException('This directory contains ownership state without an Out Loud installer record. Checked recovery is required; setup will not adopt it.');
    ExtractTemporaryFile('OutLoudInstallSigning.exe');
    Helper := ExpandConstant('{tmp}\OutLoudInstallSigning.exe');
    RuntimeReportSerial := RuntimeReportSerial + 1;
    Report := ExpandConstant('{tmp}\installation-inspect-') + IntToStr(RuntimeReportSerial) + '.ini';
    if FileExists(Report) then RaiseException('Installation inspection report already exists.');
    if not Exec(Helper, '--installation-inspect "' + Directory + '" "' + Report + '"',
      ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) or
      not FileExists(Report) or (GetIniString('results', 'schema', '', Report) <> '1') or
      (GetIniString('results', 'location', '', Report) <> 'secure_candidate') then
      RaiseException('The installation directory is insecure, occupied or unsupported. Choose an empty directory on a fixed local NTFS drive under an Administrators/SYSTEM-protected parent, without junctions or ordinary-user write/replace permissions. Setup will not alter unrelated directory permissions.');
    Result := True;
  except
    ErrorText := GetExceptionMessage;
  end;
end;
