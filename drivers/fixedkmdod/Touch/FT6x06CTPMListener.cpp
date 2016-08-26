/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

	THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
	KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
	PURPOSE.

Module Name:

	FT6x06CTPMListener.cpp

Environment:

	user mode only

Author:

Contributor: Laura Fulton

--*/

//addon includes
#include <ppltasks.h>
#include <collection.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <cwctype>
#include <thread>
#include <string>


//addon namespaces
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Devices::I2c;
using namespace Windows::Devices::I2c::Provider;



#include <windows.h>
#include <windowsx.h>
#include "common.h"
#include "HidDevice.h"
#include "SendInput.h"
#include <math.h>


//addon wexception
class wexception
{
public:
	explicit wexception(const std::wstring &msg) : msg_(msg) { }
	virtual ~wexception() { /*empty*/ }

	virtual const wchar_t *wwhat() const
	{
		return msg_.c_str();
	}

private:
	std::wstring msg_;
};

//CTPM registers
enum CTPM_REGISTERS : BYTE {
	DEV_MODE = 0x0,
	GEST_ID = 01,
	TD_STATUS = 0x2, /*Number of Touch Points*/
	FOCALTECH_ID = 0xA8, /*FocalTech Panel ID*/
	CTRL = 0x86, /*Active/Monitor Mode*/

	P1_WEIGHT = 0x07, /*1st Touch Weight*/
	P2_WEIGHT = 0x0D, /*2nd Touch Weight*/

	P1_MISC = 0x08, /*1st Touch Area*/
	P2_MISC = 0x0E, /*2nd Touch Area*/

	P1_XH = 0x3, /*Event flag, 1st X Position*/
	P1_XL = 0x4, /*1st Touch X Position*/
	P2_XH = 0x09, /*Event flag, 2nd X Position*/
	P2_XL = 0x0A, /*2nd Touch X Position*/

	P1_YH = 0x05, /*Event flag, 1st Y Position*/
	P1_YL = 0x06, /*1st Touch Y Position*/
	P2_YH = 0x0B, /*Event flag, 2nd Y Position*/
	P2_YL = 0x0C, /*2nd Touch Y Position*/

	TH_GROUP = 0x80, /*Threshold for touch detection*/
	TH_DIFF = 0x85, /*Filter function coefficient */

	TIMEENTERMONITOR = 0x87, /*Time period of switching from Active mode to Monitor mode when no touching*/
	PERIODACTIVE = 0x88, /*Report rate in Active mode*/
	PERIODMONITOR = 0x89, /*Report rate in Monitor mode*/

	RADIAN_VALUE = 0x91, /*Value of minimum allowed angle while rotating gesture mode*/
	OFFSET_LEFT_RIGHT = 0x92, /*Maximum offset while Moving Left and Moving Right gesture*/
	OFFSET_UP_DOWN = 0x93,  /*Maximum offset while Moving Up and Moving Down gesture*/
	DISTANCE_LEFT_RIGHT = 0x94, /*Minimum distance while Moving Left and Moving Right gesture*/
	DISTANCE_UP_DOWN = 0x95, /*Minimum distance while Moving Up and Moving Down gesture*/
	DISTANCE_ZOOM = 0x96, /*Maximum distance while Zoom In and Zoom Out gesture*/

	G_MODE = 0xA4, /*Interrupt Polling mode/Interrupt Trigger mode*/
	STATE = 0xBC, /*Current operating mode*/

};

enum {
	TOUCH_MAX_X = 240,
	TOUCH_MAX_Y = 320
};

enum TouchEvent {
	PRESS_DOWN = 0x0,
	LIFT_UP = 0x1,
	CONTACT = 0x2,
	NO_EVENT = 0x03,

};

static BOOL touch_down;

//declaring function MakeDevice
I2cDevice^ MakeDevice(int slaveAddress, _In_opt_ String^ friendlyName);

//declaring read/write functions for testing
BYTE readReg(BYTE regAddr);
void writeReg(BYTE regAddr, BYTE val);
void ReadTouch();

//Write/Read Buff
Platform::Array<BYTE>^ regWriteWriteBuff_;
Platform::Array<BYTE>^ regReadWriteBuff_;
Platform::Array<BYTE>^ regReadReadBuff_;
Windows::Devices::I2c::I2cDevice^ dev_;

