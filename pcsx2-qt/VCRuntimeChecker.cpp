// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "common/RedtapeWindows.h"
#include <shellapi.h>

#include "fmt/format.h"

#define MAKE_VERSION64(v0, v1, v2, v3) \
	(static_cast<DWORD64>(v3) | (static_cast<DWORD64>(v2) << 16) | (static_cast<DWORD64>(v1) << 32) | \
		(static_cast<DWORD64>(v0) << 48))
#define VERSION64_PART(v, p) (static_cast<WORD>(((v) >> (48 - ((p) * 16))) & 0xFFFFu))

// Minimum version is 14.38.33135.0.
static constexpr DWORD64 MIN_VERSION = MAKE_VERSION64(14, 38, 33135, 0);
static constexpr const char* DOWNLOAD_URL = "https://aka.ms/vs/17/release/vc_redist.x64.exe";

struct VCRuntimeCheckObject
{
	VCRuntimeCheckObject()
	{
		const HMODULE crt_handle = GetModuleHandleW(L"msvcp140.dll");
		if (!crt_handle)
			return;

		const HANDLE heap = GetProcessHeap();
		DWORD filename_length = MAX_PATH;
		LPWSTR filename = static_cast<LPWSTR>(HeapAlloc(heap, 0, filename_length));
		if (!filename)
			return;

		for (;;)
		{
			DWORD len = GetModuleFileNameW(crt_handle, filename, filename_length);
			if (len == filename_length && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				filename_length *= 2;
				if (filename_length >= 4 * 1024)
					return;
				LPWSTR new_filename = static_cast<LPWSTR>(HeapReAlloc(heap, 0, filename, filename_length));
				if (!new_filename)
				{
					HeapFree(heap, 0, filename);
					return;
				}
				filename = new_filename;
				continue;
			}

			break;
		}

		const DWORD version_size = GetFileVersionInfoSizeExW(0, filename, nullptr);
		LPVOID version_block;
		if (version_size == 0 || !(version_block = HeapAlloc(heap, 0, version_size)))
		{
			HeapFree(heap, 0, filename);
			return;
		}

		VS_FIXEDFILEINFO* fi;
		UINT fi_size;
		if (!GetFileVersionInfoExW(0, filename, 0, version_size, version_block) ||
			!VerQueryValueW(version_block, L"\\", reinterpret_cast<LPVOID*>(&fi), &fi_size))
		{
			HeapFree(heap, 0, version_block);
			HeapFree(heap, 0, filename);
			return;
		}

		const DWORD64 version = MAKE_VERSION64((fi->dwFileVersionMS >> 16) & 0xFFFFu, fi->dwFileVersionMS & 0xFFFFu,
			(fi->dwFileVersionLS >> 16) & 0xFFFFu, fi->dwFileVersionLS & 0xFFFFu);

		HeapFree(heap, 0, version_block);
		HeapFree(heap, 0, filename);

		if (version >= MIN_VERSION)
			return;

		// fmt is self-contained, hopefully it'll be okay.
		char message[512];
		const auto fmt_result =
			fmt::format_to_n(message, sizeof(message),
				"Your Microsoft Visual C++ Runtime appears to be too old for this build of PCSX2.\n\n"
				"Your version: {}.{}.{}.{}\n"
				"Required version: {}.{}.{}.{}\n\n"
				"You can download the latest version from {}.\n\n"
				"Do you want to exit and download this version now?\n"
				"If you select No, PCSX2 will likely crash.",
				VERSION64_PART(version, 0), VERSION64_PART(version, 1), VERSION64_PART(version, 2),
				VERSION64_PART(version, 3), VERSION64_PART(MIN_VERSION, 0), VERSION64_PART(MIN_VERSION, 1),
				VERSION64_PART(MIN_VERSION, 2), VERSION64_PART(MIN_VERSION, 3), DOWNLOAD_URL);
		message[(fmt_result.size > (sizeof(message) - 1)) ? (sizeof(message) - 1) : fmt_result.size] = 0;

		if (MessageBoxA(NULL, message, "Old Visual C++ Runtime Detected", MB_ICONERROR | MB_YESNO) == IDNO)
			return;

		if (!ShellExecuteA(NULL, "open", DOWNLOAD_URL, nullptr, nullptr, SW_SHOWNORMAL))
			MessageBoxA(NULL, "ShellExecuteA() failed, you may need to manually open the URL.", "Error", MB_OK);

		TerminateProcess(GetCurrentProcess(), 0xFFFFFFFF);
	}
};

// We have to use a special object which gets initialized before all other global objects, because those might use the
// CRT and go kaboom. Yucky, but gets the job done.
#pragma optimize("", off)
#pragma warning(disable : 4075) // warning C4075: initializers put in unrecognized initialization area
#pragma init_seg(".CRT$XCT")
VCRuntimeCheckObject s_vcruntime_checker;
