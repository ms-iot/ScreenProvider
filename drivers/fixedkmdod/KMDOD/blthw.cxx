/******************************Module*Header*******************************\
* Module Name: blthw.cxx
*
* Sample display driver functions for a HW blt simulation. This file is 
* only provided to simulate how a real hardware-accelerated display-only 
* driver functions, and should not be used in a real driver.
*
* Copyright (c) 2011 Microsoft Corporation
\**************************************************************************/

#include "BDD.hxx"
#include "ILI9341.hxx"
#define RESHUB_USE_HELPER_ROUTINES
#include "RESHUB.hxx"
#include "GPIO.h"

typedef struct
{
    CONST DXGKRNL_INTERFACE*        DxgkInterface;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyInterrupt;
} SYNC_NOTIFY_INTERRUPT;

KSYNCHRONIZE_ROUTINE SynchronizeVidSchNotifyInterrupt;

BOOLEAN SynchronizeVidSchNotifyInterrupt(_In_opt_ PVOID params)
{
    // This routine is non-paged code called at the device interrupt level (DIRQL)
    // to notify VidSch and schedule a DPC. It is meant as a demonstration of handling 
    // a real hardware interrupt, even though it is actually called from asynchronous 
    // present worker threads in this sample.
    SYNC_NOTIFY_INTERRUPT* pParam = reinterpret_cast<SYNC_NOTIFY_INTERRUPT*>(params);

    // The context is known to be non-NULL
    __analysis_assume(pParam != NULL);

    // Update driver information related to fences
    switch(pParam->NotifyInterrupt.InterruptType)
    {
    case DXGK_INTERRUPT_DISPLAYONLY_VSYNC:
    case DXGK_INTERRUPT_DISPLAYONLY_PRESENT_PROGRESS:
        break;
    default:
        NT_ASSERT(FALSE);
        return FALSE;
    }

    // Callback OS to report about the interrupt
    pParam->DxgkInterface->DxgkCbNotifyInterrupt(pParam->DxgkInterface->DeviceHandle,&pParam->NotifyInterrupt);

    // Now queue a DPC for this interrupt (to callback schedule at DCP level and let it do more work there)
    // DxgkCbQueueDpc can return FALSE if there is already a DPC queued
    // this is an acceptable condition
    pParam->DxgkInterface->DxgkCbQueueDpc(pParam->DxgkInterface->DeviceHandle);

    return TRUE;
}

#pragma code_seg("PAGE")

KSTART_ROUTINE HwContextWorkerThread;

struct DoPresentMemory
{
    PVOID                     DstAddr;
	size_t					  ScreenWidth;
	size_t				      ScreenHeight;
    UINT                      DstStride;
    ULONG                     DstBitPerPixel;
    UINT                      SrcWidth;
    UINT                      SrcHeight;
    BYTE*                     SrcAddr;
    LONG                      SrcPitch;
    ULONG                     NumMoves;             // in:  Number of screen to screen moves
    D3DKMT_MOVE_RECT*         Moves;               // in:  Point to the list of moves
    ULONG                     NumDirtyRects;        // in:  Number of direct rects
    RECT*                     DirtyRect;           // in:  Point to the list of dirty rects
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    BOOLEAN                   SynchExecution;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  SourceID;
    HANDLE                    hAdapter;
    PMDL                      Mdl;
    BDD_HWBLT*                DisplaySource;
};

void
HwExecutePresentDisplayOnly(
    HANDLE Context);

NTSTATUS
StartHwBltPresentWorkerThread(
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ _When_(return==0, __drv_aliasesMem) PVOID StartContext)
/*++

  Routine Description:

    This routine creates the worker thread to execute a single present 
    command. Creating a new thread on every asynchronous present is not 
    efficient, but this file is only meant as a simulation, not an example 
    of implementation.

  Arguments:

    StartRoutine - start routine
    StartContext - start context

  Return Value:

    Status

--*/
{
    PAGED_CODE();

    OBJECT_ATTRIBUTES ObjectAttributes;
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    HANDLE hWorkerThread = NULL;

    // copy data from the context which is need here, as it will be deleted in separate thread
    DoPresentMemory* ctx = reinterpret_cast<DoPresentMemory*>(StartContext);
    BDD_HWBLT* displaySource = ctx->DisplaySource;

    NTSTATUS Status = PsCreateSystemThread(
        &hWorkerThread,
        THREAD_ALL_ACCESS,
        &ObjectAttributes,
        NULL,
        NULL,
        StartRoutine,
        StartContext);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }


    // wait for thread to start - infinite wait -
    // need to make sure the tread is running before OS stars submitting the work items to it
    KeWaitForSingleObject(&displaySource->m_hThreadStartupEvent, Executive, KernelMode, FALSE, NULL);

    // Handle is passed to the parent object which must close it
    displaySource->SetPresentWorkerThreadInfo(hWorkerThread);

    // Resume context thread, this is done by setting the event the thread is waiting on
    KeSetEvent(&displaySource->m_hThreadSuspendEvent, 0, FALSE);

    return STATUS_PENDING;
}

