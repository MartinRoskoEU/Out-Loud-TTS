{ Start a child directly: Exec's synchronous wrapper disables all wizard windows.
  Keep its real process handle, pump the progress page and always join on exit. }
type
  TOutLoudStartupInfo = record
    Size, Reserved, Desktop, Title, X, Y, XSize, YSize, XChars, YChars,
      FillAttribute, Flags: LongWord;
    ShowWindow, ReservedBytes: Word;
    ReservedPointer, StdInput, StdOutput, StdError: LongWord;
  end;
  TOutLoudProcessInfo = record
    ProcessHandle, ThreadHandle, ProcessId, ThreadId: LongWord;
  end;
var
  ResponsiveCommandRunning, ResponsiveCommandCancelable: Boolean;
  ResponsiveCommandReport: String;
  MaintenanceCancelButton: TNewButton;

function OutLoudCreateProcess(ApplicationName, CommandLine: String;
  ProcessAttributes, ThreadAttributes: LongWord; InheritHandles: Boolean;
  CreationFlags, Environment: LongWord; CurrentDirectory: String;
  var Startup: TOutLoudStartupInfo; var ProcessInfo: TOutLoudProcessInfo): Boolean;
  external 'CreateProcessW@kernel32.dll stdcall';
function OutLoudWaitForProcess(Handle, Milliseconds: LongWord): LongWord;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function OutLoudProcessExitCode(Handle: LongWord; var ExitCode: LongWord): Boolean;
  external 'GetExitCodeProcess@kernel32.dll stdcall';
function OutLoudCloseProcessHandle(Handle: LongWord): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

procedure RequestMaintenanceCancellation(Sender: TObject);
begin
  if not ResponsiveCommandCancelable or RuntimeCancelRequested then Exit;
  if (ResponsiveCommandReport <> '') and FileExists(ResponsiveCommandReport + '.critical') then Exit;
  if MsgBox('Cancel installation? Setup will stop at a safe point and finish any required cleanup.',
    mbConfirmation, MB_YESNO) <> IDYES then Exit;
  { The helper may have crossed its last safe point while confirmation was open. }
  if (ResponsiveCommandReport <> '') and FileExists(ResponsiveCommandReport + '.critical') then Exit;
  if (ResponsiveCommandReport <> '') and
    not SaveStringToFile(ResponsiveCommandReport + '.cancel', 'cancel', False) then begin
    MsgBox('The cancellation request could not be written. Installation will continue.', mbError, MB_OK);
    Exit;
  end;
  RuntimeCancelRequested := True;
  MaintenanceCancelButton.Enabled := False;
  InstallationProgressPage.SetText('Cancellation requested. Waiting for safe cleanup...', '');
end;

function ExecResponsive(Filename, Parameters, WorkingDirectory: String; var ExitCode: Integer): Boolean;
var
  Startup: TOutLoudStartupInfo;
  ProcessInfo: TOutLoudProcessInfo;
  Status, ChildExitCode: LongWord;
begin
  { This repository currently builds the 32-bit Inno host. Refuse another ABI. }
  if SizeOf(Startup) <> 68 then RaiseException('Unsupported installer process ABI.');
  Startup.Size := SizeOf(Startup);
  Startup.Reserved := 0; Startup.Desktop := 0; Startup.Title := 0;
  Startup.X := 0; Startup.Y := 0; Startup.XSize := 0; Startup.YSize := 0;
  Startup.XChars := 0; Startup.YChars := 0; Startup.FillAttribute := 0;
  Startup.Flags := 1;
  Startup.ShowWindow := 0; Startup.ReservedBytes := 0; Startup.ReservedPointer := 0;
  Startup.StdInput := 0; Startup.StdOutput := 0; Startup.StdError := 0;
  Result := OutLoudCreateProcess(Filename, '"' + Filename + '" ' + Parameters,
    0, 0, False, $08000000, 0, WorkingDirectory, Startup, ProcessInfo);
  if not Result then begin
    ExitCode := DLLGetLastError;
    ResponsiveCommandCancelable := False;
    MaintenanceCancelButton.Visible := False;
    Exit;
  end;
  OutLoudCloseProcessHandle(ProcessInfo.ThreadHandle);
  ResponsiveCommandRunning := True;
  try
    repeat
      if ResponsiveCommandCancelable and FileExists(ResponsiveCommandReport + '.critical') then begin
        ResponsiveCommandCancelable := False;
        MaintenanceCancelButton.Enabled := False;
        InstallationProgressPage.SetText('Installing and completing Out Loud...',
          'This step must finish before Setup can close.');
      end;
      Status := OutLoudWaitForProcess(ProcessInfo.ProcessHandle, 50);
      if (Status <> 0) and (Status <> 258) then RaiseException('Cannot monitor installation process.');
      InstallationProgressPage.Animate;
    until Status = 0;
    if not OutLoudProcessExitCode(ProcessInfo.ProcessHandle, ChildExitCode) then
      RaiseException('Cannot read installation process result.');
    ExitCode := ChildExitCode;
  finally
    { Never leave the helper running while Inno tears down its input directory. }
    OutLoudWaitForProcess(ProcessInfo.ProcessHandle, $FFFFFFFF);
    OutLoudCloseProcessHandle(ProcessInfo.ProcessHandle);
    ResponsiveCommandRunning := False;
    ResponsiveCommandCancelable := False;
    MaintenanceCancelButton.Visible := False;
  end;
end;
