#pragma once
#include <string>
#include <stdexcept>
namespace outloud::installer {
struct SigningError:std::runtime_error { explicit SigningError(const char* code):std::runtime_error(code) {} };
inline void signing_require(bool ok,const char* code) { if(!ok) throw SigningError(code); }
inline bool hexadecimal(const std::wstring& s,size_t length) {
    return s.size()==length && s.find_first_not_of(L"0123456789abcdef")==std::wstring::npos;
}
} // namespace outloud::installer