BDD_HWBLT::BDD_HWBLT():m_DevExt (NULL),
                m_SynchExecution(TRUE),
                m_hPresentWorkerThread(NULL),
                m_pPresentWorkerThread(NULL)
{
    PAGED_CODE();

    KeInitializeEvent(&m_hThreadStartupEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&m_hThreadSuspendEvent, SynchronizationEvent, FALSE);

}


BDD_HWBLT::~BDD_HWBLT()
/*++

  Routine Description:

    This routine waits on present worker thread to exit before
    destroying the object

  Arguments:

    None

  Return Value:

    None

--*/
{
    PAGED_CODE();

    // make sure the worker thread has exited
    SetPresentWorkerThreadInfo(NULL);
}

NTSTATUS BDD_HWBLT::InitSpiAndGpio(DXGK_DEVICE_INFO *deviceInfo)
{
	PAGED_CODE();

	NTSTATUS Status;

	BOOLEAN fConnectionIdSpiFound = FALSE;
	BOOLEAN fConnectionIdGpioFound = FALSE;
	LARGE_INTEGER connectionIdSpi = { 0 };
	LARGE_INTEGER connectionIdGpio = { 0 };

	//Find a connection ID for SPI and GPIO
	{
		const CM_RESOURCE_LIST* resourceListPtr = deviceInfo->TranslatedResourceList;
		const CM_PARTIAL_RESOURCE_LIST* partialResourceListPtr = &resourceListPtr->List[0].PartialResourceList;

		// Look for a memory resource and an interrupt resource
		const ULONG resourceCount = partialResourceListPtr->Count;
		for (ULONG i = 0; i < resourceCount; ++i)
		{
			const CM_PARTIAL_RESOURCE_DESCRIPTOR* resourcePtr = &partialResourceListPtr->PartialDescriptors[i];
			//Determine the resource type.
			switch (resourcePtr->Type)
			{
			case CmResourceTypeConnection:
			{
				//check against expected connection types.
				UCHAR Class = resourcePtr->u.Connection.Class;
				UCHAR Type = resourcePtr->u.Connection.Type;

				if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL)
				{
					if (Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_SPI)
					{
						if (fConnectionIdSpiFound == FALSE)
						{
							//Save the SPI connection id
							connectionIdSpi.LowPart = resourcePtr->u.Connection.IdLowPart;
							connectionIdSpi.HighPart = resourcePtr->u.Connection.IdHighPart;
							fConnectionIdSpiFound = TRUE;
						}
					}

				}

				if (Class == CM_RESOURCE_CONNECTION_CLASS_GPIO)
				{
					if (Type == CM_RESOURCE_CONNECTION_TYPE_GPIO_IO)
					{
						if (fConnectionIdGpioFound == FALSE)
						{
							//Save the GPIO connection id
							connectionIdGpio.LowPart = resourcePtr->u.Connection.IdLowPart;
							connectionIdGpio.HighPart = resourcePtr->u.Connection.IdHighPart;
							fConnectionIdGpioFound = TRUE;
						}
					}
				}

			}
			break;
			default:
				//Don't care if not SPI or GPIO
				break;
			}
		}

	}

	//Get a device path name for SPI
	DECLARE_UNICODE_STRING_SIZE(spbDeviceName, RESOURCE_HUB_PATH_SIZE);
	Status = RESOURCE_HUB_CREATE_PATH_FROM_ID(&spbDeviceName,
		connectionIdSpi.LowPart,
		connectionIdSpi.HighPart);

	if (!NT_SUCCESS(Status))
	{
		return Status;
	}

	//Open a file handle for SPI
	m_spiDeviceObject = nullptr;
	m_spiFileObjectPointer = nullptr;
	Status = IoGetDeviceObjectPointer(&spbDeviceName,
		(FILE_WRITE_DATA | FILE_READ_DATA),
		&m_spiFileObjectPointer,
		&m_spiDeviceObject);

	if (!NT_SUCCESS(Status))
	{
		return Status;
	}

	//Get a device path name for GPIO
	DECLARE_UNICODE_STRING_SIZE(gpioDeviceName, RESOURCE_HUB_PATH_SIZE);
	Status = RESOURCE_HUB_CREATE_PATH_FROM_ID(&gpioDeviceName,
		connectionIdGpio.LowPart,
		connectionIdGpio.HighPart);

	if (!NT_SUCCESS(Status))
	{
		return Status;
	}

	//Open a file handle for GPIO
	m_gpioDeviceObject = nullptr;
	m_gpioFileObjectPointer = nullptr;
	Status = IoGetDeviceObjectPointer(&gpioDeviceName,
		(GENERIC_READ | GENERIC_WRITE),
		&m_gpioFileObjectPointer,
		&m_gpioDeviceObject);

	if (!NT_SUCCESS(Status))
	{
		return Status;
	}

	Status = InitializeAdafruitPiTFT();

	return Status;
}

