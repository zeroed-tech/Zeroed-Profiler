#include <windows.h>
#include <objbase.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/wincolor_sink.h>
#include "spdlog/sinks/msvc_sink.h"
#include "ZeroedProfiler.h"


// Externally defined CLSID
extern "C" const GUID CLSID_ZeroedProfiler;

class ZeroedProfilerClassFactory : public IClassFactory {
public:
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppvObject = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    // IClassFactory
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override {
        if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;
        ZeroedProfiler* profiler = new ZeroedProfiler();

        return profiler->QueryInterface(riid, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override { return S_OK; }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    // Setup logger
    // Create MSVC sink (logs to Output window in Visual Studio)
    auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

    // Create console sink (colored output to stdout)
    auto wincolor_sink = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>();
    msvc_sink->set_level(spdlog::level::debug);
    wincolor_sink->set_level(spdlog::level::debug);

    // Combine sinks
    std::vector<spdlog::sink_ptr> sinks{ msvc_sink, wincolor_sink};
    auto logger = std::make_shared<spdlog::logger>("dual_sink_logger", sinks.begin(), sinks.end());

    // Set as default logger
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%H:%M:%S] [%^%L%$] %v");
    spdlog::set_level(spdlog::level::info);

    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    static ZeroedProfilerClassFactory factory;

    if (rclsid == CLSID_ZeroedProfiler) {
        return factory.QueryInterface(riid, ppv);
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow(void) {
    return S_FALSE;
}

