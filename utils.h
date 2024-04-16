#pragma once
#define PHNT_VERSION PHNT_WIN10_22H2
#include <phnt_windows.h>
#include <phnt.h>
#include <ntexapi.h>
#include <ntpsapi.h>
#include <stdint.h>

#include <share.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef enum _WINDOWINFOCLASS {
	WindowProcess,
	WindowThread,
	WindowActiveWindow,
	WindowFocusWindow,
	WindowIsHung,
	WindowClientBase,
	WindowIsForegroundThread,
#ifdef FE_IME
	WindowDefaultImeWindow,
	WindowDefaultInputContext,
#endif
} WINDOWINFOCLASS;

const WCHAR* BadProcessnameList[] =
{
	L"ollydbg.exe",
	L"ida.exe",
	L"ida64.exe",
	L"idag.exe",
	L"idag64.exe",
	L"idaw.exe",
	L"idaw64.exe",
	L"idaq.exe",
	L"idaq64.exe",
	L"idau.exe",
	L"idau64.exe",
	L"scylla.exe",
	L"scylla_x64.exe",
	L"scylla_x86.exe",
	L"protection_id.exe",
	L"x64dbg.exe",
	L"x32dbg.exe",
	L"windbg.exe",
	L"reshacker.exe",
	L"ImportREC.exe",
	L"IMMUNITYDEBUGGER.EXE",
	L"devenv.exe",
	L"cheatengine-x86_64-SSE4-AVX2.exe",
	L"cheatengine.exe",
};

const WCHAR* BadWindowTextList[] =
{
	L"OLLYDBG",
	L"ida",
	L"disassembly",
	L"scylla",
	L"Debug",
	L"[CPU",
	L"Immunity",
	L"WinDbg",
	L"x32dbg",
	L"x64dbg",
	L"Import reconstructor"
	L"Cheat Engine",
	L"Cheat Engine 7.3",
};

const WCHAR* BadWindowClassList[] =
{
	L"OLLYDBG",
	L"Zeta Debugger",
	L"Rock Debugger",
	L"ObsidianGUI",
	L"ID", //Immunity Debugger
	L"WinDbgFrameClass", //WinDBG
	L"idawindow",
	L"tnavbox",
	L"idaview",
	L"tgrzoom"
};

// temporary fix until we nop out all the arxan "self healing" spots in the executable
struct intactChecksumHook
{
	uint64_t* functionAddress;
	uint8_t buffer[7];
};

struct intactBigChecksumHook
{
	uint64_t* functionAddress;
	uint8_t buffer[7+3];
};

struct splitChecksumHook
{
	uint64_t* functionAddress;
	uint8_t buffer[8];
};

bool RtlUnicodeStringContains(PUNICODE_STRING Str, PUNICODE_STRING SubStr, BOOLEAN CaseInsensitive)
{
	if (Str == nullptr || SubStr == nullptr || Str->Length < SubStr->Length)
		return false;

	const USHORT numCharsDiff = (Str->Length - SubStr->Length) / sizeof(WCHAR);
	UNICODE_STRING slice = *Str;
	slice.Length = SubStr->Length;

	for (USHORT i = 0; i <= numCharsDiff; ++i, ++slice.Buffer, slice.MaximumLength -= sizeof(WCHAR))
	{
		if (RtlEqualUnicodeString(&slice, SubStr, CaseInsensitive))
			return true;
	}
	return false;
}

bool IsWindowClassNameBad(PUNICODE_STRING className)
{
	if (className == nullptr || className->Length == 0 || className->Buffer == nullptr)
		return false;

	UNICODE_STRING badWindowClassName;
	for (int i = 0; i < _countof(BadWindowClassList); i++)
	{
		RtlInitUnicodeString(&badWindowClassName, const_cast<PWSTR>(BadWindowClassList[i]));
		if (RtlUnicodeStringContains(className, &badWindowClassName, TRUE))
			return true;
	}
	return false;
}

bool IsWindowNameBad(PUNICODE_STRING windowName)
{
	if (windowName == nullptr || windowName->Length == 0 || windowName->Buffer == nullptr)
		return false;

	UNICODE_STRING badWindowName;
	for (int i = 0; i < _countof(BadWindowTextList); i++)
	{
		RtlInitUnicodeString(&badWindowName, const_cast<PWSTR>(BadWindowTextList[i]));
		if (RtlUnicodeStringContains(windowName, &badWindowName, TRUE))
			return true;
	}
	return false;
}

