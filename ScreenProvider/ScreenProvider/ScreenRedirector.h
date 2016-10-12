#pragma once
#include "ScreenInterface.h"

class ScreenRedirector
{
    Microsoft::WRL::ComPtr<ID3D11Device> _d3dDevice;
    Microsoft::WRL::ComPtr<IDXGIOutput1> _output;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> _duplication;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> _texture;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> _context;
    uint32_t _screenSizeX;
    uint32_t _screenSizeY;
    ScreenInterface* _screenInterface;
public:
    ScreenRedirector(uint32_t screenSizeX, uint32_t screenSizeY, ScreenInterface* screenInterface);
    virtual ~ScreenRedirector();

    bool initialize();

    bool processNextFrame();
};

