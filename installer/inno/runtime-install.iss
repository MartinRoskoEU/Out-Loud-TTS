{ A03.4. Acquisition and execution are reachable only through the gated wizard. }
var
  RuntimeConsentPage: TInputOptionWizardPage;
  RuntimeDownloadPage: TDownloadWizardPage;
  RuntimeProgressPage: TOutputProgressWizardPage;
  RuntimeInitiallyCompatible, RuntimeExecuting, RuntimeCancelRequested: Boolean;

function FoundationLifecycleReady(): Boolean;
begin
  { Only the shared native controller may publish/sign/register/remove payload. }
  Result := IsAdmin;
end;

function OfficialRuntimeUrl(const Url: String): Boolean;
begin
  Result := (Pos('https://download.visualstudio.microsoft.com/', Url) = 1) or
    (Pos('https://builds.dotnet.microsoft.com/dotnet/', Url) = 1);
end;

function RuntimeDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := OfficialRuntimeUrl(Url) and not RuntimeCancelRequested;
end;

procedure InitializeRuntimeWizard();
var
  Report, Summary: String;
begin
  ExtractTemporaryFile('runtime-packages.ini');
  RuntimeInitiallyCompatible := False;
  Summary := 'Visual C++ x64 >= ' + GetIniString('requirements', 'vc_minimum', '', ExpandConstant('{tmp}\prerequisites.ini')) +
    ' and .NET 10 Desktop x64 >= 10.0.0 are required.';
  if ReadOutLoudPrerequisites(Report) then begin
    RuntimeInitiallyCompatible := (GetIniString('results', 'vc', '', Report) = 'compatible') and
      (GetIniString('results', 'dotnet', '', Report) = 'compatible');
    Summary := Summary + #13#10 + 'Visual C++: ' + PrerequisiteDescription(Report, 'vc') + #13#10 +
      '.NET Desktop: ' + PrerequisiteDescription(Report, 'dotnet');
  end;
  RuntimeConsentPage := CreateInputOptionPage(wpLicense, 'Microsoft runtime prerequisites',
    'Install required shared Microsoft components', Summary + #13#10 +
    'Compatible installations are kept. Missing components will be downloaded from Microsoft, verified and installed.' + #13#10 +
    'Shared runtimes may remain if Out Loud later fails or is removed. Out Loud will never uninstall them.' + #13#10 +
    'Cancel stops Out Loud setup. A required restart must be performed manually, followed by rerunning setup.', False, False);
  RuntimeConsentPage.Add('I agree to automatic installation of the required Microsoft runtime packages.');
  RuntimeConsentPage.Values[0] := False;
  RuntimeDownloadPage := CreateDownloadPage('Microsoft runtime download', 'Downloading the release-pinned prerequisite', @RuntimeDownloadProgress);
  RuntimeDownloadPage.ShowBaseNameInsteadOfUrl := True;
  RuntimeProgressPage := CreateOutputProgressPage('Microsoft runtime installation', 'Verifying and installing the required shared runtime');
end;

function RuntimeDiskSpaceAvailable(): Boolean;
var
  FreeBytes, TotalBytes: Int64;
begin
  { Conservative preflight; vendor installers remain authoritative for actual space. }
  Result := GetSpaceOnDisk64(ExpandConstant('{tmp}'), FreeBytes, TotalBytes) and
    (FreeBytes >= 536870912);
  if Result then
    Result := GetSpaceOnDisk64(ExpandConstant('{sd}'), FreeBytes, TotalBytes) and
      (FreeBytes >= 1610612736);
end;

function EnsureRuntimeComponent(const Component: String; var ErrorText: String): Boolean;
var
  Report, Policy, Action, Url, FileName, PackagePath, Hash, LabelText, Parameters, InstallReport, Decision, Verification, VendorLog: String;
  ExitCode: Integer;
