{ A03.5: one standard LicenseFile page, no cached or command-line acceptance. }
procedure InitializeProductConsent();
begin
  WizardForm.LicenseAcceptedRadio.Checked := False;
  WizardForm.LicenseNotAcceptedRadio.Checked := True;
end;

function ProductConsentSatisfied(): Boolean;
begin
  Result := not WizardSilent and WizardForm.LicenseAcceptedRadio.Checked;
end;

function CheckProductConsent(): Boolean;
begin
  Result := ProductConsentSatisfied;
  if not Result then begin
    Log('Out Loud requires interactive acceptance of licenses and acknowledgment of the separate Kyutai use policy.');
    if not WizardSilent then
      MsgBox('Accept the licenses and acknowledge the separate Kyutai use policy before continuing. No Out Loud installation or runtime acquisition is authorized by declining.', mbError, MB_OK);
  end;
end;
