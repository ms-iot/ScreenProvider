#include "pch.h"
#include "sendinput.h"
#include "hiddevice.h"

HIDINJECTOR_INPUT_REPORT TouchState = { 0 };

extern HANDLE g_hFile;

BOOL SendHidReport(HIDINJECTOR_INPUT_REPORT *Rep)
{
    DWORD BytesWritten = 0;

    if (g_hFile != INVALID_HANDLE_VALUE)
	{
		return WriteFile(
			g_hFile,
			Rep,
			sizeof(*Rep),
			&BytesWritten,
			NULL
			);
	}
	return FALSE;
}

bool IoTInjectTouchInput(uint32_t count, float scaleX, float scaleY, const POINTER_TOUCH_INFO * contacts)
{
    if (count > TOUCH_MAX_FINGER)
    {
        return false;
    }

    for (UINT32 i = 0; i < count; i++)
    {
        memset(&TouchState, 0, sizeof(TouchState));

        TouchState.ReportId = TOUCH_REPORT_ID;
        TouchState.Report.TouchReport.ContactCount = i == 0 ? count : 0; // Only the first report contains the contact count for the frame.
        TouchState.Report.TouchReport.ContactIndentifier = contacts[i].pointerInfo.pointerId;

        //the value is expected to be normalized and clamped to[0, 65535]
        TouchState.Report.TouchReport.AbsoluteX = std::max((USHORT)0, std::min(USHORT(contacts[i].pointerInfo.ptPixelLocation.x * scaleX), (USHORT)0x7FFF));
        TouchState.Report.TouchReport.AbsoluteY = std::max((USHORT)0, std::min(USHORT(contacts[i].pointerInfo.ptPixelLocation.y * scaleY), (USHORT)0x7FFF));

        if (contacts[i].pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT)
        {
            TouchState.Report.TouchReport.Flags |= TOUCH_TIP_SWITCH; // Finger touching the screen
        }
        else if (contacts[i].pointerInfo.pointerFlags & POINTER_FLAG_INRANGE)
        {
            TouchState.Report.TouchReport.Flags |= TOUCH_IN_RANGE;   // Finger hovering over screen
        }

        if (!SendHidReport(&TouchState))
        {
            return false;
        }
    }

    return false;
}