NTSTATUS BDD_HWBLT::WriteDataIrp(PBYTE pBuffer, size_t bufferByteLength)
{
	NTSTATUS Status;
	IO_STATUS_BLOCK IoStatus;
	KEVENT event;
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	//Allocate IRP
	_IRP* Irp = IoBuildSynchronousFsdRequest(IRP_MJ_WRITE,
		m_spiDeviceObject,
		pBuffer, bufferByteLength, NULL, &event, &IoStatus);

	PIO_STACK_LOCATION irpSp = IoGetNextIrpStackLocation(Irp);
	irpSp->FileObject = m_spiFileObjectPointer;

	//call driver
	Status = IoCallDriver(m_spiDeviceObject, Irp);

	if (Status == STATUS_PENDING)
	{
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}

	return Status;
}

NTSTATUS BDD_HWBLT::WriteGpioIrp(PBYTE pGpioData)
{
	NTSTATUS Status;
	IO_STATUS_BLOCK IoStatus;
	KEVENT event;
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	//Allocate Irp 
	_IRP* Irp = IoBuildDeviceIoControlRequest(IOCTL_GPIO_WRITE_PINS,
		m_gpioDeviceObject,
		pGpioData, 1, NULL, 0, FALSE, &event, &IoStatus);

	PIO_STACK_LOCATION irpSp = IoGetNextIrpStackLocation(Irp);
	irpSp->FileObject = m_gpioFileObjectPointer;

	//Call driver
	Status = IoCallDriver(m_gpioDeviceObject, Irp);

	if (Status == STATUS_PENDING)
	{
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}

	return Status;
}

NTSTATUS BDD_HWBLT::WriteCommandIrp(PBYTE pBuffer, size_t bufferByteLength)
{
	NTSTATUS Status;
	BYTE gpioData = 0x00;

	//Pull GPIO low 
	Status = WriteGpioIrp(&gpioData);

	//Send Command using
	Status = WriteDataIrp(pBuffer, bufferByteLength);

	//pull GPIO high
	gpioData = 0x01;
	Status = WriteGpioIrp(&gpioData);

	return Status;
}

