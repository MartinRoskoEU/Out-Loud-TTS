using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;

namespace OutLoud.WinUI;

/// <summary>A noninteractive runtime status image with an accessible automation peer.</summary>
public sealed class RuntimeStatusIndicator : Control
{
    protected override AutomationPeer OnCreateAutomationPeer() => new RuntimeStatusIndicatorAutomationPeer(this);

    private sealed class RuntimeStatusIndicatorAutomationPeer : FrameworkElementAutomationPeer
    {
        public RuntimeStatusIndicatorAutomationPeer(RuntimeStatusIndicator owner) : base(owner) { }

        protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.Image;
        protected override string GetClassNameCore() => nameof(RuntimeStatusIndicator);
    }
}
