#include <ntifs.h>
#include <ntddk.h>

#define VM_TEST 1

#if VM_TEST
#define LOGGER(Text, ...) DbgPrintEx(0, 0, "[+] %s() -> " Text, __FUNCTION__, ##__VA_ARGS__)
#define LOGGERT(Text, ...) DbgPrintEx(0, 0, "[+] %s() -> %s", __FUNCTION__, Text, ##__VA_ARGS__)
#else
#define LOGGER(Text, ...)
#define LOGGERT(Text, ...) 
#endif // VM_TEST

#define EX_STRCMP(A, B) strcmp((const char*)A, (const char*)B)

#if VM_TEST
char g_szLastError[256];

void SetUserLastError(const char* pszError)
{
	memcpy(g_szLastError, pszError, strlen(pszError) + 1);
}

const char* GetUserLastError()
{
	return g_szLastError;
}
#else
void SetUserLastError(const char* pszError) { KeBugCheck(0xAB0BA); }
const char* GetUserLastError() { return nullptr; }
#endif // VM_TEST

extern "C" NTSTATUS ZwQuerySystemInformation
(
	ULONG InfoClass,
	PVOID Buffer,
	ULONG Length,
	PULONG ReturnLength
);

extern "C" PVOID RtlFindExportedRoutineByName
(
	PVOID ImageBase,
	PCCH RoutineName
);

using HWND = __int64;
using HDC = __int64;

typedef struct POINT_s { long x, y; } POINT;

__int64(__fastcall* pfNtUserGetDC)(HWND hWnd);
__forceinline __int64 NtUserGetDC(HWND hWnd) { return pfNtUserGetDC(hWnd); }

__int64(__fastcall* pfNtGdiMoveTo)(HDC dc, int x, int y, POINT* pt);
__forceinline __int64 NtGdiMoveTo(HDC dc, int x, int y, POINT* pt) { return pfNtGdiMoveTo(dc, x, y, pt); }

__int64(__fastcall* pfNtGdiLineTo)(HDC dc, int x, int y);
__forceinline __int64 NtGdiLineTo(HDC dc, int x, int y) { return pfNtGdiLineTo(dc, x, y); }

__int64(__fastcall* pfNtGdiCreatePen)(int style, int width, DWORD32 color, __int64 unkown);
__forceinline __int64 NtGdiCreatePen(int style, int width, DWORD32 color) { return pfNtGdiCreatePen(style, width, color, 0); }

__int64(__fastcall* pfGreSelectPenInternal)(HDC dc, __int64 pen, int unkown);
__forceinline __int64 NtGdiSelectPen(HDC dc, __int64 pen) { return pfGreSelectPenInternal(dc, pen, 0); }