NTSTATUS BDD_HWBLT::InitializeAdafruitPiTFT()
{
	NTSTATUS Status;
	//const int kResetDelay = 5;
	static BYTE resetCommand = ILI9341_SOFTWARE_RESET;
	static BYTE clearDisplayCommand = ILI9341_DISP_OFF;
	static BYTE gpioData = 0x01;
	//static BYTE testValue[] = {0x12, 0x34, 0x56, 0x78};
	//PBYTE ptestValue = testValue;

	//Set gpio as known state high
	Status = WriteGpioIrp(&gpioData);
	//Status = WriteDataIrp(ptestValue, sizeof(testValue));

	//Startup Sequence
	static BYTE powerControlB = ILI9341_PW_CTRLB;
	static BYTE powerControlBParameters[] = { 0x00, 0x83, 0x30 };
	PBYTE pPowerControlBParameters = powerControlBParameters;

	static BYTE powerOnSequenceControl = ILI9341_PWR_SEQ;
	static BYTE powerOnSequenceControlParameters[] = { 0x64, 0x03, 0x12, 0x81 };
	PBYTE pPowerOnSequenceControlParameters = powerOnSequenceControlParameters;

	static BYTE driverTimingControlA = ILI9341_TIMING_CTRLA;
	static BYTE driverTimingControlAParameters[] = { 0x85, 0x01, 0x79 };
	PBYTE pDriverTimingControlAParameters = driverTimingControlAParameters;

	static BYTE powerControlA = ILI9341_PW_CTRLA;
	static BYTE powerControlAParameters[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 };
	PBYTE pPowerControlAParameters = powerControlAParameters;

	static BYTE pumpRatioControl = ILI9341_PUMP_RATIO_CTRL;
	static BYTE pumpRatioControlParameters[] = { 0x20 };
	PBYTE pPumpRatioControlParameters = pumpRatioControlParameters;

	static BYTE driverTimingControlB = ILI9341_TIMING_CTRLB;
	static BYTE driverTimingControlBParameters[] = { 0x00, 0x00 };
	PBYTE pDriverTimingControlBParameters = driverTimingControlBParameters;

	Status = WriteCommandIrp(&resetCommand, sizeof(resetCommand));
	Status = WriteCommandIrp(&clearDisplayCommand, sizeof(clearDisplayCommand));

	Status = WriteCommandIrp(&powerControlB, sizeof(powerControlB));
	Status = WriteDataIrp(pPowerControlBParameters, sizeof(powerControlBParameters));

	Status = WriteCommandIrp(&powerOnSequenceControl, sizeof(powerOnSequenceControl));
	Status = WriteDataIrp(pPowerOnSequenceControlParameters, sizeof(powerOnSequenceControlParameters));

	Status = WriteCommandIrp(&driverTimingControlA, sizeof(driverTimingControlA));
	Status = WriteDataIrp(pDriverTimingControlAParameters, sizeof(driverTimingControlAParameters));

	Status = WriteCommandIrp(&powerControlA, sizeof(powerControlA));
	Status = WriteDataIrp(pPowerControlAParameters, sizeof(powerControlAParameters));

	Status = WriteCommandIrp(&pumpRatioControl, sizeof(pumpRatioControl));
	Status = WriteDataIrp(pPumpRatioControlParameters, sizeof(pumpRatioControlParameters));

	Status = WriteCommandIrp(&driverTimingControlB, sizeof(driverTimingControlB));
	Status = WriteDataIrp(pDriverTimingControlBParameters, sizeof(driverTimingControlBParameters));

	//Power control fix array?
	static BYTE powerControl1 = ILI9341_PW_CTRL1;
	static BYTE powerControl1Parameter = 0x26;
	static BYTE powerControl2 = ILI9341_PW_CTRL2;
	static BYTE powerControl2Parameter = 0x11;

	Status = WriteCommandIrp(&powerControl1, sizeof(powerControl1));
	Status = WriteDataIrp(&powerControl1Parameter, sizeof(powerControl1Parameter));
	Status = WriteCommandIrp(&powerControl2, sizeof(powerControl2));
	Status = WriteDataIrp(&powerControl2Parameter, sizeof(powerControl2Parameter));

	//VCOM
	static BYTE VCOMControl1 = ILI9341_VCOM_CTRL1;
	static BYTE VCOMControl1Parameter[] = { 0x35, 0x3E };
	PBYTE pVCOMControl1Parameter = VCOMControl1Parameter;
	static BYTE VCOMControl2 = ILI9341_VCOM_CTRL2;
	static BYTE VCOMControl2Parameter[] = { 0xBE };
	PBYTE pVCOMControl2Parameter = VCOMControl2Parameter;

	Status = WriteCommandIrp(&VCOMControl1, sizeof(VCOMControl1));
	Status = WriteDataIrp(pVCOMControl1Parameter, sizeof(VCOMControl1Parameter));
	Status = WriteCommandIrp(&VCOMControl2, sizeof(VCOMControl2));
	Status = WriteDataIrp(pVCOMControl2Parameter, sizeof(VCOMControl2Parameter));

	//Pixel Format control, RIM[7], DPI[6:4], DBI[3:1], X
	static BYTE COLMODPixelFormatSet = ILI9341_PIX_SET;
	static BYTE COLMODPixelFormatSetParameters[] = { 0x55 };
	PBYTE pCOLMODPixelFormatSetParameters = COLMODPixelFormatSetParameters;

	Status = WriteCommandIrp(&COLMODPixelFormatSet, sizeof(COLMODPixelFormatSet));
	Status = WriteDataIrp(pCOLMODPixelFormatSetParameters, sizeof(COLMODPixelFormatSetParameters));

	//extended command, RGB Interface Signal Control, RCM is [6:5] either 10 or 11, nothing changes
	static BYTE rgbInterfaceControl = 0xB0;
	static BYTE rgbInterfaceParamter[] = { 0xD0 };
	PBYTE pRgbInterfaceParamter = rgbInterfaceParamter;

	Status = WriteCommandIrp(&rgbInterfaceControl, sizeof(rgbInterfaceControl));
	Status = WriteDataIrp(pRgbInterfaceParamter, sizeof(rgbInterfaceControl));

	//interface control, doesn't do anything right now, default value 01,00,00, RIM is last bit in 3rd command
	static BYTE interfaceControl = ILI9341_INTERFACE_CTRL;
	static BYTE interfaceParamter[] = { 0x01, 0x00, 0x00 };
	PBYTE pInterfaceParamter = interfaceParamter;

	Status = WriteCommandIrp(&interfaceControl, sizeof(interfaceControl));
	Status = WriteDataIrp(pInterfaceParamter, sizeof(interfaceParamter));

	//frame rate
	static BYTE frameRateControl = ILI9341_FRM_RATE_CTRL1;
	static BYTE frameRateControlParameters[] = { 0x00, 0x15 };
	PBYTE pFrameRateControlParameters = frameRateControlParameters;

	Status = WriteCommandIrp(&frameRateControl, sizeof(frameRateControl));
	Status = WriteDataIrp(pFrameRateControlParameters, sizeof(frameRateControlParameters));

	//gamma
	static BYTE gammaSet = ILI9341_GAMMA_SET;
	static BYTE gammaSetParameters[] = { 0x01 };
	PBYTE pGammaSetParameters = gammaSetParameters;

	Status = WriteCommandIrp(&gammaSet, sizeof(gammaSet));
	Status = WriteDataIrp(pGammaSetParameters, sizeof(gammaSetParameters));

	//Frame copy control
	BYTE memAccess = ILI9341_MEM_ACCESS_CTRL;
	BYTE memAccessParameters[] = { 0xE0 };
	PBYTE pMemAccessParameters = memAccessParameters;

	Status = WriteCommandIrp(&memAccess, sizeof(memAccess));
	Status = WriteDataIrp(pMemAccessParameters, sizeof(memAccessParameters));

	//display
	static BYTE entryModeSet = ILI9341_ENTRY_MODE_SET;
	static BYTE entryModeSetParameter[] = { 0x07 };
	PBYTE pEntryModeSetParameter = entryModeSetParameter;

	static BYTE displayFunctionControl = ILI9341_DISP_FUNC_CTRL;
	static BYTE displayFunctionControlParameters[] = { 0x0A, 0x82, 0x27, 0x00 };
	PBYTE pDisplayFunctionControlParameters = displayFunctionControlParameters;

	static BYTE sleepOut = ILI9341_SLEEP_MODE_OUT;
	static BYTE displayOn = ILI9341_DISP_ON;

	Status = WriteCommandIrp(&entryModeSet, sizeof(entryModeSet));
	Status = WriteDataIrp(pEntryModeSetParameter, sizeof(entryModeSetParameter));

	Status = WriteCommandIrp(&displayFunctionControl, sizeof(displayFunctionControl));
	Status = WriteDataIrp(pDisplayFunctionControlParameters, sizeof(displayFunctionControlParameters));

	Status = WriteCommandIrp(&sleepOut, sizeof(sleepOut));
	Status = WriteCommandIrp(&displayOn, sizeof(displayOn));

	return Status;
}

