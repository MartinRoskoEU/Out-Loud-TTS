#pragma once
#include "support.hpp"
#include <atomic>
#include <map>

// Entirely in-memory object token. No registry-backed SAPI object is created.
class TestToken final : public ISpObjectToken {
public:
    inline static std::atomic<unsigned> alive{0};
    TestToken() { ++alive; }
    std::map<std::wstring,std::wstring> values;
    IClassFactory* factory = nullptr; // borrowed for the scoped real-SAPI check
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if (!out) { return E_POINTER; } *out = nullptr;
        if (iid!=IID_IUnknown && iid!=__uuidof(ISpObjectToken) && iid!=__uuidof(ISpDataKey)) { return E_NOINTERFACE; }
        *out = static_cast<ISpObjectToken*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) { delete this; } return n; }
    HRESULT STDMETHODCALLTYPE GetStringValue(LPCWSTR key,LPWSTR* out) override {
        if (!out) { return E_POINTER; } *out = nullptr;
        const auto found = values.find(key ? key : L""); if (found==values.end()) { return SPERR_NOT_FOUND; }
        const auto bytes = (found->second.size()+1)*sizeof(wchar_t);
        *out = static_cast<wchar_t*>(CoTaskMemAlloc(bytes)); if (!*out) { return E_OUTOFMEMORY; }
        std::copy(found->second.c_str(),found->second.c_str()+found->second.size()+1,*out); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetData(LPCWSTR,ULONG,const BYTE*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetData(LPCWSTR,ULONG*,BYTE*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetStringValue(LPCWSTR,LPCWSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetDWORD(LPCWSTR,DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDWORD(LPCWSTR,DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OpenKey(LPCWSTR key,ISpDataKey** out) override {
        if (!out) { return E_POINTER; } *out = nullptr;
        if (!key || std::wstring_view(key)!=L"Attributes") { return SPERR_NOT_FOUND; }
        *out = this; AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateKey(LPCWSTR,ISpDataKey**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DeleteKey(LPCWSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DeleteValue(LPCWSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumKeys(ULONG,LPWSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumValues(ULONG,LPWSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetId(LPCWSTR,LPCWSTR,BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetId(LPWSTR* out) override { return GetStringValue(L"TestId",out); }
    HRESULT STDMETHODCALLTYPE GetCategory(ISpObjectTokenCategory**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,DWORD,REFIID iid,void** out) override {
        if (!out) { return E_POINTER; } *out = nullptr;
        if (!factory) { return E_NOTIMPL; }
        auto result = factory->CreateInstance(outer,iid,out);
        if (FAILED(result)) { return result; }
        auto object = static_cast<IUnknown*>(*out);
        ISpObjectWithToken* with = nullptr;
        result = object->QueryInterface(__uuidof(ISpObjectWithToken),reinterpret_cast<void**>(&with));
        if (SUCCEEDED(result)) { result = with->SetObjectToken(this); with->Release(); }
        if (FAILED(result)) { object->Release(); *out = nullptr; }
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetStorageFileName(REFCLSID,LPCWSTR,LPCWSTR,ULONG,LPWSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RemoveStorageFileName(REFCLSID,LPCWSTR,BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Remove(const CLSID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsUISupported(LPCWSTR,void*,ULONG,IUnknown*,BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DisplayUI(HWND,LPCWSTR,LPCWSTR,void*,ULONG,IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE MatchesAttributes(LPCWSTR,BOOL* out) override {
        if (!out) { return E_POINTER; } *out = FALSE; return S_OK;
    }
private:
    ~TestToken() { --alive; }
    std::atomic<ULONG> refs_{1};
};
