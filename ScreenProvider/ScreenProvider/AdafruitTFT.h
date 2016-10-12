#pragma once

typedef enum 
{
    AdaFruitTFTOrientation_Portrait = 0,
    AdaFruitTFTOrientation_Landscape = 1,
    AdaFruitTFTOrientation_PortraitInverted = 2,
    AdaFruitTFTOrientation_LandscapeInverted = 3
} AdaFruitTFTOrientation;



class AdafruitTFT : public ScreenInterface
{
	Windows::Devices::Spi::SpiDevice^ _device;
	Windows::Devices::Gpio::GpioPin^ _commandPin;

    void sendStartupSequence();
    void sendC(byte cmd);
    void sendD(std::initializer_list<byte> a_args);
	void sendD(uint8_t* bytes, size_t count);
	void sendD(std::vector<uint16_t> & frame);
	
	const uint32_t kPhysicalWidth = 320;
	const uint32_t kPhysicalHeight = 240;

	AdaFruitTFTOrientation _orientation;

public:
    AdafruitTFT();
    ~AdafruitTFT();

    bool initialize();

	virtual uint32_t width()
	{
		if (_orientation == AdaFruitTFTOrientation_Portrait || _orientation == AdaFruitTFTOrientation_PortraitInverted)
			return kPhysicalWidth;

		return kPhysicalHeight;
	}

	virtual uint32_t height()
	{
		if (_orientation == AdaFruitTFTOrientation_Portrait || _orientation == AdaFruitTFTOrientation_PortraitInverted)
			return kPhysicalHeight;

		return kPhysicalWidth;
	}

    void setOrientation(AdaFruitTFTOrientation o);
	void clear(Windows::UI::Color color);
	void setWindow(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

    virtual void render(uint8_t* buffer, uint32_t pitch);
};