VOID BDD_HWBLT::CleanUp()
{
	//Deference spi file pointer
	if (m_spiFileObjectPointer != nullptr)
	{
		ObDereferenceObject(m_spiFileObjectPointer);
	}

	//Deference gpio file pointer
	if (m_gpioFileObjectPointer != nullptr)
	{
		ObDereferenceObject(m_gpioFileObjectPointer);
	}
}

NTSTATUS BDD_HWBLT::SetWindow(int x, int y, int w, int h)
{
	NTSTATUS Status;
	int windowWidth = (x + w - 1);
	int windowHeight = (y + h - 1);

	//initialize column addressing
	BYTE setColumnAddress = ILI9341_COLUMN_ADDRESS_SET;
	BYTE setColumnAddressParameters[] = {
		(BYTE)((x >> 8) & 0xFF),
		(BYTE)(x & 0xFF),
		(BYTE)((windowWidth >> 8) & 0xFF),
		(BYTE)(windowWidth & 0xFF)
	};
	PBYTE pSetColumnAddressParameters = setColumnAddressParameters;

	//initialize page addressing
	BYTE setPageAddress = ILI9341_PAGE_ADDRESS_SET;
	BYTE setPageAddressParameters[] = {
		(BYTE)((y >> 8) & 0xFF),
		(BYTE)(y & 0xFF),
		(BYTE)((windowHeight >> 8) & 0xFF),
		(BYTE)(windowHeight & 0xFF)
	};
	PBYTE pSetPageAddressParameters = setPageAddressParameters;

	//set column address
	Status = WriteCommandIrp(&setColumnAddress, sizeof(setColumnAddress));
	Status = WriteDataIrp(pSetColumnAddressParameters, sizeof(setColumnAddressParameters));

	//set page address
	Status = WriteCommandIrp(&setPageAddress, sizeof(setPageAddress));
	Status = WriteDataIrp(pSetPageAddressParameters, sizeof(setPageAddressParameters));

	return Status;
}

