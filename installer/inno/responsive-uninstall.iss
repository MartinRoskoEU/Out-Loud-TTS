{ Keep native removal responsive without exposing cancellation or terminating
  the child. The existing forward-only recovery contract is unchanged. }
type
  TOutLoudSecurityAttributes = record
    Size, Descriptor, Inherit: LongWord;
  end;
  TOutLoudMessage = record
    Window, MessageId, WParam: LongWord;
    LParam: Longint;
    Time: LongWord;
    X, Y: Longint;
  end;
var
  UninstallProgressAvailable: Boolean;
  UninstallCompletionForm: TSetupForm;

function OutLoudCreateOutputFile(Path: String; Access, Share: LongWord;
  var Security: TOutLoudSecurityAttributes; Creation, Attributes, Template: LongWord): LongWord;
  external 'CreateFileW@kernel32.dll stdcall';

function OutLoudPeekMessage(var Message: TOutLoudMessage; Window, First, Last, Remove: LongWord): Boolean;
  external 'PeekMessageW@user32.dll stdcall';
function OutLoudTranslateMessage(var Message: TOutLoudMessage): Boolean;
  external 'TranslateMessage@user32.dll stdcall';
function OutLoudDispatchMessage(var Message: TOutLoudMessage): Longint;
  external 'DispatchMessageW@user32.dll stdcall';
function OutLoudDialogMessage(Dialog: LongWord; var Message: TOutLoudMessage): Boolean;
  external 'IsDialogMessageW@user32.dll stdcall';
procedure OutLoudPostQuitMessage(Code: Integer);
  external 'PostQuitMessage@user32.dll stdcall';

procedure RefuseUninstallCompletionClose(Sender: TObject; var CanClose: Boolean);
begin
  CanClose := False;
end;

procedure InitializeUninstallProgressForm;
var
  LabelText: TNewStaticText;
  Progress: TNewProgressBar;
begin
  UninstallProgressAvailable := True;
  UninstallProgressForm.CancelButton.Visible := False;
  { Inno destroys its progress form before finalization. Keep a separate,
    initially hidden completion form; never dereference the destroyed form. }
  UninstallCompletionForm := CreateCustomForm(UninstallProgressForm.ClientWidth,
    UninstallProgressForm.ClientHeight, False, False);
  UninstallCompletionForm.Caption := UninstallProgressForm.Caption;
  UninstallCompletionForm.ClientWidth := UninstallProgressForm.ClientWidth;
  UninstallCompletionForm.ClientHeight := UninstallProgressForm.ClientHeight;
  UninstallCompletionForm.Font.Assign(UninstallProgressForm.Font);
  UninstallCompletionForm.Color := UninstallProgressForm.Color;
  UninstallCompletionForm.Position := poScreenCenter;
  UninstallCompletionForm.OnCloseQuery := @RefuseUninstallCompletionClose;
  LabelText := TNewStaticText.Create(UninstallCompletionForm);
  LabelText.Parent := UninstallCompletionForm;
  LabelText.SetBounds(UninstallProgressForm.PageNameLabel.Left, UninstallProgressForm.PageNameLabel.Top,
    UninstallProgressForm.PageNameLabel.Width, UninstallProgressForm.PageNameLabel.Height);
  LabelText.Font.Style := [fsBold];
  LabelText.Caption := 'Completing removal';
  LabelText := TNewStaticText.Create(UninstallCompletionForm);
  LabelText.Parent := UninstallCompletionForm;
  LabelText.SetBounds(UninstallProgressForm.StatusLabel.Left, UninstallProgressForm.StatusLabel.Top,
    UninstallProgressForm.StatusLabel.Width, UninstallProgressForm.StatusLabel.Height);
  LabelText.AutoSize := False; LabelText.WordWrap := True;
  LabelText.Caption := 'Verifying cleanup. Please wait until removal is complete.';
  Progress := TNewProgressBar.Create(UninstallCompletionForm);
  Progress.Parent := UninstallCompletionForm;
  Progress.SetBounds(UninstallProgressForm.ProgressBar.Left, UninstallProgressForm.ProgressBar.Top,
    UninstallProgressForm.ProgressBar.Width, UninstallProgressForm.ProgressBar.Height);
  Progress.Style := npbstMarquee;
