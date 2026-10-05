#pragma once
#include <Windows.h>
namespace outloud::sapi {
// Separate from the working Chatterbox engine/token; coexistence is intentional.
inline constexpr CLSID engine_clsid{0xf38c3398,0x3552,0x43b9,{0x83,0x50,0x7d,0xd3,0x01,0x32,0x7d,0x70}};
inline constexpr wchar_t clsid_text[] = L"{F38C3398-3552-43B9-8350-7DD301327D70}";
inline constexpr wchar_t token_name[] = L"OutLoud_Pocket_GnomeMale_Development";
inline constexpr wchar_t voice_name[] = L"Out Loud - Gnome Male (Development)";
}