bool IsWindowBad(HWND hWnd)
{
	DECLARE_UNICODE_STRING_SIZE(ClassName, 256);
	DECLARE_UNICODE_STRING_SIZE(WindowText, 512);
	
	typedef int(__stdcall* NtUserGetClassName_t)(HWND hwnd, BOOL real, UNICODE_STRING* name);
	NtUserGetClassName_t NtUserGetClassName = (NtUserGetClassName_t)GetProcAddress(GetModuleHandle("win32u.dll"), "NtUserGetClassName");

	typedef int(__stdcall* NtUserInternalGetWindowText_t)(HWND hwnd, WCHAR* text, INT count);
	NtUserInternalGetWindowText_t NtUserInternalGetWindowText = (NtUserInternalGetWindowText_t)GetProcAddress(GetModuleHandle("win32u.dll"), "NtUserInternalGetWindowText");

	ClassName.Length = (USHORT)NtUserGetClassName(hWnd, FALSE, &ClassName) * sizeof(WCHAR);
	ClassName.Buffer[ClassName.Length / sizeof(WCHAR)] = UNICODE_NULL;
	if (IsWindowClassNameBad(&ClassName))
		return true;

	WindowText.Length = (USHORT)NtUserInternalGetWindowText(hWnd, WindowText.Buffer, (INT)(WindowText.MaximumLength / sizeof(WCHAR))) * sizeof(WCHAR);
	WindowText.Buffer[WindowText.Length / sizeof(WCHAR)] = UNICODE_NULL;
	return IsWindowNameBad(&WindowText);
}

void FilterHwndList(HWND* phwndFirst, PULONG pcHwndNeeded)
{
	for (UINT i = 0; i < *pcHwndNeeded; i++)
	{
		if (phwndFirst[i] != nullptr && IsWindowBad(phwndFirst[i]))
		{
			if (i == 0)
			{
				// Find the first HWND that belongs to a different process (i + 1, i + 2... may still be ours)
				for (UINT j = i + 1; j < *pcHwndNeeded; j++)
				{
					if (phwndFirst[j] != nullptr && !IsWindowBad(phwndFirst[j]))
					{
						phwndFirst[i] = phwndFirst[j];
						break;
					}
				}
			}
			else
			{
				phwndFirst[i] = phwndFirst[i - 1]; //just override with previous
			}
		}
	}
}


inline void SetBits(unsigned long& dw, int lowBit, int bits, int newValue)
{
	int mask = (1 << bits) - 1; // e.g. 1 becomes 0001, 2 becomes 0011, 3 becomes 0111

	dw = (dw & ~(mask << lowBit)) | (newValue << lowBit);
}

bool remove_evil_keywords_from_string(const UNICODE_STRING& string)
{
	static const std::wstring evil_keywords[] =
	{
		L"IDA",
		L"ida",
		L"HxD",
		L"cheatengine",
		L"Cheat Engine",
		L"x96dbg",
		L"x32dbg",
		L"x64dbg",
		L"Wireshark",
		L"Debug",
		L"DEBUG",
		L"msvsmon",
	};

	if (!string.Buffer || !string.Length)
	{
		return false;
	}

	const std::wstring_view path(string.Buffer, string.Length / sizeof(string.Buffer[0]));

	bool modified = false;
	for (const auto& keyword : evil_keywords)
	{
		while (true)
		{
			const auto pos = path.find(keyword);
			if (pos == std::wstring::npos)
			{
				break;
			}

			modified = true;

			for (size_t i = 0; i < keyword.size(); ++i)
			{
				string.Buffer[pos + i] = L'a';
			}
		}
	}

	return modified;
}

FILE* fmemopen(void* buf, size_t len, const char* type)
{
	int fd;
	FILE* fp;
	char tp[MAX_PATH - 13];
	char fn[MAX_PATH + 1];
	int* pfd = &fd;
	int retner = -1;
	char tfname[] = "MemTF_";
	if (!GetTempPathA(sizeof(tp), tp))
		return NULL;
	if (!GetTempFileNameA(tp, tfname, 0, fn))
		return NULL;
	retner = _sopen_s(pfd, fn, _O_CREAT | _O_SHORT_LIVED | _O_TEMPORARY | _O_RDWR | _O_BINARY | _O_NOINHERIT, _SH_DENYRW, _S_IREAD | _S_IWRITE);
	if (retner != 0)
		return NULL;
	if (fd == -1)
		return NULL;
	fp = _fdopen(fd, "wb+");
	if (!fp) {
		_close(fd);
		return NULL;
	}
	/*File descriptors passed into _fdopen are owned by the returned FILE * stream.If _fdopen is successful, do not call _close on the file descriptor.Calling fclose on the returned FILE * also closes the file descriptor.*/
	fwrite(buf, len, 1, fp);
	rewind(fp);
	return fp;
}

bool is_relatively_far(const void* pointer, const void* data)
{
    const int64_t diff = size_t(data) - (size_t(pointer) + 5);
    const auto small_diff = int32_t(diff);
    return diff != int64_t(small_diff);
}

uint8_t* allocate_somewhere_near(const void* base_address, const size_t size)
{
    size_t offset = 0;
    while (true)
    {
        offset += size;
        auto* target_address = static_cast<const uint8_t*>(base_address) - offset;
        if (is_relatively_far(base_address, target_address))
        {
            return nullptr;
        }

        const auto res = VirtualAlloc(const_cast<uint8_t*>(target_address), size, MEM_RESERVE | MEM_COMMIT,
                                      PAGE_EXECUTE_READWRITE);
        if (res)
        {
            if (is_relatively_far(base_address, target_address))
            {
                VirtualFree(res, 0, MEM_RELEASE);
                return nullptr;
            }

            return static_cast<uint8_t*>(res);
        }
    }
}

