{ Native Inno form/controls shared by Setup and Uninstall. Paths stay selectable
  and wrap in a scrolling memo; the original path is never shortened or rewritten. }
var
  OutLoudDiagnosticReport, OutLoudDiagnosticFolder: String;

procedure OpenOutLoudDiagnosticFolder(Sender: TObject);
var
  Code: Integer;
begin
  if (OutLoudDiagnosticFolder <> '') and DirExists(OutLoudDiagnosticFolder) then
    if not ShellExec('open', OutLoudDiagnosticFolder, '', '', SW_SHOWNORMAL, ewNoWait, Code) then
      MsgBox('Windows could not open the diagnostic folder. You can copy its full path from this window.', mbError, MB_OK);
end;

procedure ShowOutLoudDiagnostic(const Title, MessageText, ReportPath: String);
var
  Form: TSetupForm;
  Memo: TNewMemo;
  OpenButton, CloseButton: TNewButton;
begin
  Form := CreateCustomForm(ScaleX(640), ScaleY(360), False, False);
  try
    Form.Caption := Title;
    Form.BorderStyle := bsSizeable;
    Form.Position := poScreenCenter;
    Memo := TNewMemo.Create(Form);
    Memo.Parent := Form;
    Memo.SetBounds(ScaleX(16), ScaleY(16), Form.ClientWidth - ScaleX(32), Form.ClientHeight - ScaleY(72));
    Memo.Anchors := [akLeft, akTop, akRight, akBottom];
    Memo.ReadOnly := True;
    Memo.WordWrap := True;
    Memo.ScrollBars := ssVertical;
    Memo.Text := MessageText;
    if (ReportPath <> '') and not DirExists(ReportPath) and (Pos(ReportPath, MessageText) = 0) then
      Memo.Text := Memo.Text + #13#10#13#10 + 'Diagnostic report:' + #13#10 + ReportPath;
    OpenButton := TNewButton.Create(Form);
    OpenButton.Parent := Form;
    OpenButton.Caption := 'Open report folder';
    OpenButton.SetBounds(ScaleX(16), Form.ClientHeight - ScaleY(44), ScaleX(160), ScaleY(28));
    OpenButton.Anchors := [akLeft, akBottom];
    if DirExists(ReportPath) then begin
      OutLoudDiagnosticFolder := ReportPath;
      OpenButton.Caption := 'Open folder';
    end else
      OutLoudDiagnosticFolder := ExtractFileDir(ReportPath);
    OpenButton.Enabled := (ReportPath <> '') and DirExists(OutLoudDiagnosticFolder);
    OpenButton.OnClick := @OpenOutLoudDiagnosticFolder;
    CloseButton := TNewButton.Create(Form);
    CloseButton.Parent := Form;
    CloseButton.Caption := 'Close';
    CloseButton.SetBounds(Form.ClientWidth - ScaleX(112), Form.ClientHeight - ScaleY(44), ScaleX(96), ScaleY(28));
    CloseButton.Anchors := [akRight, akBottom];
    CloseButton.ModalResult := mrOk;
    CloseButton.Default := True;
    CloseButton.Cancel := True;
    Form.ActiveControl := CloseButton;
    Form.ShowModal;
  finally
    OutLoudDiagnosticFolder := '';
    Form.Free;
  end;
end;