#pragma warning(push)
#pragma warning(disable:26135) // The function doesn't lock anything

void
BDD_HWBLT::SetPresentWorkerThreadInfo(
    HANDLE hWorkerThread)
/*++

  Routine Description:

    The method is updating present worker information
    It is called in following cases:
     - In ExecutePresent to update worker thread information
     - In Dtor to wait on worker thread to exit

  Arguments:

    hWorkerThread - handle of the present worker thread

  Return Value:

    None

--*/
{

    PAGED_CODE();

    if (m_pPresentWorkerThread)
    {
        // Wait for thread to exit
        KeWaitForSingleObject(m_pPresentWorkerThread, Executive, KernelMode,
                              FALSE, NULL);
        // Dereference thread object
        ObDereferenceObject(m_pPresentWorkerThread);
        m_pPresentWorkerThread = NULL;

        NT_ASSERT(m_hPresentWorkerThread);
        ZwClose(m_hPresentWorkerThread);
        m_hPresentWorkerThread = NULL;
    }

    if (hWorkerThread)
    {
        // Make sure that thread's handle would be valid even if the thread exited
        ObReferenceObjectByHandle(hWorkerThread, THREAD_ALL_ACCESS, NULL,
                                  KernelMode, &m_pPresentWorkerThread, NULL);
        NT_ASSERT(m_pPresentWorkerThread);
        m_hPresentWorkerThread = hWorkerThread;
    }

}
#pragma warning(pop)

