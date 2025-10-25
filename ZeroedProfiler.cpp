#include "stdafx.h"
#include "ZeroedProfiler.h"
#include <iostream>
#include <vector>
#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cor.h>
#include <corprof.h>
#include "Utils.h"

ZeroedProfiler::ZeroedProfiler() :
	ClrBridge(NULL),
	m_refCount(0) {
}

ZeroedProfiler::~ZeroedProfiler() {}

HRESULT STDMETHODCALLTYPE ZeroedProfiler::QueryInterface(REFIID riid, void** ppvObject)
{
	if (ppvObject == nullptr) return E_POINTER;

	if (riid == IID_IUnknown || riid == __uuidof(ICorProfilerCallback) ||
		riid == __uuidof(ICorProfilerCallback2) || riid == __uuidof(ICorProfilerCallback3) ||
		riid == __uuidof(ICorProfilerCallback4)) {
		*ppvObject = static_cast<ICorProfilerCallback4*>(this);
		AddRef();
		return S_OK;
	}

	*ppvObject = nullptr;
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ZeroedProfiler::AddRef() {
	return ++m_refCount;
}

ULONG STDMETHODCALLTYPE ZeroedProfiler::Release() {
	ULONG count = --m_refCount;
	if (count == 0) delete this;
	return count;
}

HRESULT STDMETHODCALLTYPE ZeroedProfiler::Initialize(IUnknown* pICorProfilerInfoUnk) {
	HRESULT hr = pICorProfilerInfoUnk->QueryInterface(__uuidof(ICorProfilerInfo4), (void**)&ClrBridge);
	if (FAILED(hr)) {
		spdlog::error("Failed to retrieve interface");
		return hr;
	}
	hr = ClrBridge->SetEventMask(
		COR_PRF_MONITOR_MODULE_LOADS |
		COR_PRF_MONITOR_JIT_COMPILATION |
		COR_PRF_DISABLE_ALL_NGEN_IMAGES);
	if (FAILED(hr)) {
		spdlog::error("Failed to set event mask");
		return hr;
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ZeroedProfiler::Shutdown() {
	spdlog::info("Shutting down");

	if (ClrBridge)
	{
		ClrBridge->Release();
		ClrBridge = nullptr;
	}

	return S_OK;
}

/// <summary>
/// Called whenever a module has finished loading into the target process
/// https://learn.microsoft.com/en-us/dotnet/framework/unmanaged-api/profiling/icorprofilercallback-moduleloadfinished-method
/// </summary>
/// <param name="moduleId"></param>
/// <param name="hrStatus"></param>
/// <returns></returns>
HRESULT STDMETHODCALLTYPE ZeroedProfiler::ModuleLoadFinished(ModuleID moduleId, HRESULT hrStatus) {
	WCHAR moduleName[300];
	ULONG moduleNameLength = 0;
	AssemblyID assemblyID;
	DWORD moduleFlags;

	// Retrieve the name of the module that just loaded
	FAIL_CHECK(ClrBridge->GetModuleInfo2(moduleId, nullptr, _countof(moduleName), &moduleNameLength, moduleName, &assemblyID, &moduleFlags), "Failed to retrieve loaded module details");
	if ((moduleFlags & COR_PRF_MODULE_WINDOWS_RUNTIME)) {
		// Ignore Windows runtime modules
		return S_OK;
	}

	spdlog::info("Loaded Module {}", WideToUtf8(moduleName));	

	// Retrieve details about the assembly containing this module
	/*AppDomainID appDomainID = 0;
	ModuleID modIDDummy;
	FAIL_CHECK(ClrBridge->GetAssemblyInfo(assemblyID, 0, nullptr, nullptr, &appDomainID, &modIDDummy), "Failed to retrieve assembly details");

	WCHAR appDomainName[200];
	ULONG appDomainNameLength;
	ProcessID pProcID;
	BOOL fShared = FALSE;

	FAIL_CHECK(ClrBridge->GetAppDomainInfo(appDomainID, _countof(appDomainName), &appDomainNameLength, appDomainName, &pProcID), "Failed to retrieve application domain details");

	spdlog::info("ModuleLoadFinished for {}, ADName = {}", WideToUtf8(moduleName), WideToUtf8(appDomainName));*/

	// Check to see if the module being loaded contains the function we want to hook
	if (!EndsWith(moduleName, TargetModule))
		return S_OK;

	// Store module ID for later use
	ModuleId = moduleId;

	IMetaDataEmit* pEmit = nullptr;
	IMetaDataImport* pImport = nullptr;

	// Retrieve a metadata emitter so we can manipulate the target assembly
	{
		COMPtrHolder<IUnknown> pUnk;

		FAIL_CHECK(ClrBridge->GetModuleMetaData(moduleId, ofWrite, IID_IMetaDataEmit, &pUnk), "IID_IMetaDataEmit: GetModuleMetaData failed {} Error: {}", WideToUtf8(TargetModule));
		FAIL_CHECK(pUnk->QueryInterface(IID_IMetaDataEmit, (LPVOID*)&pEmit), "IID_IMetaDataEmit: QueryInterface failed {} Error: {}", WideToUtf8(TargetModule));
		pEmit->AddRef();
	}

	// Retrieve a metadata importer so we can manipulate the target assembly
	{
		COMPtrHolder<IUnknown> pUnk;

		FAIL_CHECK(ClrBridge->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport, &pUnk), "IID_IMetaDataImport: GetModuleMetaData failed {}", WideToUtf8(TargetModule));
		FAIL_CHECK(pUnk->QueryInterface(IID_IMetaDataImport, (LPVOID*)&pImport), "IID_IMetaDataImport: QueryInterface failed {}", WideToUtf8(TargetModule));
		pImport->AddRef();
	}

	// Now that the metadata handles are open, we can setup our signatures which will be needed later on

	// Setup target method signature
	spdlog::debug("Building target signature");
	FAIL_CHECK(SetupTargetMethodSignature(pImport), "Failed to build target method signature");

	// Get the metadata token of the target function
	spdlog::debug("Getting reference to target method {}.{}", WideToUtf8(TargetClass), WideToUtf8(TargetMethod));
	FAIL_CHECK(GetTargetMethodToken(&targetMethodDef, pEmit, pImport), "Failed to resolve target method");

	// Define a new type which will house our helper functions
	mdTypeDef tdInjectedType;
	spdlog::debug("Injecting custom type \"{}\" into {}", WideToUtf8(TypeName), WideToUtf8(TargetModule));
	FAIL_CHECK(DefineCustomType(moduleId, &tdInjectedType, pEmit, pImport), "Failed to inject type into target module");

	// Generate the metadata signature for a new module
	mdModuleRef mrZeroedProfilerReference;
	spdlog::debug("Adding reference to module {}", WideToUtf8(ModuleName));
	FAIL_CHECK(pEmit->DefineModuleRef(ModuleName, &mrZeroedProfilerReference), "DefineModuleRef against the native profiler DLL failed");

	// Add a new pinvoke method to our custom type
	spdlog::debug("Adding PInvoke reference AssemblyLoadCallback");
	AddPInvoke(tdInjectedType, mrZeroedProfilerReference, pEmit);

	// Define our hook method
	spdlog::debug("Adding method definition for {}", WideToUtf8(ManagedHelperName));
	AddManagedHookMethod(tdInjectedType, pEmit, pImport);

	// Set the IL for the hook method
	spdlog::debug("Setting IL for {}", WideToUtf8(ManagedHelperName));
	FAIL_CHECK(SetILForHookMethod(pEmit, pImport), "Failed to set IL for {}", WideToUtf8(ManagedHelperName));
	
	return S_OK;
}


HRESULT ZeroedProfiler::JITCompilationStarted(FunctionID functionID, BOOL fIsSafeToBlock)
{
	mdToken methodDef;
	ClassID classID;
	ModuleID moduleID;

	// Resolve function module ID and method def token so hooks can determine if they should handle this compilation
	FAIL_CHECK(ClrBridge->GetFunctionInfo(functionID, &classID, &moduleID, &methodDef), "GetFunctionInfo failed"); // Theres not much we can do about this failing, bail out		

	if (moduleID == ModuleId && methodDef == targetMethodDef) {
		spdlog::debug("JITCompilationStarted for {}", WideToUtf8(TargetMethod));

		FAIL_CHECK(RewriteIL(moduleID, methodDef), "SetILForManagedHelper failed for {}", WideToUtf8(TargetMethod));
	}

	return S_OK;
}

// Uses the general-purpose ILRewriter class to import original
// IL, rewrite it, and send the result to the CLR
HRESULT ZeroedProfiler::RewriteIL(ModuleID moduleID, mdMethodDef methodDef)
{
	ILRewriter* rewriter = new ILRewriter(ClrBridge, NULL, moduleID, methodDef);

	FAIL_CHECK(rewriter->Initialize(), "Failed to initalise IL rewriter");
	FAIL_CHECK(rewriter->Import(), "Failed to import existing method IL");

	spdlog::debug("Injecting redirect to managed helper", methodDef, managedHelperMethod);
	ILInstr* pFirstOriginalInstr = rewriter->GetILList()->m_pNext;
	ILInstr* pNewInstr = NULL;

	static OPCODE staticOrder[4] = { CEE_LDARG_0, CEE_LDARG_1, CEE_LDARG_2, CEE_LDARG_3 };

	if (targetMethodIsStatic && numArgsToLog > 4 || !targetMethodIsStatic && numArgsToLog > 3) {
		spdlog::error("Too many arguments");
		return E_FAIL;
	}

	for (int i = 0; i < numArgsToLog; i++) {
		pNewInstr = rewriter->NewILInstr();
		pNewInstr->m_opcode = staticOrder[i];
		rewriter->InsertBefore(pFirstOriginalInstr, pNewInstr);
	}

	// call MgdEnteredFunction32/64 (may be via memberRef or methodDef)
	pNewInstr = rewriter->NewILInstr();
	pNewInstr->m_opcode = CEE_CALL;
	pNewInstr->m_Arg32 = managedHelperMethod;
	rewriter->InsertBefore(pFirstOriginalInstr, pNewInstr);

	FAIL_CHECK(rewriter->Export(), "Failed to export modified IL");

	return S_OK;
}

/// <summary>
/// Setup the signature for the method we want to hook. This will be used to resolve the method later on
/// Take special care to ensure the parameters and return types are correct
/// </summary>
/// <param name="pImport"></param>
/// <returns></returns>
HRESULT ZeroedProfiler::SetupTargetMethodSignature(IMetaDataImport* pImport) {
	mdTypeDef typeDef;
	HRESULT hr;

	BYTE compressedToken[4];
	FAIL_CHECK(pImport->FindTypeDefByName(TargetClass, mdTypeDefNil, &typeDef), "Failed to find class '{}'. Error: {}", WideToUtf8(TargetClass), HrToString(hr));

	ULONG tokenLen = CorSigCompressToken(typeDef, compressedToken);

	targetMethodSignature = {
		IMAGE_CEE_CS_CALLCONV_DEFAULT, // Calling convention (DEFAULT = static)
		1,                             // 1 parameter
		ELEMENT_TYPE_CLASS,            // Return type - Class
	};

	targetMethodSignature.insert(targetMethodSignature.end(), compressedToken, compressedToken + tokenLen);
	targetMethodSignature.push_back(ELEMENT_TYPE_SZARRAY);// parameter type: byte[]
	targetMethodSignature.push_back(ELEMENT_TYPE_U1);     // array element type = byte

	return S_OK;
}

/// <summary>
/// Inject a new type into the target module. This custom type will house our hook function and any custom managed functions we may want to use
/// </summary>
/// <returns></returns>
HRESULT ZeroedProfiler::DefineCustomType(ModuleID moduleId, mdTypeDef* tdInjectedType, IMetaDataEmit* pEmit, IMetaDataImport* pImport) {
	// Retrieve a reference to System.Object so we can configure our new type to extend it
	mdTypeRef systemObjectRef = mdTypeRefNil;
	FAIL_CHECK(pEmit->DefineTypeRefByName(moduleId, L"System.Object", &systemObjectRef), "Failed to retrieve System.Object");

	// Define a new type so we have somewhere to safely store all our methods
	FAIL_CHECK(pEmit->DefineTypeDef(TypeName, tdSealed | tdAbstract | tdPublic, systemObjectRef, nullptr, tdInjectedType), "Error encountered whilst injecting {}", WideToUtf8(TypeName));

	// Now that we have setup a new type, we can inject any helper methods in we may need
	// We don't need any for this demo but this can be handy if you want to run custom managed code as part of your hook
	return S_OK;
}

// Resolve the MethodDef for the method we want to hook
HRESULT ZeroedProfiler::GetTargetMethodToken(mdMethodDef* mdTarget, IMetaDataEmit* pEmit, IMetaDataImport* pImport)
{
	mdTypeDef typeDef;
	spdlog::debug("Looking for class {}", WideToUtf8(TargetClass));
	FAIL_CHECK(pImport->FindTypeDefByName(TargetClass, mdTypeDefNil, &typeDef), "Failed to find class '{}'", WideToUtf8(TargetClass));

	spdlog::debug("Found class, looking for method {}", WideToUtf8(TargetMethod));


	FAIL_CHECK(pImport->FindMethod(typeDef, TargetMethod, targetMethodSignature.data(), targetMethodSignature.size(), mdTarget), "FindMethod with signature failed for {}.{}", WideToUtf8(TargetClass), WideToUtf8(TargetMethod));

	spdlog::debug("Found {} - {}.{}", WideToUtf8(TargetModule), WideToUtf8(TargetClass), WideToUtf8(TargetMethod));

	return S_OK;
}

// Creates a PInvoke method to inject into our custom type
HRESULT ZeroedProfiler::AddPInvoke(mdTypeDef td, mdModuleRef mr, IMetaDataEmit* pEmit) {
	spdlog::debug("Injecting pinvoke {}", WideToUtf8(CallbackMethodName));
	if (!pEmit) {
		spdlog::error("IMetaDataEmit pointer is null or invalid!");
		return E_FAIL;
	}

	// Define a new public static pinvoke method in our custom type. This will represent our pinvoke back to native land
	FAIL_CHECK(pEmit->DefineMethod(td,
		CallbackMethodName,
		~mdAbstract & (mdStatic | mdPublic | mdPinvokeImpl), 
		pInvokeSignature.data(), 
		pInvokeSignature.size(), 
		0, 
		miPreserveSig, 
		&pInvokeMethod), "Failed in DefineMethod when creating P/Invoke method {}", WideToUtf8(CallbackMethodName));

	FAIL_CHECK(pEmit->DefinePinvokeMap(pInvokeMethod,
		pmCallConvStdcall | pmNoMangle, 
		CallbackMethodName, 
		mr), "Failed in DefinePinvokeMap when creating P/Invoke method {}", WideToUtf8(CallbackMethodName));
	
	return S_OK;
}

/// <summary>
/// Define a new method in our custom type. Note that this method will have no body until we set one in the next function
/// </summary>
HRESULT ZeroedProfiler::AddManagedHookMethod(mdTypeDef td, IMetaDataEmit* pEmit, IMetaDataImport* pImport)
{
	spdlog::debug("Defining method {}", WideToUtf8(ManagedHelperName), WideToUtf8(TypeName), td);
	if (!pEmit) {
		spdlog::error("IMetaDataEmit pointer is null or invalid!");
		return E_FAIL;
	}

	FAIL_CHECK(pEmit->DefineMethod(td,
		ManagedHelperName, 
		mdStatic | mdPublic, 
		managedHelperSignature.data(),
		managedHelperSignature.size(),
		0,
		miIL | miNoInlining, 
		&managedHelperMethod), "Failed to add managed hook method to custom type");

	// As our method will be calling native code, we'll need to add give it the SecuritySafeCriticalAttribute
	// https://learn.microsoft.com/en-us/dotnet/api/system.security.securitysafecriticalattribute?view=netframework-4.8.1

	// Resolve SecuritySafeCriticalAttribute type
	mdTypeDef tdSafeCritical;
	mdMethodDef mdSafeCritical = mdMethodDefNil;
	FAIL_CHECK(pImport->FindTypeDefByName(L"System.Security.SecuritySafeCriticalAttribute", mdTokenNil, &tdSafeCritical),
		"FindTypeDefByName(System.Security.SecuritySafeCriticalAttribute) failed");

	COR_SIGNATURE sigSafeCriticalCtor[] = {
		IMAGE_CEE_CS_CALLCONV_HASTHIS,
		0x00,                               // Number of arguments
		ELEMENT_TYPE_VOID,                  // Return type
	};

	FAIL_CHECK(pImport->FindMember(tdSafeCritical, L".ctor", sigSafeCriticalCtor, sizeof(sigSafeCriticalCtor), &mdSafeCritical),
		"FindMember(System.Security.SecuritySafeCriticalAttribute..ctor) failed");

	// Attach SecuritySafeCriticalAttribute attribute to target method
	mdToken tkCustomAttribute;
	FAIL_CHECK(pEmit->DefineCustomAttribute(managedHelperMethod, mdSafeCritical, NULL, 0, &tkCustomAttribute),
		"DefineMethod - Failed to define custom attribute");
}

// Dynamiclly define the IL for the managed helper. This IL should call the pinvoke target, passing any params needed
// then return following the call
HRESULT ZeroedProfiler::SetILForHookMethod(IMetaDataEmit* pEmit, IMetaDataImport* pImport)
{
	/*------Type Vars------*/
	mdTypeDef tdBytes = mdTypeDefNil; // System.Byte

	/*------Type Resolution------*/
	// Resolve System.Byte
	FAIL_CHECK(pImport->FindTypeDefByName(L"System.Byte", mdTypeDefNil, &tdBytes),
		"Failed to resolve System.Byte");

	/*------Locals Signature Generation------*/
	std::vector<COR_SIGNATURE> localSig = {
	  IMAGE_CEE_CS_CALLCONV_LOCAL_SIG,      // SIG_LOCAL_SIG
	  0x02,                                 // Max stack size
	  ELEMENT_TYPE_PTR, ELEMENT_TYPE_U1,    // System.Byte*
	  ELEMENT_TYPE_PINNED,                  // Prevent GC/moving of value
	  ELEMENT_TYPE_SZARRAY, ELEMENT_TYPE_U1 // System.Byte[]
	};
	
	mdSignature tkLocalSig = mdTokenNil;
	FAIL_CHECK(pEmit->GetTokenFromSig(localSig.data(), (ULONG)localSig.size(), &tkLocalSig), "Failed in create local sig");

	/*------Method Vars------*/
	ILRewriter rewriter(ClrBridge, NULL, ModuleId, managedHelperMethod);
	rewriter.Initialize(tkLocalSig);

	ILInstr* pFirstOriginalInstr = rewriter.GetILList()->m_pNext;
	ILInstr* tgt_IL_000C = rewriter.NewILInstr();
	ILInstr* tgt_IL_0011 = rewriter.NewILInstr();
	ILInstr* tgt_IL_001A = rewriter.NewILInstr();

	// 0000: nop
	ILInstr* IL_0000 = rewriter.NewILInstr();
	IL_0000->m_opcode = CEE_NOP;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0000);

	// 0001: nop
	ILInstr* IL_0001 = rewriter.NewILInstr();
	IL_0001->m_opcode = CEE_NOP;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0001);

	// 0002: ldarg.0
	ILInstr* IL_0002 = rewriter.NewILInstr();
	IL_0002->m_opcode = CEE_LDARG_0;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0002);

	// 0003: dup
	ILInstr* IL_0003 = rewriter.NewILInstr();
	IL_0003->m_opcode = CEE_DUP;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0003);

	// 0004: stloc.1
	ILInstr* IL_0004 = rewriter.NewILInstr();
	IL_0004->m_opcode = CEE_STLOC_1;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0004);

	// 0005: brfalse.s IL_000C: ldc.i4.0
	ILInstr* IL_0005 = rewriter.NewILInstr();
	IL_0005->m_opcode = CEE_BRFALSE_S;
	IL_0005->m_pTarget = tgt_IL_000C;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0005);

	// 0007: ldloc.1
	ILInstr* IL_0007 = rewriter.NewILInstr();
	IL_0007->m_opcode = CEE_LDLOC_1;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0007);

	// 0008: ldlen
	ILInstr* IL_0008 = rewriter.NewILInstr();
	IL_0008->m_opcode = CEE_LDLEN;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0008);

	// 0009: conv.i4
	ILInstr* IL_0009 = rewriter.NewILInstr();
	IL_0009->m_opcode = CEE_CONV_I4;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0009);

	// 000A: brtrue.s IL_0011: ldloc.1
	ILInstr* IL_000A = rewriter.NewILInstr();
	IL_000A->m_opcode = CEE_BRTRUE_S;
	IL_000A->m_pTarget = tgt_IL_0011;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_000A);

	// 000C: ldc.i4.0
	tgt_IL_000C->m_opcode = CEE_LDC_I4_0;
	rewriter.InsertBefore(pFirstOriginalInstr, tgt_IL_000C);

	// 000D: conv.u
	ILInstr* IL_000D = rewriter.NewILInstr();
	IL_000D->m_opcode = CEE_CONV_U;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_000D);

	// 000E: stloc.0
	ILInstr* IL_000E = rewriter.NewILInstr();
	IL_000E->m_opcode = CEE_STLOC_0;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_000E);

	// 000F: br.s IL_001A: nop
	ILInstr* IL_000F = rewriter.NewILInstr();
	IL_000F->m_opcode = CEE_BR_S;
	IL_000F->m_pTarget = tgt_IL_001A;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_000F);

	// 0011: ldloc.1
	tgt_IL_0011->m_opcode = CEE_LDLOC_1;
	rewriter.InsertBefore(pFirstOriginalInstr, tgt_IL_0011);

	// 0012: ldc.i4.0
	ILInstr* IL_0012 = rewriter.NewILInstr();
	IL_0012->m_opcode = CEE_LDC_I4_0;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0012);

	// 0013: ldelema System.Byte
	ILInstr* IL_0013 = rewriter.NewILInstr();
	IL_0013->m_opcode = CEE_LDELEMA;
	IL_0013->m_Arg32 = tdBytes; // TypeRef System.Byte
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0013);

	// 0018: conv.u
	ILInstr* IL_0018 = rewriter.NewILInstr();
	IL_0018->m_opcode = CEE_CONV_U;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0018);

	// 0019: stloc.0
	ILInstr* IL_0019 = rewriter.NewILInstr();
	IL_0019->m_opcode = CEE_STLOC_0;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0019);

	// 001A: nop
	tgt_IL_001A->m_opcode = CEE_NOP;
	rewriter.InsertBefore(pFirstOriginalInstr, tgt_IL_001A);

	// 001B: ldloc.0
	ILInstr* IL_001B = rewriter.NewILInstr();
	IL_001B->m_opcode = CEE_LDLOC_0;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_001B);

	// 001C: ldarg.0
	ILInstr* IL_001C = rewriter.NewILInstr();
	IL_001C->m_opcode = CEE_LDARG_0;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_001C);

	// 001D: ldlen
	ILInstr* IL_001D = rewriter.NewILInstr();
	IL_001D->m_opcode = CEE_LDLEN;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_001D);

	// 001E: conv.i4
	ILInstr* IL_001E = rewriter.NewILInstr();
	IL_001E->m_opcode = CEE_CONV_I4;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_001E);

	// 001F: call System.Void ZeroedMethods::AssemblyLoadHook(System.Byte*,System.Int32)
	ILInstr* IL_001F = rewriter.NewILInstr();
	IL_001F->m_opcode = CEE_CALL;
	IL_001F->m_Arg32 = pInvokeMethod; // MethodDef System.Void ZeroedMethods::AssemblyLoadHook(System.Byte*,System.Int32)
	rewriter.InsertBefore(pFirstOriginalInstr, IL_001F);

	// 0024: nop
	ILInstr* IL_0024 = rewriter.NewILInstr();
	IL_0024->m_opcode = CEE_NOP;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0024);

	// 0025: nop
	ILInstr* IL_0025 = rewriter.NewILInstr();
	IL_0025->m_opcode = CEE_NOP;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0025);

	// 0026: ldnull
	ILInstr* IL_0026 = rewriter.NewILInstr();
	IL_0026->m_opcode = CEE_LDNULL;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0026);

	// 0027: stloc.1
	ILInstr* IL_0027 = rewriter.NewILInstr();
	IL_0027->m_opcode = CEE_STLOC_1;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0027);

	// 0028: nop
	ILInstr* IL_0028 = rewriter.NewILInstr();
	IL_0028->m_opcode = CEE_NOP;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0028);

	// 0029: ret
	ILInstr* IL_0029 = rewriter.NewILInstr();
	IL_0029->m_opcode = CEE_RET;
	rewriter.InsertBefore(pFirstOriginalInstr, IL_0029);

	FAIL_CHECK(rewriter.Export(), "Failed to export IL");

	return S_OK;
}



extern "C" void STDAPICALLTYPE AssemblyLoadHook(byte* rawAssembly, int assemblyLength)
{
	std::ostringstream oss;
	for (int i = 0; i < assemblyLength; ++i) {
		oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(rawAssembly[i]);
	}
	spdlog::info("[AssembyLoad] Bytes: {}", oss.str());
}