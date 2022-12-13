#include <Windows.h>
#include <iostream>

bool IsKeyPressed(int iVkCode)
{
	return GetAsyncKeyState(iVkCode);
}

bool IsKeyDowned(int iVkCode)
{
	static bool bKeyMapDowned[255];
	bool bIsPressed = GetAsyncKeyState(iVkCode);
	bool ret = bIsPressed && !bKeyMapDowned[iVkCode];
	bKeyMapDowned[iVkCode] = bIsPressed;
	return ret;
}

bool IsKeyReleased(int iVkCode)
{
	static bool bKeyMapReleased[255];
	bool bIsPressed = GetAsyncKeyState(iVkCode);
	bool ret = !bIsPressed && bKeyMapReleased[iVkCode];
	bKeyMapReleased[iVkCode] = bIsPressed;
	return ret;
}

namespace CommunicationDriver
{
	constexpr int DRIVER_ACK_CODE = 28052002;

	enum COMMUNICATIONS_CODE : int
	{
		TEST_CODE = 69,
		SERVICE_CODE = 1488,
		COMMUNICATIONS_CODE_MAX_SIZE
	};

	enum class SERVICE_TYPE : int
	{
		DRAWING
	};

	typedef struct HookCommunicationsArgs_s
	{
		SERVICE_TYPE ServiceType;
		int RequestType;
		void* _Data;
	};

	bool LoadNtFunction(const char* LibName, const char* FuncName, void* pDest)
	{
		auto Lib = GetModuleHandle(LibName);

		if (!Lib)
		{
			printf("[-] Not found %s\n", LibName);
			return false;
		}

		auto Function = GetProcAddress(Lib, FuncName);

		if (!Function)
		{
			printf("[-] Not found %s\n", FuncName);
			return false;
		}

		*(std::uintptr_t*)pDest = (std::uintptr_t)Function;

		return true;
	}

	static __int64(__fastcall* pfNtGdiGetTextExtent)(HDC, const void*, signed int, __int64, int);

	bool InitNtFunction_NtGdiGetTextExtent()
	{
		if (!pfNtGdiGetTextExtent)
		{
			if (!LoadNtFunction("win32u.dll", "NtGdiGetTextExtent", &pfNtGdiGetTextExtent))
				return false;
		}

		return true;
	}

	bool IsDriverLoaded()
	{
		if (!InitNtFunction_NtGdiGetTextExtent())
			return false;

		int iAckResult = 0;

		pfNtGdiGetTextExtent(0, &iAckResult, 0, 0, (int)COMMUNICATIONS_CODE::TEST_CODE);

		return iAckResult == DRIVER_ACK_CODE;
	}

	bool RequestCommunication(SERVICE_TYPE ServiceType, int RequestType, void* Data)
	{
		if (!InitNtFunction_NtGdiGetTextExtent())
			return false;

		HookCommunicationsArgs_s args;
		args.ServiceType = ServiceType;
		args.RequestType = RequestType;
		args._Data = Data;

		pfNtGdiGetTextExtent(0, &args, 0, 0, (int)COMMUNICATIONS_CODE::SERVICE_CODE);

		return true;
	}
}

namespace DrawingService
{
	enum REQUEST_TYPE
	{
		NOP,
		CREATE_PEN,
		LINETO
	};

	using GDI_PEN = __int64;

	typedef struct pen_data
	{
		IN int width;
		IN unsigned int colors;
		OUT GDI_PEN pen;
	};

	typedef struct select_pen_data
	{
		IN GDI_PEN pen;
	};

	typedef struct lineto_data
	{
		IN int x1, y1, x2, y2;
		IN GDI_PEN pen;
	};

	bool CreatePen(int width, unsigned int colors, GDI_PEN& pen)
	{
		pen_data pendata;
		pendata.width = width;
		pendata.colors = colors;
		pendata.pen = 0;

		if (CommunicationDriver::RequestCommunication(CommunicationDriver::SERVICE_TYPE::DRAWING, REQUEST_TYPE::CREATE_PEN, &pendata))
		{
			pen = pendata.pen;
			return true;
		}

		pen = 0;
		return false;
	}

	bool DrawLine(int x1, int y1, int x2, int y2, GDI_PEN pen)
	{
		lineto_data linedata;
		linedata.x1 = x1;
		linedata.y1 = y1;
		linedata.x2 = x2;
		linedata.y2 = y2;
		linedata.pen = pen;

		return CommunicationDriver::RequestCommunication(CommunicationDriver::SERVICE_TYPE::DRAWING, REQUEST_TYPE::LINETO, &linedata);
	}
}

int main()
{
	LoadLibrary("user32.dll");

	printf("[+] Hello from usermode\n");

	if (!CommunicationDriver::IsDriverLoaded())
	{
		printf("Driver not loaded\n");
		system("pause");
		return 1;
	}

	DrawingService::GDI_PEN pen;
	if (!DrawingService::CreatePen(5, 0x000000FF, pen) || !pen)
	{
		printf("Pen not created: %I64x\n", pen);
		system("pause");
		return 1;
	}
		
	printf("Pen: %I64x", pen);

	while (true)
	{
		DrawingService::DrawLine(0, 0, 1000, 1000, pen);
		DrawingService::DrawLine(1000, 0, 0, 1000, pen);

		Sleep(1);
	}

	Sleep(1000);

	return 0;
}