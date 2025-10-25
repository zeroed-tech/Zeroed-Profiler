#include "stdafx.h"
#include <sstream>
#include <iomanip>
#include "Utils.h"

std::string HrToString(HRESULT hr)
{
	char* msgBuf = nullptr;
	DWORD size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		hr,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&msgBuf,
		0,
		nullptr);

	std::string message;
	if (size && msgBuf)
	{
		message = msgBuf;
		LocalFree(msgBuf);
	}
	else
	{
		message = "Unknown error parsing HR";
	}
	return message;
}

std::string WideToUtf8(const std::wstring& wstr) {
	if (wstr.empty()) return {};
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
	std::string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, nullptr, nullptr);
	return strTo;
}

BOOL EndsWith(LPCWSTR wszContainer, LPCWSTR wszProspectiveEnding)
{
	size_t cchContainer = wcslen(wszContainer);
	size_t cchEnding = wcslen(wszProspectiveEnding);

	if (cchContainer < cchEnding)
		return FALSE;

	if (cchEnding == 0)
		return FALSE;

	if (_wcsicmp(
		wszProspectiveEnding,
		&(wszContainer[cchContainer - cchEnding])) != 0)
	{
		return FALSE;
	}

	return TRUE;
}

std::string CorExceptionFlagToString(unsigned flags)
{
	std::ostringstream oss;
	bool any = false;
	if (flags == 0) {
		oss << "NONE";
		return oss.str();
	}
	if (flags & COR_ILEXCEPTION_CLAUSE_FILTER) {
		oss << "FILTER ";
		any = true;
	}
	if (flags & COR_ILEXCEPTION_CLAUSE_FINALLY) {
		oss << "FINALLY ";
		any = true;
	}
	if (flags & COR_ILEXCEPTION_CLAUSE_FAULT) {
		oss << "FAULT ";
		any = true;
	}
	if (flags & COR_ILEXCEPTION_CLAUSE_DUPLICATED) {
		oss << "DUPLICATED ";
		any = true;
	}
	if (!any) {
		oss << "UNKNOWN(0x" << std::hex << flags << ")";
	}
	return oss.str();
}

std::vector<BYTE> HexStringToByteVector(const std::string& hex)
{
	std::vector<BYTE> bytes;
	size_t len = hex.length();
	if (len % 2 != 0)
		throw std::invalid_argument("Hex string must have even length");

	for (size_t i = 0; i < len; i += 2) {
		char hi = hex[i];
		char lo = hex[i + 1];
		if (!std::isxdigit(hi) || !std::isxdigit(lo))
			throw std::invalid_argument("Hex string contains non-hex characters");

		BYTE byte = static_cast<BYTE>(std::stoi(hex.substr(i, 2), nullptr, 16));
		bytes.push_back(byte);
	}
	return bytes;
}



