using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace OutLoud.WinUI;

public sealed partial class SettingsView : UserControl
{
    public SettingsView()
    {
        InitializeComponent();
    }

    // These handlers only reproduce Studio Lite's responsive presentation.
    private void AvailableModelsPanel_SizeChanged(object sender, SizeChangedEventArgs args)
    {
        bool narrow = args.NewSize.Width < 480;
        Grid.SetColumnSpan(ModelsHeaderText, narrow ? 2 : 1);
        Grid.SetRow(CheckModelsButton, narrow ? 1 : 0);
        Grid.SetColumn(CheckModelsButton, narrow ? 0 : 1);
        Grid.SetColumnSpan(CheckModelsButton, narrow ? 2 : 1);
        CheckModelsButton.HorizontalAlignment = narrow ? HorizontalAlignment.Left : HorizontalAlignment.Right;
        AvailableModelsHeader.RowSpacing = narrow ? 12 : 0;
    }

    private void RuntimeRow_SizeChanged(object sender, SizeChangedEventArgs args)
    {
        bool narrow = args.NewSize.Width < 480;
        Grid.SetColumnSpan(RuntimeUpdateStatusPanel, narrow ? 2 : 1);
        Grid.SetRow(RuntimeUpdateActions, narrow ? 1 : 0);
        Grid.SetColumn(RuntimeUpdateActions, narrow ? 0 : 1);
        Grid.SetColumnSpan(RuntimeUpdateActions, narrow ? 2 : 1);
        RuntimeUpdateActions.HorizontalAlignment = narrow ? HorizontalAlignment.Left : HorizontalAlignment.Right;
        RuntimeUpdateRow.RowSpacing = narrow ? 12 : 0;
    }

    private void ModelRow_SizeChanged(object sender, SizeChangedEventArgs args)
    {
        if (sender is not Grid row) { return; }
        var identifier = row.Children.OfType<TextBlock>().FirstOrDefault();
        var status = row.Children.OfType<Grid>().FirstOrDefault();
        var action = row.Children.OfType<Button>().FirstOrDefault();
        if (identifier is null || status is null || action is null) { return; }
        bool stacked = args.NewSize.Width < 560;
        row.RowSpacing = stacked ? 8 : 0;
        Grid.SetColumnSpan(identifier, stacked ? 2 : 1);
        Grid.SetColumn(status, stacked ? 0 : 1);
        Grid.SetRow(status, stacked ? 1 : 0);
        Grid.SetColumnSpan(status, stacked ? 2 : 1);
        Grid.SetRowSpan(action, stacked ? 2 : 1);
    }
}