typedef struct _RTL_PROCESS_MODULE_INFORMATION
{
	HANDLE Section;
	PVOID MappedBase;
	PVOID ImageBase;
	ULONG ImageSize;
	ULONG Flags;
	USHORT LoadOrderIndex;
	USHORT InitOrderIndex;
	USHORT LoadCount;
	USHORT OffsetToFileName;
	UCHAR  FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION, * PRTL_PROCESS_MODULE_INFORMATION;

typedef enum _SYSTEM_INFORMATION_CLASS
{
	SystemBasicInformation,
	SystemProcessorInformation,
	SystemPerformanceInformation,
	SystemTimeOfDayInformation,
	SystemPathInformation,
	SystemProcessInformation,
	SystemCallCountInformation,
	SystemDeviceInformation,
	SystemProcessorPerformanceInformation,
	SystemFlagsInformation,
	SystemCallTimeInformation,
	SystemModuleInformation = 0x0B
} SYSTEM_INFORMATION_CLASS,
* PSYSTEM_INFORMATION_CLASS;

typedef struct _RTL_PROCESS_MODULES
{
	ULONG NumberOfModules;
	RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, * PRTL_PROCESS_MODULES;

struct BasicModuleInformation
{
	void* Base;
	unsigned long Size;
	void* End;

	void Set(void* pBase, unsigned long Size)
	{
		this->Base = pBase;
		this->Size = Size;
		this->End = (void*)((DWORD_PTR)this->Base + this->Size);
	}
};

bool FindSystemModule(const char* pszLibName, BasicModuleInformation* pBMI)
{
	ULONG SizeOfSystemModuleInformation = 0;

	if (NT_SUCCESS(ZwQuerySystemInformation(SYSTEM_INFORMATION_CLASS::SystemModuleInformation, nullptr, 0, &SizeOfSystemModuleInformation)) || !SizeOfSystemModuleInformation)
		return false;

	auto pModulesInformation = (PRTL_PROCESS_MODULES)ExAllocatePoolWithTag(POOL_TYPE::NonPagedPool, SizeOfSystemModuleInformation, 'MDLS');

	if (!pModulesInformation)
		return false;

	if (!NT_SUCCESS(ZwQuerySystemInformation(SYSTEM_INFORMATION_CLASS::SystemModuleInformation, pModulesInformation, SizeOfSystemModuleInformation, &SizeOfSystemModuleInformation)))
	{
		ExFreePoolWithTag(pModulesInformation, 0);
		return false;
	}

	for (auto i = 0; i < pModulesInformation->NumberOfModules; i++)
	{
		auto& Module = pModulesInformation->Modules[i];

		if (!EX_STRCMP(Module.FullPathName, pszLibName))
		{
			pBMI->Set(Module.ImageBase, Module.ImageSize);
			break;
		}
	}

	ExFreePoolWithTag(pModulesInformation, 0);

	return pBMI->Base != nullptr;
}

//Usermode Space
using UM_SPACE = void*;
using GDI_PEN = __int64;

constexpr int DRIVER_ACK_CODE = 28052002;

enum COMMUNICATIONS_CODE : int
{
	TEST_CODE = 69,
	SERVICE_CODE = 1488,
	COMMUNICATIONS_CODE_MAX_SIZE
};

typedef struct pen_data
{
	IN int width;
	IN unsigned int colors;
	OUT GDI_PEN pen;
};

typedef struct lineto_data
{
	IN int x1, y1, x2, y2;
	IN GDI_PEN pen;
};

enum class SERVICE_TYPE
{
	DRAWING
};

typedef struct HookCommunicationsArgs_s
{
	SERVICE_TYPE ServiceType;
	int RequestType;
	void* _Data;
};

template <class T>
T* GetData(HookCommunicationsArgs_s* args)
{
	return (T*)args->_Data;
}

enum REQUEST_TYPE
{
	NOP,
	CREATE_PEN,
	LINETO
};

void DrawingServiceCommunication(SERVICE_TYPE ServiceType, HookCommunicationsArgs_s* args)
{
	if (ServiceType != SERVICE_TYPE::DRAWING)
		return;

	switch (args->RequestType)
	{
		case REQUEST_TYPE::CREATE_PEN:
		{
			auto data = GetData<pen_data>(args);

			data->pen = NtGdiCreatePen(0, data->width, data->colors);
			LOGGER("CreatePen: colors: %d pen: %I64x", data->colors, data->pen);
			break;
		}
		case REQUEST_TYPE::LINETO:
		{
			auto data = GetData<lineto_data>(args);

			auto DC = pfNtUserGetDC(0);
			NtGdiSelectPen(DC, data->pen);
			NtGdiMoveTo(DC, data->x1, data->y1, nullptr);
			NtGdiLineTo(DC, data->x2, data->y2);
			LOGGER("DrawLine: x1: %d y1: %d x2: %d y2: %d", data->x1, data->y1, data->x2, data->y2);
			break;
		}
	}
}

__int64(__fastcall* pfNtGdiGetTextExtent_gate)(HDC a1, const void* a2, int a3, __int64 a4, int a5);
__int64 __fastcall NtGdiGetTextExtent_proxy(HDC a1, const void* a2, int a3, __int64 a4, int a5)
{
	if (a2 != nullptr)
	{
		if (a5 == COMMUNICATIONS_CODE::TEST_CODE)
		{
			*(int*)a2 = DRIVER_ACK_CODE;
		}

		if (a5 == COMMUNICATIONS_CODE::SERVICE_CODE)
		{
			auto args = (HookCommunicationsArgs_s*)a2;
			DrawingServiceCommunication(args->ServiceType, args);
		}
	}

	return pfNtGdiGetTextExtent_gate(a1, a2, a3, a4, a5);
}

bool FindDrawingFunction()
{
	auto pszWin32kBase = "\\SystemRoot\\System32\\win32kbase.sys";
	BasicModuleInformation Win32kBase{};
	if (!FindSystemModule(pszWin32kBase, &Win32kBase))
	{
		SetUserLastError("Not Founded win32kbase");
		return false;
	}

	auto pszWin32kFull = "\\SystemRoot\\System32\\win32kfull.sys";
	BasicModuleInformation Win32kFull{};
	if (!FindSystemModule(pszWin32kFull, &Win32kFull))
	{
		SetUserLastError("Not Founded win32kfull");
		return false;
	}

	if (!(pfNtUserGetDC = (decltype(pfNtUserGetDC))RtlFindExportedRoutineByName(Win32kBase.Base, "NtUserGetDC")))
	{
		SetUserLastError("Not Founded NtUserGetDC");
		return false;
	}

	if (!(pfNtGdiMoveTo = (decltype(pfNtGdiMoveTo))RtlFindExportedRoutineByName(Win32kFull.Base, "NtGdiMoveTo")))
	{
		SetUserLastError("Not Founded NtGdiMoveTo");
		return false;
	}

	if (!(pfNtGdiLineTo = (decltype(pfNtGdiLineTo))RtlFindExportedRoutineByName(Win32kFull.Base, "NtGdiLineTo")))
	{
		SetUserLastError("Not Founded NtGdiLineTo");
		return false;
	}

	if (!(pfNtGdiCreatePen = (decltype(pfNtGdiCreatePen))RtlFindExportedRoutineByName(Win32kFull.Base, "NtGdiCreatePen")))
	{
		SetUserLastError("Not Founded NtGdiCreatePen");
		return false;
	}

	if (!(pfGreSelectPenInternal = (decltype(pfGreSelectPenInternal))RtlFindExportedRoutineByName(Win32kBase.Base, "GreSelectPenInternal")))
	{
		SetUserLastError("Not Founded GreSelectPenInternal");
		return false;
	}

	return true;
}

bool KM_Hook(void* pFunction, void* pProxyFunction, void** pGateFunction, int iActualGateInstructionLength)
{
	const int ABSOLUTE_JMP_OPCODE_OFFSET = 6;

	unsigned char bTrap[] = {
		0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /*JMP 0xFFFFFFFFFFFFFFFF*/
	};
	*(void**)&bTrap[ABSOLUTE_JMP_OPCODE_OFFSET] = pProxyFunction;

	unsigned char bSrcTrap[128]{};
	memcpy(bSrcTrap, pFunction, iActualGateInstructionLength);
	memcpy(bSrcTrap + iActualGateInstructionLength, bTrap, sizeof(bTrap));
	*(void**)&bSrcTrap[iActualGateInstructionLength + ABSOLUTE_JMP_OPCODE_OFFSET] = (void*)((__int64)pFunction + iActualGateInstructionLength);

	auto pJmpMirror = ExAllocatePoolWithTag(
		POOL_TYPE::NonPagedPool,
		iActualGateInstructionLength + sizeof(bTrap) /* idk, pool size actually min size is 4096 like VirtualAlloc? */,
		'TRAP'
	);

	if (!pJmpMirror)
	{
		SetUserLastError("ExAllocatePoolWithTag == 0");
		return false;
	}

	auto pMDL = IoAllocateMdl(pFunction, sizeof(bTrap), FALSE, FALSE, nullptr);

	if (!pMDL)
	{
		SetUserLastError("IoAllocateMdl == 0");
		return false;
	}

	MmProbeAndLockPages(pMDL, MODE::KernelMode, LOCK_OPERATION::IoReadAccess);

	auto pSpecifyCopy = MmMapLockedPagesSpecifyCache(pMDL, MODE::KernelMode, MEMORY_CACHING_TYPE::MmNonCached, nullptr, FALSE, MM_PAGE_PRIORITY::HighPagePriority);

	if (!pSpecifyCopy)
	{
		SetUserLastError("pSpecifyCopy == 0");
		return false;
	}

	if (!NT_SUCCESS(MmProtectMdlSystemAddress(pMDL, PAGE_EXECUTE_READWRITE)))
	{
		SetUserLastError("MmProtectMdlSystemAddress != 0");
		return false;
	}

	memcpy(pJmpMirror, bSrcTrap, iActualGateInstructionLength + sizeof(bTrap));
	*pGateFunction = pJmpMirror;

	memcpy(pSpecifyCopy, bTrap, sizeof(bTrap));

	MmUnmapLockedPages(pSpecifyCopy, pMDL);
	MmUnlockPages(pMDL);
	IoFreeMdl(pMDL);

	return true;
}

bool CreateCommunicationHook_NtGdiGetTextExtent()
{
	BasicModuleInformation CurrentModule{};
	if (!FindSystemModule("\\SystemRoot\\System32\\win32kfull.sys", &CurrentModule))
	{
		SetUserLastError("Not founded win32kfull.sys");
		return false;
	}

	auto pFunction = RtlFindExportedRoutineByName(CurrentModule.Base, "NtGdiGetTextExtent");

	if (!pFunction)
	{
		SetUserLastError("Not founded NtGdiGetTextExtent");
		return false;
	}

	return KM_Hook(pFunction, NtGdiGetTextExtent_proxy, (void**)&pfNtGdiGetTextExtent_gate, 19);
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	LOGGER("Loaded");

	if (!FindDrawingFunction())
	{
		LOGGERT(GetUserLastError());
		return STATUS_SUCCESS;
	}

	if (!CreateCommunicationHook_NtGdiGetTextExtent())
	{
		LOGGERT(GetUserLastError());
		return STATUS_SUCCESS;
	}
		
	LOGGER("Fully initialized");

	return STATUS_SUCCESS;
}