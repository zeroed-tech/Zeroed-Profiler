#pragma once

#include "stdafx.h"


// Implementation of the ICorProfilerCallback4 profiler API
class BaseProfiler : public ICorProfilerCallback4
{
public:
	BaseProfiler() {}

	// ICorProfilerCallback
	STDMETHODIMP Initialize(IUnknown* pICorProfilerInfoUnk) override { return S_OK; }
	STDMETHODIMP Shutdown() override { return S_OK; }
	STDMETHODIMP AppDomainCreationStarted(AppDomainID appDomainId) override { return S_OK; }
	STDMETHODIMP AppDomainCreationFinished(AppDomainID appDomainId, HRESULT hrStatus) override { return S_OK; }
	STDMETHODIMP AppDomainShutdownStarted(AppDomainID appDomainId) override { return S_OK; }
	STDMETHODIMP AppDomainShutdownFinished(AppDomainID appDomainId, HRESULT hrStatus) override { return S_OK; }
	STDMETHODIMP AssemblyLoadStarted(AssemblyID assemblyId) override { return S_OK; }
	STDMETHODIMP AssemblyLoadFinished(AssemblyID assemblyId, HRESULT hrStatus) override { return S_OK; }
	STDMETHODIMP AssemblyUnloadStarted(AssemblyID assemblyId) override { return S_OK; }
	STDMETHODIMP AssemblyUnloadFinished(AssemblyID assemblyId, HRESULT hrStatus) override { return S_OK; }
	STDMETHODIMP ModuleLoadStarted(ModuleID moduleId) override { return S_OK; }
	STDMETHODIMP ModuleLoadFinished(ModuleID moduleId, HRESULT hrStatus) override { return S_OK; }
	STDMETHODIMP ModuleUnloadStarted(ModuleID moduleId) override { return S_OK; }
	STDMETHODIMP ModuleUnloadFinished(ModuleID moduleId, HRESULT hrStatus) override { return S_OK; }
	STDMETHODIMP ModuleAttachedToAssembly(ModuleID moduleId, AssemblyID assemblyId) override { return S_OK; }
	STDMETHODIMP ClassLoadStarted(ClassID classId) override { return S_OK; }
	STDMETHODIMP ClassLoadFinished(ClassID classId, HRESULT hrStatus) override { return S_OK; }
	STDMETHODIMP ClassUnloadStarted(ClassID classId) override { return S_OK; }
	STDMETHODIMP ClassUnloadFinished(ClassID classId, HRESULT hrStatus) override { return S_OK; }
	STDMETHODIMP FunctionUnloadStarted(FunctionID functionId) override { return S_OK; }
	STDMETHODIMP JITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock) override { return S_OK; }
	STDMETHODIMP JITCompilationFinished(FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock) override { return S_OK; }
	STDMETHODIMP JITCachedFunctionSearchStarted(FunctionID functionId, BOOL* pbUseCachedFunction) override { return S_OK; }
	STDMETHODIMP JITCachedFunctionSearchFinished(FunctionID functionId, COR_PRF_JIT_CACHE result) override { return S_OK; }
	STDMETHODIMP JITFunctionPitched(FunctionID functionId) override { return S_OK; }
	STDMETHODIMP JITInlining(FunctionID callerId, FunctionID calleeId, BOOL* pfShouldInline) override { return S_OK; }
	STDMETHODIMP ThreadCreated(ThreadID threadId) override { return S_OK; }
	STDMETHODIMP ThreadDestroyed(ThreadID threadId) override { return S_OK; }
	STDMETHODIMP ThreadAssignedToOSThread(ThreadID managedThreadId, DWORD osThreadId) override { return S_OK; }
	STDMETHODIMP RemotingClientInvocationStarted() override { return S_OK; }
	STDMETHODIMP RemotingClientSendingMessage(GUID* pCookie, BOOL fIsAsync) override { return S_OK; }
	STDMETHODIMP RemotingClientReceivingReply(GUID* pCookie, BOOL fIsAsync) override { return S_OK; }
	STDMETHODIMP RemotingClientInvocationFinished() override { return S_OK; }
	STDMETHODIMP RemotingServerReceivingMessage(GUID* pCookie, BOOL fIsAsync) override { return S_OK; }
	STDMETHODIMP RemotingServerInvocationStarted() override { return S_OK; }
	STDMETHODIMP RemotingServerInvocationReturned() override { return S_OK; }
	STDMETHODIMP RemotingServerSendingReply(GUID* pCookie, BOOL fIsAsync) override { return S_OK; }
	STDMETHODIMP UnmanagedToManagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason) override { return S_OK; }
	STDMETHODIMP ManagedToUnmanagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason) override { return S_OK; }
	STDMETHODIMP RuntimeSuspendStarted(COR_PRF_SUSPEND_REASON suspendReason) override { return S_OK; }
	STDMETHODIMP RuntimeSuspendFinished() override { return S_OK; }
	STDMETHODIMP RuntimeSuspendAborted() override { return S_OK; }
	STDMETHODIMP RuntimeResumeStarted() override { return S_OK; }
	STDMETHODIMP RuntimeResumeFinished() override { return S_OK; }
	STDMETHODIMP RuntimeThreadSuspended(ThreadID threadId) override { return S_OK; }
	STDMETHODIMP RuntimeThreadResumed(ThreadID threadId) override { return S_OK; }
	STDMETHODIMP MovedReferences(ULONG cmovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], ULONG cObjectIDRangeLength[]) override { return S_OK; }
	STDMETHODIMP ObjectAllocated(ObjectID objectId, ClassID classId) override { return S_OK; }
	STDMETHODIMP ObjectsAllocatedByClass(ULONG classCount, ClassID classIds[], ULONG objects[]) override { return S_OK; }
	STDMETHODIMP ObjectReferences(ObjectID objectId, ClassID classId, ULONG cObjectRefs, ObjectID objectRefIds[]) override { return S_OK; }
	STDMETHODIMP RootReferences(ULONG cRootRefs, ObjectID rootRefIds[]) override { return S_OK; }
	STDMETHODIMP ExceptionThrown(ObjectID thrownObjectId) override { return S_OK; }
	STDMETHODIMP ExceptionSearchFunctionEnter(FunctionID functionId) override { return S_OK; }
	STDMETHODIMP ExceptionSearchFunctionLeave() override { return S_OK; }
	STDMETHODIMP ExceptionSearchFilterEnter(FunctionID functionId) override { return S_OK; }
	STDMETHODIMP ExceptionSearchFilterLeave() override { return S_OK; }
	STDMETHODIMP ExceptionSearchCatcherFound(FunctionID functionId) override { return S_OK; }
	STDMETHODIMP ExceptionOSHandlerEnter(UINT_PTR __unused) override { return S_OK; }
	STDMETHODIMP ExceptionOSHandlerLeave(UINT_PTR __unused) override { return S_OK; }
	STDMETHODIMP ExceptionUnwindFunctionEnter(FunctionID functionId) override { return S_OK; }
	STDMETHODIMP ExceptionUnwindFunctionLeave() override { return S_OK; }
	STDMETHODIMP ExceptionUnwindFinallyEnter(FunctionID functionId) override { return S_OK; }
	STDMETHODIMP ExceptionUnwindFinallyLeave() override { return S_OK; }
	STDMETHODIMP ExceptionCatcherEnter(FunctionID functionId, ObjectID objectId) override { return S_OK; }
	STDMETHODIMP ExceptionCatcherLeave() override { return S_OK; }
	STDMETHODIMP COMClassicVTableCreated(ClassID wrappedClassId, REFGUID implementedIID, void* pVTable, ULONG cSlots) override { return S_OK; }
	STDMETHODIMP COMClassicVTableDestroyed(ClassID wrappedClassId, REFGUID implementedIID, void* pVTable) override { return S_OK; }
	STDMETHODIMP ThreadNameChanged(ThreadID threadId, ULONG cchName, WCHAR name[]) override { return S_OK; }

	STDMETHODIMP ExceptionCLRCatcherFound() override { return S_OK; }
	STDMETHODIMP ExceptionCLRCatcherExecute() override { return S_OK; }

	// ICorProfilerCallback2
	STDMETHODIMP GarbageCollectionStarted(int cGenerations, BOOL generationCollected[], COR_PRF_GC_REASON reason) override { return S_OK; }
	STDMETHODIMP SurvivingReferences(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], ULONG cObjectIDRangeLength[]) override { return S_OK; }
	STDMETHODIMP GarbageCollectionFinished() override { return S_OK; }
	STDMETHODIMP FinalizeableObjectQueued(DWORD finalizerFlags, ObjectID objectID) override { return S_OK; }
	STDMETHODIMP RootReferences2(ULONG cRootRefs, ObjectID rootRefIds[], COR_PRF_GC_ROOT_KIND rootKinds[], COR_PRF_GC_ROOT_FLAGS rootFlags[], UINT_PTR rootIds[]) override { return S_OK; }
	STDMETHODIMP HandleCreated(GCHandleID handleId, ObjectID initialObjectId) override { return S_OK; }
	STDMETHODIMP HandleDestroyed(GCHandleID handleId) override { return S_OK; }

	// ICorProfilerCallback3
	STDMETHODIMP InitializeForAttach(IUnknown* pCorProfilerInfoUnk, void* pvClientData, UINT cbClientData) override { return S_OK; }
	STDMETHODIMP ProfilerAttachComplete() override { return S_OK; }
	STDMETHODIMP ProfilerDetachSucceeded() override { return S_OK; }

	// ICorProfilerCallback4
	STDMETHODIMP ReJITCompilationStarted(FunctionID functionId, ReJITID rejitId, BOOL fIsSafeToBlock) override { return S_OK; }
	STDMETHODIMP GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl) override { return S_OK; }
	STDMETHODIMP ReJITCompilationFinished(FunctionID functionId, ReJITID rejitId, HRESULT hrStatus, BOOL fIsSafeToBlock) override { return S_OK; }
	STDMETHODIMP ReJITError(ModuleID moduleId, mdMethodDef methodId, FunctionID functionId, HRESULT hrStatus) override { return S_OK; }
	STDMETHODIMP MovedReferences2(ULONG cmovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], SIZE_T cObjectIDRangeLength[]) override { return S_OK; }
	STDMETHODIMP SurvivingReferences2(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], SIZE_T cObjectIDRangeLength[]) override { return S_OK; }
};