NTSTATUS
BDD_HWBLT::ExecutePresentDisplayOnly(
	_In_ BYTE*             DstAddr,
	_In_ size_t			   screenHeight,
	_In_ size_t			   screenWidth,
    _In_ UINT              DstBitPerPixel,
    _In_ BYTE*             SrcAddr,
    _In_ UINT              SrcBytesPerPixel,
    _In_ LONG              SrcPitch,
    _In_ ULONG             NumMoves,
    _In_ D3DKMT_MOVE_RECT* Moves,
    _In_ ULONG             NumDirtyRects,
    _In_ RECT*             DirtyRect,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
/*++

  Routine Description:

    The method creates present worker thread and provides context
    for it filled with present commands

  Arguments:

    DstAddr - address of destination surface
	screenHeight - height in pixels of the display
	screenWidth - width in pixels of the display
	DstBitPerPixel - color depth of destination surface
    SrcAddr - address of source surface
    SrcBytesPerPixel - bytes per pixel of source surface
    SrcPitch - source surface pitch (bytes in a row)
    NumMoves - number of moves to be copied
    Moves - moves' data
    NumDirtyRects - number of rectangles to be copied
    DirtyRect - rectangles' data
    Rotation - roatation to be performed when executing copy
    CallBack - callback for present worker thread to report execution status

  Return Value:

    Status

--*/
{

    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;

    SIZE_T sizeMoves = NumMoves*sizeof(D3DKMT_MOVE_RECT);
    SIZE_T sizeRects = NumDirtyRects*sizeof(RECT);
    SIZE_T size = sizeof(DoPresentMemory) + sizeMoves + sizeRects;

    DoPresentMemory* ctx = reinterpret_cast<DoPresentMemory*>
                                (new (PagedPool) BYTE[size]);

    if (!ctx)
    {
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(ctx,size);

    const CURRENT_BDD_MODE* pModeCur = m_DevExt->GetCurrentMode(m_SourceId);

    ctx->DstAddr          = DstAddr;
	ctx->ScreenWidth	  = screenWidth;
	ctx->ScreenHeight	  = screenHeight;
    ctx->DstBitPerPixel   = DstBitPerPixel;
    ctx->DstStride        = pModeCur->DispInfo.Pitch;
    ctx->SrcWidth         = pModeCur->SrcModeWidth;
    ctx->SrcHeight        = pModeCur->SrcModeHeight;
    ctx->SrcAddr          = NULL;
    ctx->SrcPitch         = SrcPitch;
    ctx->Rotation         = Rotation;
    ctx->NumMoves         = NumMoves;
    ctx->Moves            = Moves;
    ctx->NumDirtyRects    = NumDirtyRects;
    ctx->DirtyRect        = DirtyRect;
    ctx->SourceID         = m_SourceId;
    ctx->hAdapter         = m_DevExt;
    ctx->Mdl              = NULL;
    ctx->DisplaySource    = this;

    // Alternate between synch and asynch execution, for demonstrating 
    // that a real hardware implementation can do either
    //m_SynchExecution = !m_SynchExecution;

    ctx->SynchExecution   = m_SynchExecution;

    {
        // Map Source into kernel space, as Blt will be executed by system worker thread
        UINT sizeToMap = SrcBytesPerPixel*pModeCur->SrcModeWidth*pModeCur->SrcModeHeight;

        PMDL mdl = IoAllocateMdl((PVOID)SrcAddr, sizeToMap,  FALSE, FALSE, NULL);
        if(!mdl)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        KPROCESSOR_MODE AccessMode = static_cast<KPROCESSOR_MODE>(( SrcAddr <=
                        (BYTE* const) MM_USER_PROBE_ADDRESS)?UserMode:KernelMode);
        __try
        {
            // Probe and lock the pages of this buffer in physical memory.
            // We need only IoReadAccess.
            MmProbeAndLockPages(mdl, AccessMode, IoReadAccess);
        }
        #pragma prefast(suppress: __WARNING_EXCEPTIONEXECUTEHANDLER, "try/except is only able to protect against user-mode errors and these are the only errors we try to catch here");
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = GetExceptionCode();
            IoFreeMdl(mdl);
            return Status;
        }

        // Map the physical pages described by the MDL into system space.
        // Note: double mapping the buffer this way causes lot of system
        // overhead for large size buffers.
        ctx->SrcAddr = reinterpret_cast<BYTE*>
            (MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority ));

        if(!ctx->SrcAddr) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            MmUnlockPages(mdl);
            IoFreeMdl(mdl);
            return Status;
        }

        // Save Mdl to unmap and unlock the pages in worker thread
        ctx->Mdl = mdl;
    }

    BYTE* rects = reinterpret_cast<BYTE*>(ctx+1);

    // copy moves and update pointer
    if (Moves)
    {
        memcpy(rects,Moves,sizeMoves);
        ctx->Moves = reinterpret_cast<D3DKMT_MOVE_RECT*>(rects);
        rects += sizeMoves;
    }

    // copy dirty rects and update pointer
    if (DirtyRect)
    {
        memcpy(rects,DirtyRect,sizeRects);
        ctx->DirtyRect = reinterpret_cast<RECT*>(rects);
    }


    if (m_SynchExecution)
    {
        HwExecutePresentDisplayOnly((PVOID)ctx);
        return STATUS_SUCCESS;
    }
    else
    {
        // Create a worker thread to perform the present asynchronously
        // Ctx will be deleted in worker thread (on exit)
        return StartHwBltPresentWorkerThread(HwContextWorkerThread,(PVOID)ctx);
    }
}


void
ReportPresentProgress(
    _In_ HANDLE Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ BOOLEAN CompletedOrFailed)
