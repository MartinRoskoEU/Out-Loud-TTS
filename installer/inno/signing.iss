var SigningNoticePage: TOutputMsgWizardPage;
procedure InitializeSigningWizard();
begin
  SigningNoticePage := CreateOutputMsgPage(wpSelectTasks, 'Windows Certificate and Speech Engine Registration',
    'Local signing and Windows speech integration',
    'Out Loud requires a locally trusted certificate to sign its speech engine components and integrate with Windows SAPI 5.' + #13#10#13#10 +
    'During installation, Out Loud will create a local certificate, configure the required Windows trust settings, digitally sign its components, and register the speech engine.' + #13#10#13#10 +
    'These operations require administrator privileges and modify the Windows certificate store.');
end;
