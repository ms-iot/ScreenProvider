#include "pch.h"
#include "ScreenInterface.h"
#include "AdafruitTFT.h"

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Devices::Spi;
using namespace Windows::Devices::Gpio;
using namespace Windows::UI;

const int SPI_CHIP_SELECT_LINE = 0;       /* Line 0 maps to physical pin number 24 on the Rpi2        */
const int DATA_COMMAND_PIN = 25;          /* We use GPIO 25 since it's conveniently near the SPI pins */
const int PhysicalMaxWidth = 240;
const int PhysicalMaxHeight = 320;

const int kResetDelay = 5;
byte resetCommand = 0x1;
byte clearDisplayCommand = 0x28;

AdafruitTFT::AdafruitTFT()
{
}


AdafruitTFT::~AdafruitTFT()
{
}

bool AdafruitTFT::initialize()
{
    auto controller = GpioController::GetDefault();

    _commandPin = controller->OpenPin(DATA_COMMAND_PIN);
    if (_commandPin == nullptr)
    {
        return false;
    }

	_commandPin->Write(GpioPinValue::Low);
	_commandPin->SetDriveMode(GpioPinDriveMode::Output);

    using namespace Windows::Devices::Enumeration;
    String^ aqs = SpiDevice::GetDeviceSelector(L"SPI0");

    auto dis = concurrency::create_task(
        DeviceInformation::FindAllAsync(aqs)).get();
    if (dis->Size < 1)
    {
        return false;
    }

    String^ id = dis->GetAt(0)->Id;

    auto settings = ref new SpiConnectionSettings(SPI_CHIP_SELECT_LINE);

    settings->Mode = SpiMode::Mode3;
    settings->ClockFrequency = 10000000;

    _device = concurrency::create_task(
        SpiDevice::FromIdAsync(id, settings)).get();


    if (_device != nullptr)
    {

        sendStartupSequence();

        return true;
    }

    return false;
}

void AdafruitTFT::sendStartupSequence()
{
    // Startup Sequence
    sendC(resetCommand);
    Sleep(kResetDelay);
    sendC(clearDisplayCommand);

    sendC(0xCF);
    sendD({ 0x00, 0x83, 0x30 });
    sendC(0xED);
    sendD({ 0x64, 0x03, 0x12, 0x81 });
    sendC(0xE8);
    sendD({ 0x85, 0x01, 0x79 });
    sendC(0xCB);
    sendD({ 0x39, 0X2C, 0x00, 0x34, 0x02 });
    sendC(0xF7);
    sendD({ 0x20 });
    sendC(0xEA);
    sendD({ 0x00, 0x00 });
    /* ------------power control-------------------------------- */
    sendC(0xC0);
    sendD({ 0x26 });
    sendC(0xC1);
    sendD({ 0x11 });
    /* ------------VCOM --------- */
    sendC(0xC5);
    sendD({ 0x35, 0x3E });
    sendC(0xC7);
    sendD({ 0xBE });
    /* ------------memory access control------------------------ */
    sendC(0x3A);
    sendD({ 0x55 }); /* 16bit pixel */
                               /* ------------frame rate----------------------------------- */
    sendC(0xB1);
    sendD({ 0x00, 0x1B });
    /* ------------Gamma---------------------------------------- */
    /* sendD(new byte[] {0xF2, 0x08); */ /* Gamma Function Disable */
    sendC(0x26);
    sendD({ 0x01 });
    /* ------------display-------------------------------------- */
    sendC(0xB7);
    sendD({ 0x07 }); /* entry mode set */
    sendC(0xB6);
    sendD({ 0x0A, 0x82, 0x27, 0x00 });
    sendC(0x11); /* sleep out */
    Sleep(100);
    sendC(0x29); /* display on */
    Sleep(20);

    setOrientation(AdaFruitTFTOrientation_Landscape);

}

uint16_t get565Color(Color color)
{
	uint32_t color565 = (((color.R & 0xF8) << 8) | ((color.B & 0xFC) << 3) | ((color.G & 0xF8) >> 3));
	return  (uint16_t)(color565);
}

void AdafruitTFT::clear(Color color)
{
	setWindow(0, 0, width(), height());

	uint32_t numPixels = width() * height();
	uint16_t color565 = get565Color(color);
	sendC(0x2C);

	std::vector<uint16_t> frame(numPixels);

	std::fill(frame.begin(), frame.end(), color565);

	sendD(frame);
}


void AdafruitTFT::render(uint8_t* buffer, uint32_t pitch)
{


}

void AdafruitTFT::setOrientation(AdaFruitTFTOrientation o)
{
	sendC(0x36);
	switch (o)
	{
	case AdaFruitTFTOrientation_Portrait:
		sendC(0x48);
		break;
	case AdaFruitTFTOrientation_Landscape:
		sendC(0x28);
		break;
	case AdaFruitTFTOrientation_PortraitInverted:
		sendC(0x88);
		break;
	case AdaFruitTFTOrientation_LandscapeInverted:
		sendC(0xE8);
		break;
	}
}

void AdafruitTFT::setWindow(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	int windowWidth = (x + w - 1);
	int windowHeight = (y + h - 1);
	sendC(0x2A);
	sendD({
		(byte)((x >> 8) & 0xFF),
		(byte)(x & 0xFF),
		(byte)((windowWidth >> 8) & 0xFF),
		(byte)(windowWidth & 0xFF)
	});

	sendC(0x2B);
	sendD({
		(byte)((y >> 8) & 0xFF),
		(byte)(y & 0xFF),
		(byte)((windowHeight >> 8) & 0xFF),
		(byte)(windowHeight & 0xFF)
	});
}

void AdafruitTFT::sendC(byte cmd)
{
	byte cmdArray[] = { cmd };
    _commandPin->Write(GpioPinValue::Low);

	_device->Write(ArrayReference<BYTE>(cmdArray, 1));
    _commandPin->Write(GpioPinValue::High);
}

void AdafruitTFT::sendD(std::initializer_list<BYTE> a_args)
{
    _device->Write(ArrayReference<BYTE>(
        const_cast<BYTE*>(a_args.begin()), // yuck
        static_cast<unsigned int>(a_args.size())));
}

void AdafruitTFT::sendD(uint8_t* bytes, size_t count)
{
	_device->Write(ArrayReference<BYTE>(bytes, count));
}

void AdafruitTFT::sendD(std::vector<uint16_t>& frame)
{
	BYTE* data = reinterpret_cast<BYTE*>(frame.data());
	_device->Write(ArrayReference<BYTE>(data, frame.size() * sizeof(uint16_t)));
}
