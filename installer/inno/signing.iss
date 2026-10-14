{ A03.6. No certificate/signing operation is reachable while lifecycle gates hold. }
var
  SigningNoticePage: TOutputMsgWizardPage;
  SigningVerified: Boolean;

procedure InitializeSigningWizard();
begin
  SigningVerified := False;
  SigningNoticePage := CreateOutputMsgPage(RuntimeConsentPage.ID, 'Local engine signing',
    'Installation-owned signing certificate',
    'Out Loud will generate a unique local code-signing key and certificate for the SAPI engine.' + #13#10#13#10 +
    'The certificate is stored in LocalMachine Personal and its public certificate in LocalMachine Trusted Root, preserving the working local trust approach for all Windows users. No Trusted Publishers entry is added.' + #13#10#13#10 +
    'This local code-signing trust is not a public CA identity or SmartScreen reputation. The machine key is nonexportable and accessible only to elevated Administrators and SYSTEM.' + #13#10#13#10 +
    'Upgrades reuse the installation-owned machine certificate, including maintenance by a different authorized administrator. Missing, expired or inaccessible keys require explicit recovery.' + #13#10#13#10 +
    'Signing and registration remain disabled in this installer foundation.');
end;

function PrepareOutLoudSigning(var ErrorText: String): Boolean;
var
  Helper, Policy, IdentityReport, Report, OwnerSID, Mode, Parameters: String;
  ExitCode: Integer;
begin
  Result := False;
  SigningVerified := False;
  if not FoundationLifecycleReady then begin
    ErrorText := 'Signing is disabled until production registration and transaction recovery are implemented and validated.';
    Exit;
  end;
  if not CheckProductConsent or not CheckOutLoudPrerequisites then begin
    ErrorText := 'Licenses and compatible runtimes must be verified before signing.';
    Exit;
  end;
  try
    ExtractTemporaryFile('OutLoudInstallSigning.exe');
    ExtractTemporaryFile('signing-policy.ini');
    Helper := ExpandConstant('{tmp}\OutLoudInstallSigning.exe');
    Policy := ExpandConstant('{tmp}\signing-policy.ini');
    RuntimeReportSerial := RuntimeReportSerial + 1;
    IdentityReport := ExpandConstant('{tmp}\signing-identity-') + IntToStr(RuntimeReportSerial) + '.ini';
    Report := ExpandConstant('{tmp}\signing-result-') + IntToStr(RuntimeReportSerial) + '.ini';
    if FileExists(IdentityReport) or FileExists(Report) then
      RaiseException('Signing report destination already exists.');
    { Native machine authority probe; interactive-user identity never selects certificate scope. }
    if not Exec(Helper, '--identity "' + IdentityReport + '"', ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or
      (ExitCode <> 0) or not FileExists(IdentityReport) or (GetIniString('results', 'schema', '', IdentityReport) <> '1') then
      RaiseException('Cannot establish elevated machine installation authority.');
    OwnerSID := GetIniString('results', 'owner_sid', '', IdentityReport);
    if (OwnerSID <> 'S-1-5-32-544') or (GetIniString('results', 'scope', '', IdentityReport) <> 'LocalMachine') or
      (GetIniString('results', 'elevated', '', IdentityReport) <> 'true') then
      RaiseException('Machine signing requires elevated Administrators or SYSTEM authority.');
    Mode := 'fresh';
    if ExistingInstallationDirectory <> '' then Mode := 'upgrade';
    Parameters := '--prepare ' + Mode + ' "' + ExpandConstant('{app}') + '" "' + Policy + '" "' + OwnerSID +
      '" --consented "' + Report + '"';
    if not Exec(Helper, Parameters, ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then
      RaiseException('Local signing did not complete. Registration is prohibited; installation-owned pending state requires recovery.');
    if not FileExists(Report) or (GetIniString('results', 'schema', '', Report) <> '1') or
      (GetIniString('results', 'signing', '', Report) <> 'verified') or
      (GetIniString('results', 'owner_sid', '', Report) <> OwnerSID) or
      (Length(GetIniString('results', 'thumbprint', '', Report)) <> 40) or
      (Length(GetIniString('results', 'certificate_sha256', '', Report)) <> 64) or
      (GetIniString('results', 'signed_sha256', '', Report) <> GetSHA256OfFile(ExpandConstant('{app}\bin\OutLoudPocketSAPI.dll'))) then
      RaiseException('Signed engine identity was not verified. SAPI registration is prohibited.');
    SigningVerified := True;
    Result := True;
  except
    ErrorText := GetExceptionMessage;
  end;
end;