void ParseRawILStream(LPCBYTE stream, ULONG streamLen) {

	std::ostringstream aaa;
	for (ULONG i = 0; i < streamLen; ++i) {
		aaa << std::hex << std::setw(2) << std::setfill('0') << (unsigned)stream[i];
	}
	spdlog::debug("Raw Bytes: {}", aaa.str());

	// Interpret the header
	const IMAGE_COR_ILMETHOD_FAT* pFat = reinterpret_cast<const IMAGE_COR_ILMETHOD_FAT*>(stream);

	struct FlagInfo {
		unsigned value;
		const char* name;
	};

	static const FlagInfo flagInfos[] = {
		{ CorILMethod_InitLocals,      "InitLocals" },
		{ CorILMethod_MoreSects,       "MoreSects" },
		{ CorILMethod_CompressedIL,    "CompressedIL" },
		{ CorILMethod_TinyFormat,      "TinyFormat" },
		{ CorILMethod_FatFormat,       "FatFormat" },
		{ CorILMethod_FormatMask,      "FormatMask" }
	};

	std::ostringstream parsedFlags;
	bool any = false;
	for (const auto& fi : flagInfos) {
		if (pFat->Flags & fi.value) {
			parsedFlags << " " << fi.name;
			any = true;
		}
	}
	if (!any) parsedFlags << " (none)";

	spdlog::debug("IMAGE_COR_ILMETHOD_FAT header:");
	spdlog::debug("  Flags:        {:0>10b} {}", pFat->Flags, parsedFlags.str());
	spdlog::debug("  Size:         {}", pFat->Size);
	spdlog::debug("  MaxStack:     {}", pFat->MaxStack);
	spdlog::debug("  CodeSize:     {}", pFat->CodeSize);
	spdlog::debug("  LocalVarSigTok: 0x{:08x}", pFat->LocalVarSigTok);

	// The IL code immediately follows the header
	const BYTE* pCode = stream + (pFat->Size * 4); // Size is in DWORDs
	ULONG codeSize = pFat->CodeSize;

	unsigned alignedCodeSize = (codeSize + 3) & ~3;

	std::ostringstream oss;
	for (ULONG i = 0; i < codeSize; ++i) {
		oss << std::hex << std::setw(2) << std::setfill('0') << (unsigned)pCode[i];
	}
	spdlog::debug("IL Bytes: {}", oss.str());

	// Parse remainder: alignment bytes and EH table
	const BYTE* pRemainder = pCode + codeSize;
	ULONG remainderLen = streamLen - (ULONG)(pRemainder - stream);


	std::ostringstream rem;
	for (ULONG i = 0; i < remainderLen; ++i) {
		rem << std::hex << std::setw(2) << std::setfill('0') << (unsigned)pRemainder[i];
	}
	spdlog::debug("Remaining Bytes: {}", rem.str());

	// Move to the start of the EH section (if any)
	const BYTE* pEH = pRemainder + ((alignedCodeSize - pFat->CodeSize));
	ULONG ehSize = (ULONG)(streamLen - (pEH - stream));
	if (ehSize == 0) {
		spdlog::debug("No EH section present.");
		return;
	}

	// Print EH section header
	BYTE ehKind = *pEH;
	if ((ehKind & CorILMethod_Sect_EHTable) == 0) {
		spdlog::debug("No EH table found.");
		return;
	}

	bool isFat = (ehKind & CorILMethod_Sect_FatFormat) != 0;
	spdlog::debug("EH section kind: 0x{:02x} ({})", ehKind, isFat ? "Fat" : "Small");

	if (isFat) {
		// Fat format
		const IMAGE_COR_ILMETHOD_SECT_FAT* pSectFat = reinterpret_cast<const IMAGE_COR_ILMETHOD_SECT_FAT*>(pEH);
		unsigned dataSize = pSectFat->DataSize;

		int count = (dataSize - sizeof(IMAGE_COR_ILMETHOD_SECT_FAT)) / sizeof(IMAGE_COR_ILMETHOD_SECT_EH_CLAUSE_FAT);

		// The EH clauses start immediately after the IMAGE_COR_ILMETHOD_SECT_FAT header
		const BYTE* pClausesStart = reinterpret_cast<const BYTE*>(pSectFat) + sizeof(IMAGE_COR_ILMETHOD_SECT_FAT);

		spdlog::debug("EH Fat Table: DataSize={}, Clauses={}", dataSize, count);
		for (int i = 0; i < count; i++) {
			const IMAGE_COR_ILMETHOD_SECT_EH_CLAUSE_FAT* clause =
				reinterpret_cast<const IMAGE_COR_ILMETHOD_SECT_EH_CLAUSE_FAT*>(pClausesStart + i * sizeof(IMAGE_COR_ILMETHOD_SECT_EH_CLAUSE_FAT));
			spdlog::debug("  Clause {}: Flags={} TryOffset=0x{:x} TryLength=0x{:x} HandlerOffset=0x{:x} HandlerLength=0x{:x} ClassToken=0x{:x}",
				i, CorExceptionFlagToString(clause->Flags), clause->TryOffset, clause->TryLength, clause->HandlerOffset, clause->HandlerLength, clause->ClassToken);
		}
	}
	else {
		// Small format
		const IMAGE_COR_ILMETHOD_SECT_EH_SMALL* pEHSmall = reinterpret_cast<const IMAGE_COR_ILMETHOD_SECT_EH_SMALL*>(pEH);

		BYTE dataSize = pEHSmall->SectSmall.DataSize;

		int count = (dataSize - sizeof(IMAGE_COR_ILMETHOD_SECT_SMALL) - sizeof(WORD)) / sizeof(IMAGE_COR_ILMETHOD_SECT_EH_CLAUSE_SMALL);

		spdlog::debug("EH Small Table: DataSize={}, Clauses={}", dataSize, count);

		const BYTE* pClausesStart = reinterpret_cast<const BYTE*>(pEHSmall) + sizeof(IMAGE_COR_ILMETHOD_SECT_SMALL) + sizeof(WORD);

		for (int i = 0; i < count; i++) {
			const IMAGE_COR_ILMETHOD_SECT_EH_CLAUSE_SMALL* clause =
				reinterpret_cast<const IMAGE_COR_ILMETHOD_SECT_EH_CLAUSE_SMALL*>(pClausesStart + i * sizeof(IMAGE_COR_ILMETHOD_SECT_EH_CLAUSE_SMALL));
			spdlog::debug(
				"  Clause {}: Flags={} TryOffset=0x{:x} TryLength=0x{:x} HandlerOffset=0x{:x} HandlerLength=0x{:x} ClassToken=0x{:x}",
				i, CorExceptionFlagToString(clause->Flags), clause->TryOffset, clause->TryLength, clause->HandlerOffset, clause->HandlerLength, clause->ClassToken
			);
		}
	}


}