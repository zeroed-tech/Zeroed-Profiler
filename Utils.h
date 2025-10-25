#pragma once
#include "stdafx.h"

#define FAIL_CHECK(hr_call, ...) \
        do { \
            HRESULT hr = (hr_call); \
            if (FAILED(hr)) { \
                spdlog::error(__VA_ARGS__); \
                spdlog::error("Last Error: {}", HrToString(hr)); \
                return hr; \
            } \
        } while (0);

// Converts a HRESULT code into a string
std::string HrToString(HRESULT hr);

std::string WideToUtf8(const std::wstring& wstr);
void ParseRawILStream(LPCBYTE stream, ULONG streamLen);
std::vector<BYTE> HexStringToByteVector(const std::string& hex);
BOOL EndsWith(LPCWSTR wszContainer, LPCWSTR wszProspectiveEnding);
