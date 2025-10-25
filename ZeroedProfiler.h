#pragma once

#include "BaseProfiler.h"
#include "COMPtrHolder.h"
#include "stdafx.h"
#include "ilrewriter.h"
#include <atomic>
#include <string>
#include <map>

// {681AD446-325F-4C07-9BF8-6A199302F62A}
const CLSID CLSID_ZeroedProfiler =
{ 0x681ad446, 0x325f, 0x4c07, { 0x9b, 0xf8, 0x6a, 0x19, 0x93, 0x2, 0xf6, 0x2a } };


class ZeroedProfiler : public BaseProfiler
{
public:
    ZeroedProfiler();
    virtual ~ZeroedProfiler();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // ICorProfilerCallback
    HRESULT STDMETHODCALLTYPE Initialize(IUnknown* pICorProfilerInfoUnk) override;
    HRESULT STDMETHODCALLTYPE Shutdown() override;

    //HRESULT STDMETHODCALLTYPE AssemblyLoadStarted(AssemblyID assemblyId) override;
    //HRESULT STDMETHODCALLTYPE AssemblyLoadFinished(AssemblyID assemblyId, HRESULT hrStatus) override;
    HRESULT STDMETHODCALLTYPE ModuleLoadFinished(ModuleID moduleId, HRESULT hrStatus) override;
    HRESULT STDMETHODCALLTYPE JITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock) override;
    //HRESULT STDMETHODCALLTYPE GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl) override;

    std::atomic<ULONG> m_refCount;
    ICorProfilerInfo4* ClrBridge = nullptr;

    LPCWSTR TargetModule = L"mscorlib.dll";
    LPCWSTR TargetClass = L"System.Reflection.Assembly";
    LPCWSTR TargetMethod = L"Load";
    LPCWSTR TypeName = L"ZeroedProfilerType";
    LPCWSTR ModuleName = L"ZeroedProfiler";
    short numArgsToLog = 1;
    bool targetMethodIsStatic = true;

    // The name of the managed method to be injected into the adopted type
    LPCWSTR ManagedHelperName = L"AssemblyLoadManagedHelper";
    // The name of the callback function in the target dll
    LPCWSTR CallbackMethodName = L"AssemblyLoadHook";

    // Contains a reference to module hooked by this class
    ModuleID ModuleId = 0;
    mdMethodDef pInvokeMethod = mdMethodDefNil;
    mdMethodDef managedHelperMethod = mdMethodDefNil;
    // The method def of the method being hooked
    mdMethodDef targetMethodDef = mdMethodDefNil;

    /*
    *  [DllImport("ZeroedProfiler"), CallingConvention = CallingConvention.StdCall)]
    *  public static extern void SystemReflectionAssemblyLoadCallback(byte* data, int size);
    */
    std::vector<COR_SIGNATURE> pInvokeSignature = {
        IMAGE_CEE_CS_CALLCONV_DEFAULT,     // Calling convention (DEFAULT = static)
        2,                                 // 2 inputs
        ELEMENT_TYPE_VOID,                 // No return
        ELEMENT_TYPE_PTR, ELEMENT_TYPE_U1, // Byte array pointer,
        ELEMENT_TYPE_I4                    // Byte array length
    };

    std::vector<COR_SIGNATURE> managedHelperSignature = {
        IMAGE_CEE_CS_CALLCONV_DEFAULT,        // Calling convention (DEFAULT = static)
        1,                                    // 1 inputs
        ELEMENT_TYPE_VOID,                    // No return
        ELEMENT_TYPE_SZARRAY, ELEMENT_TYPE_U1 // Byte array
    };

    std::vector<COR_SIGNATURE> targetMethodSignature;

private:
    HRESULT SetupTargetMethodSignature(IMetaDataImport* pImport);
    HRESULT DefineCustomType(ModuleID moduleId, mdTypeDef* tdInjectedType, IMetaDataEmit* pEmit, IMetaDataImport* pImport);
    HRESULT GetTargetMethodToken(mdMethodDef* mdTarget, IMetaDataEmit* pEmit, IMetaDataImport* pImport);
    HRESULT AddPInvoke(mdTypeDef td, mdModuleRef modrefTarget, IMetaDataEmit* pEmit);
    HRESULT AddManagedHookMethod(mdTypeDef td, IMetaDataEmit* pEmit, IMetaDataImport* pImport);
    HRESULT SetILForHookMethod(IMetaDataEmit* pEmit, IMetaDataImport* pImport);
    HRESULT RewriteIL(ModuleID moduleID, mdMethodDef methodDef);
};