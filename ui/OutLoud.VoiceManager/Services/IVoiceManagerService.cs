using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Services;

public interface IVoiceManagerService
{
    Task<VoiceSnapshot> ReadAsync();
}