//
// Implementation
//

int main(Platform::Array<Platform::String^>^ args)
{
	HANDLE file = INVALID_HANDLE_VALUE;
	BOOLEAN found = FALSE;
	BOOLEAN bSuccess = FALSE;

	//addon for Read/Write

	regReadReadBuff_ = ref new Array<BYTE>(1);
	regReadWriteBuff_ = ref new Array<BYTE>(1);
	regWriteWriteBuff_ = ref new Array<BYTE>(2);

	try {

		dev_ = MakeDevice(0x38, nullptr);
		printf("Made device\n");
	}
	catch (const wexception& ex) {
		std::wcerr << L"Error: " << ex.wwhat() << L"\n";
		return 1;
	}
	catch (Platform::Exception^ ex) {
		std::wcerr << L"Error: " << ex->Message->Data() << L"\n";
		return 1;
	}


	srand((unsigned)time(NULL));

	found = OpenHidInjectorDevice();
	if (found) {
		printf("...sending control request to our device\n");

		std::thread reading(ReadTouch);

		reading.join();

	}
	else {
		printf("Failure: Could not find our HID device \n");
	}


cleanup:

	if (found && bSuccess == FALSE) {
		printf("****** Failure: one or more commands to device failed *******\n");
	}

	if (file != INVALID_HANDLE_VALUE) {
		CloseHandle(file);
	}

	return (bSuccess ? 0 : 1);
}
BOOLEAN SendReport(
	HANDLE File,
	void *Data,
	DWORD  Size
)
{
	DWORD BytesWritten = 0;

	return WriteFile(
		File,
		Data,
		Size,
		&BytesWritten,
		NULL
	);
}


void writeReg(BYTE regAddr, BYTE val)
{
	regWriteWriteBuff_[0] = regAddr;
	regWriteWriteBuff_[1] = val;
	dev_->Write(regWriteWriteBuff_);

}

BYTE readReg(BYTE regAddr)
{
	regReadWriteBuff_[0] = regAddr;
	dev_->WriteRead(regReadWriteBuff_, regReadReadBuff_);
	return regReadReadBuff_[0];

}


BOOL SendTouch(HANDLE File,
	UINT32 count,
	CONST POINTER_TOUCH_INFO * contacts
)
{
	for (UINT32 i = 0; i < count; i++)
	{
		//printf("\n i value: %d", i);
		HIDINJECTOR_INPUT_REPORT TouchState = { 0 };

		TouchState.ReportId = TOUCH_REPORT_ID;
		TouchState.Report.TouchReport.ContactCount = i == 0 ? count : 0; //first report contains contact count for the frame.
		TouchState.Report.TouchReport.ContactIndentifier = contacts[i].pointerInfo.pointerId;

		TouchState.Report.TouchReport.AbsoluteX = contacts[i].pointerInfo.ptPixelLocation.x;
		TouchState.Report.TouchReport.AbsoluteY = contacts[i].pointerInfo.ptPixelLocation.y;

		//TouchState.Report.TouchReport.Flags |= contacts[i].pointerInfo.pointerFlags; // Finger touching the screen

		if (contacts[i].pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT)
		{
			TouchState.Report.TouchReport.Flags |= TOUCH_TIP_SWITCH; // Finger touching the screen
		}
		else if (contacts[i].pointerInfo.pointerFlags & POINTER_FLAG_INRANGE)
		{
			TouchState.Report.TouchReport.Flags |= TOUCH_IN_RANGE;   // Finger hovering over screen
		}

		if (!SendReport(File, &TouchState, sizeof(TouchState)))
		{
			return FALSE;
		}
	}

	return TRUE;
}

