#pragma once
#include "stdafx.h"

#include "stdio.h"


// Forward declarations for COM interfaces
struct ICorProfilerInfo;
struct ICorProfilerFunctionControl;
struct IMethodMalloc;
struct IMetaDataImport;
struct IMetaDataEmit;

// IL instruction node
struct ILInstr
{
    ILInstr* m_pNext;
    ILInstr* m_pPrev;

    unsigned m_opcode;
    unsigned m_offset;

    union
    {
        ILInstr* m_pTarget;
        INT8     m_Arg8;
        INT16    m_Arg16;
        INT32    m_Arg32;
        INT64    m_Arg64;
    };
};

// Exception handling clause
struct EHClause
{
    CorExceptionFlag            m_Flags;
    ILInstr* m_pTryBegin;
    ILInstr* m_pTryEnd;
    ILInstr* m_pHandlerBegin;    // First instruction inside the handler
    ILInstr* m_pHandlerEnd;      // Last instruction inside the handler
    union
    {
        DWORD                   m_ClassToken;   // use for type-based exception handlers
        ILInstr* m_pFilter;      // use for filter-based exception handlers (COR_ILEXCEPTION_CLAUSE_FILTER is set)
    };
};

#undef IfFailRet
#define IfFailRet(EXPR) do { HRESULT hr = (EXPR); if(FAILED(hr)) { return (hr); } } while (0)

#undef IfNullRet
#define IfNullRet(EXPR) do { if ((EXPR) == NULL) return E_OUTOFMEMORY; } while (0)


typedef enum
{
#define OPDEF(c,s,pop,push,args,type,l,s1,s2,ctrl) c,
#include "opcode.def"
#undef OPDEF
    CEE_COUNT,
    CEE_SWITCH_ARG, // special internal instructions
} OPCODE;

#define OPCODEFLAGS_SizeMask        0x0F
#define OPCODEFLAGS_BranchTarget    0x10
#define OPCODEFLAGS_Switch          0x20

static const BYTE s_OpCodeFlags[] =
{
#define InlineNone           0
#define ShortInlineVar       1
#define InlineVar            2
#define ShortInlineI         1
#define InlineI              4
#define InlineI8             8
#define ShortInlineR         4
#define InlineR              8
#define ShortInlineBrTarget  1 | OPCODEFLAGS_BranchTarget
#define InlineBrTarget       4 | OPCODEFLAGS_BranchTarget
#define InlineMethod         4
#define InlineField          4
#define InlineType           4
#define InlineString         4
#define InlineSig            4
#define InlineRVA            4
#define InlineTok            4
#define InlineSwitch         0 | OPCODEFLAGS_Switch

#define OPDEF(c,s,pop,push,args,type,l,s1,s2,flow) args,
#include "opcode.def"
#undef OPDEF

#undef InlineNone
#undef ShortInlineVar
#undef InlineVar
#undef ShortInlineI
#undef InlineI
#undef InlineI8
#undef ShortInlineR
#undef InlineR
#undef ShortInlineBrTarget
#undef InlineBrTarget
#undef InlineMethod
#undef InlineField
#undef InlineType
#undef InlineString
#undef InlineSig
#undef InlineRVA
#undef InlineTok
#undef InlineSwitch
    0,                              // CEE_COUNT
    4 | OPCODEFLAGS_BranchTarget,   // CEE_SWITCH_ARG
};

static int k_rgnStackPushes[] = {

#define OPDEF(c,s,pop,push,args,type,l,s1,s2,ctrl) \
	{ push },

#define Push0    0
#define Push1    1
#define PushI    1
#define PushI4   1
#define PushR4   1
#define PushI8   1
#define PushR8   1
#define PushRef  1
#define VarPush  1          // Test code doesn't call vararg fcns, so this should not be used

#include "opcode.def"

#undef Push0   
#undef Push1   
#undef PushI   
#undef PushI4  
#undef PushR4  
#undef PushI8  
#undef PushR8  
#undef PushRef 
#undef VarPush 
#undef OPDEF
};

// ILRewriter class
class ILRewriter
{
private:
    ICorProfilerInfo* m_pICorProfilerInfo;
    ICorProfilerFunctionControl* m_pICorProfilerFunctionControl;

    ModuleID    m_moduleId;
    mdToken     m_tkMethod;

    mdToken     m_tkLocalVarSig;
    unsigned    m_maxStack;
    unsigned    m_flags;
    bool        m_fGenerateTinyHeader;

    ILInstr m_IL; // Double linked list of all il instructions


    // Helper table for importing.  Sparse array that maps BYTE offset of beginning of an
    // instruction to that instruction's ILInstr*.  BYTE offsets that don't correspond
    // to the beginning of an instruction are mapped to NULL.
    ILInstr** m_pOffsetToInstr;
    unsigned    m_CodeSize;

    unsigned    m_nInstrs;

    BYTE* m_pOutputBuffer;

    IMethodMalloc* m_pIMethodMalloc;

    IMetaDataImport* m_pMetaDataImport;
    IMetaDataEmit* m_pMetaDataEmit;

public:
    ILRewriter(ICorProfilerInfo* pICorProfilerInfo, ICorProfilerFunctionControl* pICorProfilerFunctionControl, ModuleID moduleID, mdToken tkMethod);
    ~ILRewriter();

    HRESULT Initialize(mdToken localVarSig);
    HRESULT Initialize();
    void InitializeTiny();

    HRESULT Import();
    HRESULT ImportIL(LPCBYTE pIL);
    
    ILInstr* NewILInstr();
    ILInstr* GetInstrFromOffset(unsigned offset);
    void InsertBefore(ILInstr* pWhere, ILInstr* pWhat);
    void InsertAfter(ILInstr* pWhere, ILInstr* pWhat);
    void AdjustState(ILInstr* pNewInstr);
    ILInstr* GetILList();

    HRESULT Export();
    HRESULT SetILFunctionBody(unsigned size, LPBYTE pBody);
    LPBYTE AllocateILMemory(unsigned size);
    void DeallocateILMemory(LPBYTE pBody);

    WCHAR* GetNameFromToken(mdToken tk);
    ILInstr* NewLDC(LPVOID p);
    void PrintImportedMethodInfo();
};