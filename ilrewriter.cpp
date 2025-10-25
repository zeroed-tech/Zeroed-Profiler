// ==++==
// 
//   Copyright (c) Microsoft Corporation.  All rights reserved.
// 
// ==--==
// 
// #include this cpp file to get the definition and implementation of the ILRewriter
// class.  This class contains a lot of general-purpose IL rewriting functionality,
// which parses an IL stream into a structured linked list of IL instructions.  This
// list can then be manipulated (items added / removed), and then rewritten back into an
// IL stream, with things like branches automatically updated.
// 
// Refer to the C functions at the bottom of this file for examples of how the
// ILRewriter class can be used
// 

#include "ilrewriter.h"
#include "sigparse.inl"
#include <corhlpr.cpp>
#include <fstream>
#include <iomanip>
#include "COMPtrHolder.h"
#include "Utils.h"

// ILRewriter::Export intentionally does a comparison by casting a variable (delta) down
// to an INT8, with data loss being expected and handled. This pragma is required because
// this is compiled with RTC on, and without the pragma, the above cast will generate a
// run-time check on whether we lose data, and cause an unhandled exception (look up
// RTC_Check_4_to_1).  In theory, I should be able to just bracket the Export function
// with the #pragma, but that didn't work.  (Perhaps because all the functions are
// defined inline in the class definition?)
#pragma runtime_checks("", off)

void __fastcall UnmanagedInspectObject(void* pv)
{
	void* pv2 = pv;
}

ILRewriter::ILRewriter(ICorProfilerInfo* pICorProfilerInfo, ICorProfilerFunctionControl* pICorProfilerFunctionControl, ModuleID moduleID, mdToken tkMethod)
	: m_pICorProfilerInfo(pICorProfilerInfo), m_pICorProfilerFunctionControl(pICorProfilerFunctionControl),
	m_moduleId(moduleID), m_tkMethod(tkMethod), m_fGenerateTinyHeader(false),
	m_pOffsetToInstr(NULL), m_pOutputBuffer(NULL), m_pIMethodMalloc(NULL),
	m_pMetaDataImport(NULL), m_pMetaDataEmit(NULL)
{
	m_IL.m_pNext = &m_IL;
	m_IL.m_pPrev = &m_IL;

	m_nInstrs = 0;
}

ILRewriter::~ILRewriter()
{
	ILInstr* p = m_IL.m_pNext;
	while (p != &m_IL)
	{
		ILInstr* t = p->m_pNext;
		delete p;
		p = t;
	}
	delete[] m_pOffsetToInstr;
	delete[] m_pOutputBuffer;

	if (m_pIMethodMalloc)
		m_pIMethodMalloc->Release();
	if (m_pMetaDataImport)
		m_pMetaDataImport->Release();
	if (m_pMetaDataEmit)
		m_pMetaDataEmit->Release();
}

HRESULT ILRewriter::Initialize(mdToken localVarSig) {
	m_tkLocalVarSig = localVarSig;
	return Initialize();
}

HRESULT ILRewriter::Initialize()
{
	// Get metadata interfaces ready

	IfFailRet(m_pICorProfilerInfo->GetModuleMetaData(
		m_moduleId, ofRead | ofWrite, IID_IMetaDataImport, (IUnknown**)&m_pMetaDataImport));

	IfFailRet(m_pMetaDataImport->QueryInterface(IID_IMetaDataEmit, (void**)&m_pMetaDataEmit));

	return S_OK;
}

