#include "pch.h"
#include "initguid.h"
#include "hiddevice.h"
#include <malloc.h>

// {7390C4D1-774F-466A-A343-AF91CD702C45}
DEFINE_GUID(GUID_DEVCLASS_HIDINJECTOR,
	0x7390c4d1, 0x774f, 0x466a, 0xa3, 0x43, 0xaf, 0x91, 0xcd, 0x70, 0x2c, 0x45);

// {A8E7AF29-4F2F-43E0-AA80-086C12135547}
DEFINE_GUID(GUID_DEVINTERFACE_HIDINJECTOR,
	0xa8e7af29, 0x4f2f, 0x43e0, 0xaa, 0x80, 0x8, 0x6c, 0x12, 0x13, 0x55, 0x47);

// {97976E57-A740-4B31-A929-6B21B435BCC4}
DEFINE_GUID(GUID_BUS_HIDINJECTOR,
	0x97976e57, 0xa740, 0x4b31, 0xa9, 0x29, 0x6b, 0x21, 0xb4, 0x35, 0xbc, 0xc4);

HANDLE g_hFile = NULL;

BOOLEAN
FindMatchingDevice(
	_Out_ HANDLE* Handle
	)
{
	CONFIGRET cr = CR_SUCCESS;
	PWSTR deviceInterfaceList = NULL;
	ULONG deviceInterfaceListLength = 0;
	PWSTR currentInterface;
	BOOLEAN bRet = FALSE;
	HANDLE devHandle = INVALID_HANDLE_VALUE;
	GUID DevInterface = GUID_DEVINTERFACE_HIDINJECTOR;

	if (NULL == Handle) {
		return FALSE;
	}

	*Handle = INVALID_HANDLE_VALUE;

	cr = CM_Get_Device_Interface_List_Size(
		&deviceInterfaceListLength,
		&DevInterface,
		NULL,
		CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
	if (cr != CR_SUCCESS) {
		goto clean0;
	}

	if (deviceInterfaceListLength <= 1) {
		bRet = FALSE;
		goto clean0;
	}

	deviceInterfaceList = (PWSTR)malloc(deviceInterfaceListLength * sizeof(WCHAR));
	if (deviceInterfaceList == NULL) {
		goto clean0;
	}
	ZeroMemory(deviceInterfaceList, deviceInterfaceListLength * sizeof(WCHAR));

	cr = CM_Get_Device_Interface_ListW(
		&DevInterface,
		NULL,
		deviceInterfaceList,
		deviceInterfaceListLength,
		CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
	if (cr != CR_SUCCESS) {
		goto clean0;
	}

	//
	// Enumerate devices of this interface class
	//
	for (currentInterface = deviceInterfaceList;
	*currentInterface;
		currentInterface += wcslen(currentInterface) + 1) {

		devHandle = CreateFileW(currentInterface,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, // no SECURITY_ATTRIBUTES structure
			OPEN_EXISTING, // No special create flags
			0, // No special attributes
			NULL); // No template file

		if (INVALID_HANDLE_VALUE == devHandle) {
			continue;
		}

		bRet = TRUE;
		*Handle = devHandle;
	}

clean0:
	if (deviceInterfaceList != NULL) {
		free(deviceInterfaceList);
	}
	if (CR_SUCCESS != cr) {
		bRet = FALSE;
	}
	return bRet;
}

BOOLEAN
FindMatchingDevice2(
	_Out_ HANDLE* Handle
	)
{
	*Handle = CreateFileW(L"\\\\.\\HidInjector",
		GENERIC_WRITE,
		0,
		NULL, // no SECURITY_ATTRIBUTES structure
		OPEN_EXISTING, // No special create flags
		0, // No special attributes
		NULL); // No template file

	if (INVALID_HANDLE_VALUE == *Handle) {
		return FALSE;
	}

	return TRUE;
}

BOOL OpenHidInjectorDevice()
{
	BOOL success = FindMatchingDevice(&g_hFile);
	if (!success)
	{
		g_hFile = NULL;
	}
	return success;
}

void CloseHidInjectorDevice()
{
	if (g_hFile != NULL)
	{
		CloseHandle(g_hFile);
		g_hFile = NULL;
	}
}