end;

procedure DeinitializeUninstall;
begin
  if UninstallCompletionForm <> nil then UninstallCompletionForm.Free;
end;

function ExecUninstallResponsive(Filename, Parameters, WorkingDirectory: String; var ExitCode: Integer): Boolean;
var
  Startup: TOutLoudStartupInfo;
  ProcessInfo: TOutLoudProcessInfo;
  Message: TOutLoudMessage;
  Security: TOutLoudSecurityAttributes;
  Status, ChildExitCode, Dialog, OutputHandle: LongWord;
  QuitPending: Boolean;
  QuitCode: Integer;
begin
  if (SizeOf(Startup) <> 68) or (SizeOf(Message) <> 28) or (SizeOf(Security) <> 12) then
    RaiseException('Unsupported uninstall process ABI.');
  Security.Size := SizeOf(Security); Security.Descriptor := 0; Security.Inherit := 1;
  OutputHandle := OutLoudCreateOutputFile(UninstallReport + '.output.log', $40000000, 1,
    Security, 1, $80, 0);
  if OutputHandle = $FFFFFFFF then RaiseException('Cannot create cleanup output capture.');
  Startup.Size := SizeOf(Startup);
  Startup.Reserved := 0; Startup.Desktop := 0; Startup.Title := 0;
  Startup.X := 0; Startup.Y := 0; Startup.XSize := 0; Startup.YSize := 0;
  Startup.XChars := 0; Startup.YChars := 0; Startup.FillAttribute := 0;
  Startup.Flags := $101; Startup.ShowWindow := 0;
  Startup.ReservedBytes := 0; Startup.ReservedPointer := 0;
  Startup.StdInput := 0; Startup.StdOutput := OutputHandle; Startup.StdError := OutputHandle;
  QuitPending := False;
  Result := OutLoudCreateProcess(Filename, '"' + Filename + '" ' + Parameters,
    0, 0, True, $08000000, 0, WorkingDirectory, Startup, ProcessInfo);
  if not Result then begin
    ExitCode := DLLGetLastError; OutLoudCloseProcessHandle(OutputHandle); Exit;
  end;
  OutLoudCloseProcessHandle(ProcessInfo.ThreadHandle);
  try
    repeat
      Status := OutLoudWaitForProcess(ProcessInfo.ProcessHandle, 50);
      if (Status <> 0) and (Status <> 258) then RaiseException('Cannot monitor cleanup process.');
      while OutLoudPeekMessage(Message, 0, 0, 0, 1) do begin
        if Message.MessageId = $0012 then begin QuitPending := True; QuitCode := Message.WParam; end
        else begin
          if UninstallProgressAvailable then Dialog := UninstallProgressForm.Handle
          else Dialog := UninstallCompletionForm.Handle;
          if not OutLoudDialogMessage(Dialog, Message) then begin
            OutLoudTranslateMessage(Message); OutLoudDispatchMessage(Message);
          end;
        end;
      end;
    until Status = 0;
    if not OutLoudProcessExitCode(ProcessInfo.ProcessHandle, ChildExitCode) then RaiseException('Cannot read cleanup process result.');
    ExitCode := ChildExitCode;
  finally
    OutLoudWaitForProcess(ProcessInfo.ProcessHandle, $FFFFFFFF);
    OutLoudCloseProcessHandle(ProcessInfo.ProcessHandle);
    OutLoudCloseProcessHandle(OutputHandle);
    if QuitPending then OutLoudPostQuitMessage(QuitCode);
  end;
end;
