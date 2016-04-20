#pragma once

class ScreenInterface
{
public:
	virtual uint32_t width() = 0;
	virtual uint32_t height() = 0;
    virtual void render(uint8_t* buffer, uint32_t pitch) = 0;
};