BOOL SendSingleTouch(HANDLE file,
	int index,
	USHORT x,
	USHORT y,
	POINTER_FLAGS flags)
{
	POINTER_TOUCH_INFO touchInfo = { 0 };

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	//touchInfo.pointerInfo.pointerType = PT_TOUCH;
	touchInfo.pointerInfo.pointerId = 0;

	//value is expected to be normalized and clamped to[0, 0x7FFF]
	touchInfo.pointerInfo.ptPixelLocation.x = x; //(USHORT)max(0, min(x * screenWidth, 0x7FFF));
	touchInfo.pointerInfo.ptPixelLocation.y = y; //(USHORT)max(0, min(y * screenHeight, 0x7FFF));
	touchInfo.pointerInfo.pointerFlags = flags;

	{
		CHAR msgbuf[1000];
		sprintf(msgbuf, "SendSingleTouchTest:<%d,%d>\n", touchInfo.pointerInfo.ptPixelLocation.x, touchInfo.pointerInfo.ptPixelLocation.y);
		OutputDebugStringA(msgbuf);
	}

	return SendTouch(file, 1, &touchInfo);

}

//I2C addon
I2cDevice^ MakeDevice(int slaveAddress, _In_opt_ String^ friendlyName)
{
	using namespace Windows::Devices::Enumeration;

	String^ aqs;
	if (friendlyName)
		aqs = I2cDevice::GetDeviceSelector(friendlyName);
	else
		aqs = I2cDevice::GetDeviceSelector();

	auto dis = concurrency::create_task(DeviceInformation::FindAllAsync(aqs)).get();
	if (dis->Size < 1) {
		throw (L"I2C controller not found");
	}

	String^ id = dis->GetAt(0)->Id;
	auto device = concurrency::create_task(I2cDevice::FromIdAsync(
		id,
		ref new I2cConnectionSettings(slaveAddress))).get();

	if (!device) {
		std::wostringstream msg;
		msg << L"Slave address 0x" << std::hex << slaveAddress << L" on bus " << id->Data() <<
			L" is in use. Please ensure that no other applications are using I2C.";
		throw (msg.str());
	}

	return device;
}


//Interpreting CTPMs, sending to HID
void ReadTouch() {

	while (TRUE) {

		BYTE tdstatus = readReg(CTPM_REGISTERS::TD_STATUS);
		//number of touchpoints
		if ((tdstatus & 0x0F) == 0)
		{
			if (touch_down)
			{
				SendSingleTouch(g_hFile, 0, 0, 0, 0);
				touch_down = FALSE;
			}
			continue;

		}

		BYTE ctrl_ = readReg(CTPM_REGISTERS::CTRL);
		//active/monitor mode

		BYTE gmode = readReg(CTPM_REGISTERS::G_MODE);
		//interrupt polling mode vs. trigger mode


		BYTE p1xl = readReg(CTPM_REGISTERS::P1_XL);
		//printf("P1_XL:0x%08x\n", p1xl);

		BYTE p1xh = readReg(CTPM_REGISTERS::P1_XH);
		//printf("P1_XH:0x%08x\n", p1xh);

		BYTE p1yl = readReg(CTPM_REGISTERS::P1_YL);
		//printf("P1_YL:0x%08x\n", p1yl);

		BYTE p1yh = readReg(CTPM_REGISTERS::P1_YH);
		//printf("P1_YH:0x%08x\n", p1yh);

		TouchEvent event = TouchEvent(p1xh >> 6);

		UINT32 xPosition = (p1xl) | ((p1xh & 0x0F) << 8);
		UINT32 yPosition = (p1yl) | ((p1yh & 0x0F) << 8);

		//normalizing
		float x_windows = float(yPosition) / float(TOUCH_MAX_Y);
		float y_windows = float(TOUCH_MAX_X - xPosition) / float(TOUCH_MAX_X);

		{
			CHAR msgbuf[1000];
			sprintf(msgbuf, "P1:<%.2f,%.2f, %d>\n", x_windows, y_windows, event);
			OutputDebugStringA(msgbuf);
		}

		POINTER_FLAGS flags;
		//printf("yPosition:0x%08x\n", yPosition);

		switch (event)
		{
	

		case CONTACT:
			flags = POINTER_FLAG_INCONTACT;
			break;
		case PRESS_DOWN:
			flags = 0;
			break;
		case LIFT_UP:
			flags = 0;
			break;
		default:
			continue;
		}

		USHORT HidNormalizedX = x_windows * 0x7FFF;
		USHORT HidNormalizedY = y_windows * 0x7FFF;

		SendSingleTouch(g_hFile, 0, HidNormalizedX, HidNormalizedY, flags);

		touch_down = TRUE;
	
	}
}