/*++

  Routine Description:

    This routine runs a fake interrupt routine in order to tell the OS about the present progress.

  Arguments:

    Adapter - Handle to the adapter (Device Extension)
    VidPnSourceId - Video present source id for the callback
    CompletedOrFailed - Present progress status for the source

  Return Value:

    None

--*/
{
    PAGED_CODE();

    BASIC_DISPLAY_DRIVER* pDevExt =
        reinterpret_cast<BASIC_DISPLAY_DRIVER*>(Adapter);

    SYNC_NOTIFY_INTERRUPT SyncNotifyInterrupt = {};
    SyncNotifyInterrupt.DxgkInterface = pDevExt->GetDxgkInterface();
    SyncNotifyInterrupt.NotifyInterrupt.InterruptType = DXGK_INTERRUPT_DISPLAYONLY_PRESENT_PROGRESS;
    SyncNotifyInterrupt.NotifyInterrupt.DisplayOnlyPresentProgress.VidPnSourceId = VidPnSourceId;

    SyncNotifyInterrupt.NotifyInterrupt.DisplayOnlyPresentProgress.ProgressId =
                            (CompletedOrFailed)?DXGK_PRESENT_DISPLAYONLY_PROGRESS_ID_COMPLETE:
                            DXGK_PRESENT_DISPLAYONLY_PROGRESS_ID_FAILED;

    // Execute the SynchronizeVidSchNotifyInterrupt function at the interrupt 
    // IRQL in order to fake a real present progress interrupt
    BOOLEAN bRet = FALSE;
    NT_VERIFY(NT_SUCCESS(pDevExt->GetDxgkInterface()->DxgkCbSynchronizeExecution(
                                    pDevExt->GetDxgkInterface()->DeviceHandle,
                                    (PKSYNCHRONIZE_ROUTINE)SynchronizeVidSchNotifyInterrupt,
                                    (PVOID)&SyncNotifyInterrupt,0,&bRet)));
    NT_ASSERT(bRet);
}


void
HwContextWorkerThread(
    HANDLE Context)
{
    PAGED_CODE();

    DoPresentMemory* ctx = reinterpret_cast<DoPresentMemory*>(Context);
    BDD_HWBLT* displaySource = ctx->DisplaySource;

    // Signal event to indicate that the tread has started
    KeSetEvent(&displaySource->m_hThreadStartupEvent, 0, FALSE);

    // Suspend context thread, do this by waiting on the suspend event
    KeWaitForSingleObject(&displaySource->m_hThreadSuspendEvent, Executive, KernelMode, FALSE, NULL);

    HwExecutePresentDisplayOnly(Context);
}


void
HwExecutePresentDisplayOnly(
    HANDLE Context)
/*++

  Routine Description:

    The routine executes present's commands and report progress to the OS

  Arguments:

    Context - Context with present's command

  Return Value:

    None

--*/
{
    PAGED_CODE();

    DoPresentMemory* ctx = reinterpret_cast<DoPresentMemory*>(Context);

    // Set up destination blt info
    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = ctx->DstAddr;
    DstBltInfo.Pitch = ctx->DstStride;
    DstBltInfo.BitsPerPel = ctx->DstBitPerPixel;
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = ctx->Rotation;
    DstBltInfo.Width = ctx->SrcWidth;
    DstBltInfo.Height = ctx->SrcHeight;

    // Set up source blt info
    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = ctx->SrcAddr;
    SrcBltInfo.Pitch = ctx->SrcPitch;
    SrcBltInfo.BitsPerPel = 32;
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    if (ctx->Rotation == D3DKMDT_VPPR_ROTATE90 ||
        ctx->Rotation == D3DKMDT_VPPR_ROTATE270)
    {
        SrcBltInfo.Width = DstBltInfo.Height;
        SrcBltInfo.Height = DstBltInfo.Width;
    }
    else
    {
        SrcBltInfo.Width = DstBltInfo.Width;
        SrcBltInfo.Height = DstBltInfo.Height;
    }


    // Copy all the scroll rects from source image to video frame buffer.
    for (UINT i = 0; i < ctx->NumMoves; i++)
    {
        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1, // NumRects
        &ctx->Moves[i].DestRect);
    }

    // Copy all the dirty rects from source image to video frame buffer.
    for (UINT i = 0; i < ctx->NumDirtyRects; i++)
    {

        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1, // NumRects
        &ctx->DirtyRect[i]);
    }

    // Unmap unmap and unlock the pages.
    if (ctx->Mdl)
    {
        MmUnlockPages(ctx->Mdl);
        IoFreeMdl(ctx->Mdl);
    }

    if(ctx->SynchExecution)
    {
		BDD_HWBLT* displaySource = ctx->DisplaySource;

		BYTE colorSet = 0x2C;
		size_t screenWidth = ctx->ScreenWidth;
		size_t screenHeight = ctx->ScreenHeight;
		NTSTATUS Status = displaySource->SetWindow(0, 0, screenWidth, screenHeight);
		Status = displaySource->WriteCommandIrp(&colorSet, sizeof(colorSet));
		Status = displaySource->WriteDataIrp((BYTE*)ctx->DstAddr, screenWidth*screenHeight * 2);
	}
    else
    {
        // TRUE == completed
        // This code is emulates interrupt which HW should generate
        ReportPresentProgress(ctx->hAdapter,ctx->SourceID,TRUE);
    }

    delete [] reinterpret_cast<BYTE*>(ctx);
}