#define pushad64() a.push(asmjit::x86::rax); 	\
				a.push(asmjit::x86::rcx); 	\
				a.push(asmjit::x86::rdx);	\
				a.push(asmjit::x86::rbx);	\
				a.push(asmjit::x86::rsp);	\
				a.push(asmjit::x86::rbp);	\
				a.push(asmjit::x86::rsi);	\
				a.push(asmjit::x86::rdi);	\
				a.push(asmjit::x86::r8);	\
				a.push(asmjit::x86::r9);	\
				a.push(asmjit::x86::r10);	\
				a.push(asmjit::x86::r11);	\
				a.push(asmjit::x86::r12);	\
				a.push(asmjit::x86::r13);	\
				a.push(asmjit::x86::r14);	\
				a.push(asmjit::x86::r15);


#define popad64() a.pop(asmjit::x86::r15); 	\
				a.pop(asmjit::x86::r14);	\
				a.pop(asmjit::x86::r13);	\
				a.pop(asmjit::x86::r12);	\
				a.pop(asmjit::x86::r11);	\
				a.pop(asmjit::x86::r10);	\
				a.pop(asmjit::x86::r9);		\
				a.pop(asmjit::x86::r8);		\
				a.pop(asmjit::x86::rdi);	\
				a.pop(asmjit::x86::rsi);	\
				a.pop(asmjit::x86::rbp);	\
				a.pop(asmjit::x86::rsp);	\
				a.pop(asmjit::x86::rbx);	\
				a.pop(asmjit::x86::rdx);	\
				a.pop(asmjit::x86::rcx);	\
				a.pop(asmjit::x86::rax);

#define popad64WithoutRAX() a.pop(asmjit::x86::r11);	\
				a.pop(asmjit::x86::r10);	\
				a.pop(asmjit::x86::r9);		\
				a.pop(asmjit::x86::r8);		\
				a.pop(asmjit::x86::rdi);	\
				a.pop(asmjit::x86::rsi);	\
				a.pop(asmjit::x86::rbp);	\
				a.pop(asmjit::x86::rsp);	\
				a.pop(asmjit::x86::rbx);	\
				a.pop(asmjit::x86::rdx);	\
				a.pop(asmjit::x86::rcx);



/*
#define pushad64() a.push(asmjit::x86::rax); 	\
				a.push(asmjit::x86::rcx); 	\
				a.push(asmjit::x86::rdx);	\
				a.push(asmjit::x86::rbx);	\
				a.push(asmjit::x86::rsp);	\
				a.push(asmjit::x86::rbp);	\
				a.push(asmjit::x86::rsi);	\
				a.push(asmjit::x86::rdi);	\
				a.push(asmjit::x86::r8);	\
				a.push(asmjit::x86::r9);	\
				a.push(asmjit::x86::r10);	\
				a.push(asmjit::x86::r11);	\
				a.push(asmjit::x86::r12);	\
				a.push(asmjit::x86::r13);	\
				a.push(asmjit::x86::r14);	\
				a.push(asmjit::x86::r15);


#define popad64() a.pop(asmjit::x86::r15); 	\
				a.pop(asmjit::x86::r14);	\
				a.pop(asmjit::x86::r13);	\
				a.pop(asmjit::x86::r12);	\
				a.pop(asmjit::x86::r11);	\
				a.pop(asmjit::x86::r10);	\
				a.pop(asmjit::x86::r9);		\
				a.pop(asmjit::x86::r8);		\
				a.pop(asmjit::x86::rdi);	\
				a.pop(asmjit::x86::rsi);	\
				a.pop(asmjit::x86::rbp);	\
				a.pop(asmjit::x86::rsp);	\
				a.pop(asmjit::x86::rbx);	\
				a.pop(asmjit::x86::rdx);	\
				a.pop(asmjit::x86::rcx);	\
				a.pop(asmjit::x86::rax);

#define popad64WithoutRAX() a.pop(asmjit::x86::r15); 	\
				a.pop(asmjit::x86::r14);	\
				a.pop(asmjit::x86::r13);	\
				a.pop(asmjit::x86::r12);	\
				a.pop(asmjit::x86::r11);	\
				a.pop(asmjit::x86::r10);	\
				a.pop(asmjit::x86::r9);		\
				a.pop(asmjit::x86::r8);		\
				a.pop(asmjit::x86::rdi);	\
				a.pop(asmjit::x86::rsi);	\
				a.pop(asmjit::x86::rbp);	\
				a.pop(asmjit::x86::rsp);	\
				a.pop(asmjit::x86::rbx);	\
				a.pop(asmjit::x86::rdx);	\
				a.pop(asmjit::x86::rcx);
*/