void ILRewriter::InitializeTiny()
{
	m_tkLocalVarSig = 0;
	m_maxStack = 8;
	m_flags = CorILMethod_TinyFormat;
	m_CodeSize = 0;
	m_fGenerateTinyHeader = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
//
// I M P O R T
//
////////////////////////////////////////////////////////////////////////////////////////////////

HRESULT ILRewriter::Import()
{
	LPCBYTE pMethodBytes;
	ULONG pMethodBytesSize;

	IfFailRet(m_pICorProfilerInfo->GetILFunctionBody(
		m_moduleId, m_tkMethod, &pMethodBytes, &pMethodBytesSize));

	//spdlog::debug("Importing {} - {}", m_moduleId, m_tkMethod);
	//ParseRawILStream(pMethodBytes, pMethodBytesSize);

	COR_ILMETHOD_DECODER decoder((COR_ILMETHOD*)pMethodBytes);

	// Import the header flags
	m_tkLocalVarSig = decoder.GetLocalVarSigTok();
	m_maxStack = decoder.GetMaxStack();
	m_flags = (decoder.GetFlags() & CorILMethod_InitLocals);

	m_CodeSize = decoder.GetCodeSize();

	IfFailRet(ImportIL(decoder.Code));

	return S_OK;
}

HRESULT ILRewriter::ImportIL(LPCBYTE pIL)
{
	m_pOffsetToInstr = new ILInstr * [m_CodeSize + 1];
	IfNullRet(m_pOffsetToInstr);

	ZeroMemory(m_pOffsetToInstr, m_CodeSize * sizeof(ILInstr*));

	// Set the sentinel instruction
	m_pOffsetToInstr[m_CodeSize] = &m_IL;
	m_IL.m_opcode = -1;

	bool fBranch = false;
	unsigned offset = 0;
	while (offset < m_CodeSize)
	{
		unsigned startOffset = offset;
		unsigned opcode = pIL[offset++];

		if (opcode == CEE_PREFIX1)
		{
			if (offset >= m_CodeSize)
			{
				assert(false);
				return COR_E_INVALIDPROGRAM;
			}
			opcode = 0x100 + pIL[offset++];
		}

		if ((CEE_PREFIX7 <= opcode) && (opcode <= CEE_PREFIX2))
		{
			// NOTE: CEE_PREFIX2-7 are currently not supported
			assert(false);
			return COR_E_INVALIDPROGRAM;
		}

		if (opcode >= CEE_COUNT)
		{
			assert(false);
			return COR_E_INVALIDPROGRAM;
		}

		BYTE flags = s_OpCodeFlags[opcode];

		int size = (flags & OPCODEFLAGS_SizeMask);
		if (offset + size > m_CodeSize)
		{
			assert(false);
			return COR_E_INVALIDPROGRAM;
		}

		ILInstr* pInstr = NewILInstr();
		IfNullRet(pInstr);

		pInstr->m_opcode = opcode;

		InsertBefore(&m_IL, pInstr);

		m_pOffsetToInstr[startOffset] = pInstr;

		switch (flags)
		{
		case 0:
			break;
		case 1:
			pInstr->m_Arg8 = *(UNALIGNED INT8*) & (pIL[offset]);
			break;
		case 2:
			pInstr->m_Arg16 = *(UNALIGNED INT16*) & (pIL[offset]);
			break;
		case 4:
			pInstr->m_Arg32 = *(UNALIGNED INT32*) & (pIL[offset]);
			break;
		case 8:
			pInstr->m_Arg64 = *(UNALIGNED INT64*) & (pIL[offset]);
			break;
		case 1 | OPCODEFLAGS_BranchTarget:
			pInstr->m_Arg32 = offset + 1 + *(UNALIGNED INT8*) & (pIL[offset]);
			fBranch = true;
			break;
		case 4 | OPCODEFLAGS_BranchTarget:
			pInstr->m_Arg32 = offset + 4 + *(UNALIGNED INT32*) & (pIL[offset]);
			fBranch = true;
			break;
		case 0 | OPCODEFLAGS_Switch:
		{
			if (offset + sizeof(INT32) > m_CodeSize)
			{
				assert(false);
				return COR_E_INVALIDPROGRAM;
			}

			unsigned nTargets = *(UNALIGNED INT32*) & (pIL[offset]);
			pInstr->m_Arg32 = nTargets;
			offset += sizeof(INT32);

			unsigned base = offset + nTargets * sizeof(INT32);

			for (unsigned iTarget = 0; iTarget < nTargets; iTarget++)
			{
				if (offset + sizeof(INT32) > m_CodeSize)
				{
					assert(false);
					return COR_E_INVALIDPROGRAM;
				}

				pInstr = NewILInstr();
				IfNullRet(pInstr);

				pInstr->m_opcode = CEE_SWITCH_ARG;

				pInstr->m_Arg32 = base + *(UNALIGNED INT32*) & (pIL[offset]);
				offset += sizeof(INT32);

				InsertBefore(&m_IL, pInstr);
			}
			fBranch = true;
			break;
		}
		default:
			assert(false);
			break;
		}
		offset += size;
	}
	assert(offset == m_CodeSize);

	if (fBranch)
	{
		// Go over all control flow instructions and resolve the targets
		for (ILInstr* pInstr = m_IL.m_pNext; pInstr != &m_IL; pInstr = pInstr->m_pNext)
		{
			if (s_OpCodeFlags[pInstr->m_opcode] & OPCODEFLAGS_BranchTarget)
				pInstr->m_pTarget = GetInstrFromOffset(pInstr->m_Arg32);
		}
	}

	return S_OK;
}

ILInstr* ILRewriter::NewILInstr()
{
	m_nInstrs++;
	return new ILInstr();
}

ILInstr* ILRewriter::GetInstrFromOffset(unsigned offset)
{
	ILInstr* pInstr = NULL;

	if (offset <= m_CodeSize)
		pInstr = m_pOffsetToInstr[offset];

	assert(pInstr != NULL);
	return pInstr;
}

void ILRewriter::InsertBefore(ILInstr* pWhere, ILInstr* pWhat)
{
	pWhat->m_pNext = pWhere;
	pWhat->m_pPrev = pWhere->m_pPrev;

	pWhat->m_pNext->m_pPrev = pWhat;
	pWhat->m_pPrev->m_pNext = pWhat;

	AdjustState(pWhat);
}

void ILRewriter::InsertAfter(ILInstr* pWhere, ILInstr* pWhat)
{
	pWhat->m_pNext = pWhere->m_pNext;
	pWhat->m_pPrev = pWhere;

	pWhat->m_pNext->m_pPrev = pWhat;
	pWhat->m_pPrev->m_pNext = pWhat;

	AdjustState(pWhat);
}

void ILRewriter::AdjustState(ILInstr* pNewInstr)
{
	m_maxStack += k_rgnStackPushes[pNewInstr->m_opcode];
}


ILInstr* ILRewriter::GetILList()
{
	return &m_IL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
//
// E X P O R T
//
////////////////////////////////////////////////////////////////////////////////////////////////


HRESULT ILRewriter::Export()
{
	// One instruction produces 6 bytes in the worst case
	unsigned maxSize = m_nInstrs * 6;

	m_pOutputBuffer = new BYTE[maxSize];
	IfNullRet(m_pOutputBuffer);

again:
	BYTE* pIL = m_pOutputBuffer;

	bool fBranch = false;
	unsigned offset = 0;

	// Go over all instructions and produce code for them
	for (ILInstr* pInstr = m_IL.m_pNext; pInstr != &m_IL; pInstr = pInstr->m_pNext)
	{
		pInstr->m_offset = offset;

		unsigned opcode = pInstr->m_opcode;
		if (opcode < CEE_COUNT)
		{
			// CEE_PREFIX1 refers not to instruction prefixes (like tail.), but to
			// the lead byte of multi-byte opcodes. For now, the only lead byte
			// supported is CEE_PREFIX1 = 0xFE.
			if (opcode >= 0x100)
				m_pOutputBuffer[offset++] = CEE_PREFIX1;

			// This appears to depend on an implicit conversion from
			// unsigned opcode down to BYTE, to deliberately lose data and have
			// opcode >= 0x100 wrap around to 0.
			m_pOutputBuffer[offset++] = (opcode & 0xFF);
		}

		assert(pInstr->m_opcode < _countof(s_OpCodeFlags));
		BYTE flags = s_OpCodeFlags[pInstr->m_opcode];
		switch (flags)
		{
		case 0:
			break;
		case 1:
			*(UNALIGNED INT8*) & (pIL[offset]) = pInstr->m_Arg8;
			break;
		case 2:
			*(UNALIGNED INT16*) & (pIL[offset]) = pInstr->m_Arg16;
			break;
		case 4:
			*(UNALIGNED INT32*) & (pIL[offset]) = pInstr->m_Arg32;
			break;
		case 8:
			*(UNALIGNED INT64*) & (pIL[offset]) = pInstr->m_Arg64;
			break;
		case 1 | OPCODEFLAGS_BranchTarget:
			fBranch = true;
			break;
		case 4 | OPCODEFLAGS_BranchTarget:
			fBranch = true;
			break;
		case 0 | OPCODEFLAGS_Switch:
			*(UNALIGNED INT32*) & (pIL[offset]) = pInstr->m_Arg32;
			offset += sizeof(INT32);
			break;
		default:
			assert(false);
			break;
		}
		offset += (flags & OPCODEFLAGS_SizeMask);
	}
	m_IL.m_offset = offset;

	if (fBranch)
	{
		bool fTryAgain = false;
		unsigned switchBase = 0;

		// Go over all control flow instructions and resolve the targets
		for (ILInstr* pInstr = m_IL.m_pNext; pInstr != &m_IL; pInstr = pInstr->m_pNext)
		{
			unsigned opcode = pInstr->m_opcode;

			if (pInstr->m_opcode == CEE_SWITCH)
			{
				switchBase = pInstr->m_offset + 1 + sizeof(INT32) * (pInstr->m_Arg32 + 1);
				continue;
			}
			if (opcode == CEE_SWITCH_ARG)
			{
				// Switch args are special
				*(UNALIGNED INT32*)& (pIL[pInstr->m_offset]) = pInstr->m_pTarget->m_offset - switchBase;
				continue;
			}

			BYTE flags = s_OpCodeFlags[pInstr->m_opcode];

			if (flags & OPCODEFLAGS_BranchTarget)
			{
				int delta = pInstr->m_pTarget->m_offset - pInstr->m_pNext->m_offset;

				switch (flags)
				{
				case 1 | OPCODEFLAGS_BranchTarget:
					// Check if delta is too big to fit into an INT8.
					// 
					// (see #pragma at top of file)
					if ((INT8)delta != delta)
					{
						if (opcode == CEE_LEAVE_S)
						{
							pInstr->m_opcode = CEE_LEAVE;
						}
						else
						{
							assert(opcode >= CEE_BR_S && opcode <= CEE_BLT_UN_S);
							pInstr->m_opcode = opcode - CEE_BR_S + CEE_BR;
							assert(pInstr->m_opcode >= CEE_BR && pInstr->m_opcode <= CEE_BLT_UN);
						}
						fTryAgain = true;
						continue;
					}
					*(UNALIGNED INT8*)& (pIL[pInstr->m_pNext->m_offset - sizeof(INT8)]) = delta;
					break;
				case 4 | OPCODEFLAGS_BranchTarget:
					*(UNALIGNED INT32*) & (pIL[pInstr->m_pNext->m_offset - sizeof(INT32)]) = delta;
					break;
				default:
					assert(false);
					break;
				}
			}
		}

		// Do the whole thing again if we changed the size of some branch targets
		if (fTryAgain)
			goto again;
	}

	unsigned codeSize = offset;
	unsigned totalSize;
	LPBYTE pBody = NULL;
	if (m_fGenerateTinyHeader)
	{
		// Make sure we can fit in a tiny header
		if (codeSize >= 64)
			return E_FAIL;

		totalSize = sizeof(IMAGE_COR_ILMETHOD_TINY) + codeSize;
		pBody = AllocateILMemory(totalSize);
		IfNullRet(pBody);

		BYTE* pCurrent = pBody;

		// Here's the tiny header
		*pCurrent = (BYTE)(CorILMethod_TinyFormat | (codeSize << 2));
		pCurrent += sizeof(IMAGE_COR_ILMETHOD_TINY);

		// And the body
		CopyMemory(pCurrent, m_pOutputBuffer, codeSize);
	}
	else
	{
		// Use FAT header
		unsigned alignedCodeSize = (offset + 3) & ~3;
		//unsigned alignedCodeSize = offset;

		totalSize = sizeof(IMAGE_COR_ILMETHOD_FAT) + alignedCodeSize;
		//spdlog::error("Allocated {} bytes - IMAGE_COR_ILMETHOD_FAT: {}, Code: {}, Alignment: {}, EH: {}, Offset: {}, CodeSizeWithAlign: {}", totalSize, sizeof(IMAGE_COR_ILMETHOD_FAT), offset, alignedCodeSize - offset, (m_nEH ? (sizeof(IMAGE_COR_ILMETHOD_SECT_FAT) + sizeof(IMAGE_COR_ILMETHOD_SECT_EH_CLAUSE_FAT) * m_nEH) : 0), offset, alignedCodeSize);
		pBody = AllocateILMemory(totalSize);
		IfNullRet(pBody);

		BYTE* pCurrent = pBody;

		IMAGE_COR_ILMETHOD_FAT* pHeader = (IMAGE_COR_ILMETHOD_FAT*)pCurrent;
		pHeader->Flags = m_flags | CorILMethod_InitLocals | CorILMethod_FatFormat;
		pHeader->Size = sizeof(IMAGE_COR_ILMETHOD_FAT) / sizeof(DWORD);
		pHeader->MaxStack = m_maxStack;
		pHeader->CodeSize = offset;
		pHeader->LocalVarSigTok = m_tkLocalVarSig;

		pCurrent = (BYTE*)(pHeader + 1);

		CopyMemory(pCurrent, m_pOutputBuffer, codeSize);

		// Zero out padding bytes
		if (alignedCodeSize > codeSize) {
			memset(pCurrent + codeSize, 0, alignedCodeSize - codeSize);
		}

		pCurrent += alignedCodeSize;
		BYTE* startOfRemainder = pCurrent - (alignedCodeSize - offset);
		
	}
	//spdlog::debug("Exporting method");
	//ParseRawILStream(pBody, totalSize);
	IfFailRet(SetILFunctionBody(totalSize, pBody));
	DeallocateILMemory(pBody);

	return S_OK;
}

HRESULT ILRewriter::SetILFunctionBody(unsigned size, LPBYTE pBody)
{
	if (m_pICorProfilerFunctionControl != NULL)
	{
		// We're supplying IL for a rejit, so use the rejit mechanism
		IfFailRet(m_pICorProfilerFunctionControl->SetILFunctionBody(size, pBody));
	}
	else
	{
		// "classic-style" instrumentation on first JIT, so use old mechanism
		IfFailRet(m_pICorProfilerInfo->SetILFunctionBody(m_moduleId, m_tkMethod, pBody));
	}

	return S_OK;
}

LPBYTE ILRewriter::AllocateILMemory(unsigned size)
{
	if (m_pICorProfilerFunctionControl != NULL)
	{
		// We're supplying IL for a rejit, so we can just allocate from
		// the heap
		return new BYTE[size];
	}

	// Else, this is "classic-style" instrumentation on first JIT, and
	// need to use the CLR's IL allocator

	if (FAILED(m_pICorProfilerInfo->GetILFunctionBodyAllocator(m_moduleId, &m_pIMethodMalloc)))
		return NULL;

	return (LPBYTE)m_pIMethodMalloc->Alloc(size);
}

void ILRewriter::DeallocateILMemory(LPBYTE pBody)
{
	if (m_pICorProfilerFunctionControl == NULL)
	{
		// Old-style instrumentation does not provide a way to free up bytes
		return;
	}

	delete[] pBody;
}