begin
  Result := False;
  PackagePath := '';
  if not CheckProductConsent then begin
    ErrorText := 'Licenses and Kyutai policy acknowledgment are required before runtime acquisition.';
    Exit;
  end;
  if not ReadOutLoudPrerequisites(Report) then begin
    ErrorText := 'Cannot reliably detect Microsoft runtime prerequisites.';
    Exit;
  end;
  Action := GetIniString('results', Component + '_action', 'block', Report);
  if Action = 'skip' then begin Result := True; Exit; end;
  if Action <> 'install' then begin
    ErrorText := 'Automatic installation is unsafe: ' + PrerequisiteDescription(Report, Component);
    Exit;
  end;
  if RuntimeCancelRequested or not RuntimeConsentPage.Values[0] then begin
    ErrorText := 'Runtime installation cancelled or consent not given. Out Loud was not installed.';
    Exit;
  end;
  if not RuntimeDiskSpaceAvailable then begin
    ErrorText := 'Insufficient or uninspectable disk space: allow at least 512 MiB temporary and 1536 MiB system-drive free space.';
    Exit;
  end;
  Policy := ExpandConstant('{tmp}\runtime-packages.ini');
  Url := GetIniString(Component, 'url', '', Policy);
  FileName := GetIniString(Component, 'filename', '', Policy);
  Hash := GetIniString(Component, 'sha256', '', Policy);
  LabelText := GetIniString(Component, 'name', '', Policy) + ' ' + GetIniString(Component, 'version', '', Policy);
  if not OfficialRuntimeUrl(Url) or (Length(Hash) <> 64) or
    ((Component = 'vc') and (FileName <> 'vc_redist.x64.exe')) or
    ((Component = 'dotnet') and (FileName <> 'windowsdesktop-runtime-' + GetIniString(Component, 'version', '', Policy) + '-win-x64.exe')) then begin
    ErrorText := 'Invalid release-pinned Microsoft package metadata.';
    Exit;
  end;
  PackagePath := ExpandConstant('{tmp}\') + FileName;
  try
    try
      RuntimeDownloadPage.Clear;
      RuntimeDownloadPage.Add(Url, FileName, Hash);
      RuntimeDownloadPage.SetText(LabelText, 'Downloading and checking the pinned SHA-256');
      RuntimeDownloadPage.Show;
      try
        RuntimeDownloadPage.Download;
      finally
        RuntimeDownloadPage.Hide;
      end;
      if RuntimeDownloadPage.AbortedByUser or RuntimeCancelRequested then
        RaiseException('Download cancelled.');
      RuntimeReportSerial := RuntimeReportSerial + 1;
      InstallReport := ExpandConstant('{tmp}\runtime-install-') + Component + '-' + IntToStr(RuntimeReportSerial) + '.ini';
      VendorLog := ExpandConstant('{tmp}\runtime-vendor-') + Component + '-' + IntToStr(RuntimeReportSerial) + '.log';
      if FileExists(InstallReport) or FileExists(VendorLog) then
        RaiseException('Runtime diagnostic destination already exists.');
      Parameters := '--ensure-package "' + Policy + '" ' + Component + ' "' + PackagePath +
        '" --requirements "' + ExpandConstant('{tmp}\prerequisites.ini') +
        '" --runtime-config "' + ExpandConstant('{tmp}\OutLoud.runtimeconfig.json') +
        '" --consented --log "' + VendorLog + '" --report "' + InstallReport + '"';
      RuntimeProgressPage.SetText(LabelText, 'Verifying Microsoft signature, package identity and current runtime; installing only if still required');
      RuntimeProgressPage.SetProgress(0, 1);
      RuntimeProgressPage.Show;
      RuntimeExecuting := True;
      try
        if not Exec(ExpandConstant('{tmp}\OutLoudPrerequisites.exe'), Parameters,
          ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
          RaiseException('Could not launch the verified runtime controller.');
      finally
        RuntimeExecuting := False;
        RuntimeProgressPage.Hide;
      end;
      if not FileExists(InstallReport) or (GetIniString('results', 'schema', '', InstallReport) <> '1') then
        RaiseException('Runtime controller failed to return a valid result; completion is unknown.');
      Decision := GetIniString('results', 'installation', 'failed', InstallReport);
      Verification := GetIniString('results', 'verification', 'unknown', InstallReport);
      Log(LabelText + ': ' + Decision + '; verification=' + Verification + '; vendor exit=' +
        GetIniString('results', 'vendor_exit', 'unknown', InstallReport));
      { Read the complete A03.3 probe again, even when the vendor requested restart. }
      if not ReadOutLoudPrerequisites(Report) then
        RaiseException('Post-install prerequisite detection failed.');
      if Decision = 'restart_required' then
        RaiseException('A Microsoft runtime requires a restart. Out Loud setup is stopped. Save your work, restart Windows manually and rerun setup; detection will skip compatible runtimes.');
      if RuntimeCancelRequested or (Decision = 'cancelled') then
        RaiseException('Runtime installation cancelled. Already installed shared components may remain.');
      if (ExitCode <> 0) or (Decision <> 'verified') or
        (GetIniString('results', Component, '', Report) <> 'compatible') then
        RaiseException('Runtime installation was not verified: ' + Decision + ' / ' + Verification +
          '. ' + PrerequisiteDescription(Report, Component) + ' Vendor log: ' + VendorLog);
      Result := True;
    except
      ErrorText := LabelText + ': ' + GetExceptionMessage;
    end;
  finally
    { Delete only this known package in Inno's private temporary directory. }
    if PackagePath <> '' then
      if FileExists(PackagePath) and not DeleteFile(PackagePath) then
        Log('Could not remove temporary runtime package; Inno cleanup will retry on exit.');
  end;
end;

function PrepareRuntimePrerequisites(var ErrorText: String): Boolean;
begin
  Result := False;
  if not FoundationLifecycleReady then begin
    ErrorText := 'Out Loud installer foundation is blocked: live production registration and lifecycle validation are incomplete.';
    Exit;
  end;
  if not CheckProductConsent then begin ErrorText := 'Product consent is required.'; Exit; end;
  if not CheckOutLoudWindows then begin ErrorText := 'Unsupported Windows system.'; Exit; end;
  if not IsAdmin then begin ErrorText := 'Administrator privileges are required for Microsoft runtime installation.'; Exit; end;
  if not EnsureRuntimeComponent('vc', ErrorText) then Exit;
  if not EnsureRuntimeComponent('dotnet', ErrorText) then Exit;
  Result := not RuntimeCancelRequested and CheckOutLoudPrerequisites;
  if not Result then ErrorText := 'Both Microsoft runtimes must be verified compatible before Out Loud installation.';
end;
