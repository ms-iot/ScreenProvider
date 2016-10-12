// ConsoleApplication1.cpp : Defines the entry point for the console application.
//

#include "pch.h"
#include "hiddevice.h"
#include "sendinput.h"
#include "ScreenInterface.h"
#include "ScreenRedirector.h"
#include "AdafruitTFT.h"

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Devices::Spi;
using namespace Windows::UI;

const uint32_t kFramerate = (1 * 1000) / 30;

int main(Platform::Array<Platform::String^>^ args)
{
    AdafruitTFT* tft = new AdafruitTFT();
    ScreenRedirector* redirector = new ScreenRedirector(320, 200, tft);

    if (tft->initialize() && redirector->initialize())
    {

		tft->clear(Colors::Blue);
    }

	while (true)
	{
		redirector->processNextFrame();

		Sleep(kFramerate);
	}

//    if (OpenHidInjectorDevice())
    {
//        IoTInjectTouchInput(1, )

    }
}
