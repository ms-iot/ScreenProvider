#include "pch.h"
#include "ScreenRedirector.h"

using namespace Microsoft::WRL;
const uint32_t kScreenTimeout = 100;

ScreenRedirector::ScreenRedirector(uint32_t screenSizeX, uint32_t screenSizeY, ScreenInterface* screenInterface)
{
    _screenSizeX = screenSizeX;
    _screenSizeY = screenSizeY;
    _screenInterface = screenInterface;
}


ScreenRedirector::~ScreenRedirector()
{
}

bool ScreenRedirector::initialize()
{
    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDXGIAdapter1> adapter;

    if (FAILED(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void**)factory.GetAddressOf())))
    {
        wprintf(L"Failed on call to CreateDXGIFactory2\n");
        return false;
    }

    UINT i = 0;
    HRESULT hr = S_OK;
    while ((_output == nullptr) && ((hr = factory->EnumAdapters1(i, adapter.GetAddressOf())) != DXGI_ERROR_NOT_FOUND))
    {
        if (FAILED(hr))
        {
            printf("Failed to enumerate adapters");
        }

        UINT j = 0;
        ComPtr<IDXGIOutput> tempOutput;
        while ((_output == nullptr) && ((hr = adapter->EnumOutputs(j, tempOutput.GetAddressOf())) != DXGI_ERROR_NOT_FOUND))
        {
            if (FAILED(hr)) break;    // just try a different adapter

            DXGI_OUTPUT_DESC desc;
            hr = tempOutput->GetDesc(&desc);
            if (FAILED(hr)) break;    // just try a different adapter

            if (desc.AttachedToDesktop)
            {
                hr = tempOutput.As(&_output);
                if (FAILED(hr))
                {
                    break;
                }

                D3D_FEATURE_LEVEL deviceLevel;
                static const D3D_FEATURE_LEVEL acceptableLevels[] =
                {
                    D3D_FEATURE_LEVEL_11_1,
                    D3D_FEATURE_LEVEL_11_0,
                    D3D_FEATURE_LEVEL_10_1,
                    D3D_FEATURE_LEVEL_10_0,
                    D3D_FEATURE_LEVEL_9_3,
                    D3D_FEATURE_LEVEL_9_2,
                    D3D_FEATURE_LEVEL_9_1
                };

                DWORD flags = 0; //When Pi Gets hardware video, reenable D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
                HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, acceptableLevels, ARRAYSIZE(acceptableLevels), D3D11_SDK_VERSION, _d3dDevice.GetAddressOf(), &deviceLevel, nullptr);
                if (FAILED(hr))
                {
                    wprintf(L"Failed on call to D3D11CreateDevice\n");
                    break;
                }
            }
        }
    }

    if (!_output)
    {
        wprintf(L"Could not find a decent dx output\n");
    }

    if (SUCCEEDED(_output->DuplicateOutput(_d3dDevice.Get(), _duplication.GetAddressOf())))
    {

        _d3dDevice->GetImmediateContext(_context.GetAddressOf());


        return true;

    }
    else
    {
        wprintf(L"Could not duplicate output\n");
    }


    return false;
}

bool ScreenRedirector::processNextFrame()
{
    ComPtr<IDXGIResource> nextFrame;
    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};

    HRESULT hr = _duplication->AcquireNextFrame(kScreenTimeout, &frameInfo, &nextFrame);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT || hr == DXGI_ERROR_INVALID_CALL)
    {
        return false;
    }

    if (!_texture)
    {
		DXGI_OUTDUPL_DESC desc;
		_duplication->GetDesc(&desc);

		D3D11_TEXTURE2D_DESC desc;
        desc.Width = static_cast<UINT>(_screenSizeX);
        desc.Height = static_cast<UINT>(_screenSizeY);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        _d3dDevice->CreateTexture2D(&desc, 0, &_texture);
    }

    ComPtr<ID3D11Resource> nextFrameResource;
    nextFrame.As(&nextFrameResource);


    _context->CopyResource(_texture.Get(), nextFrameResource.Get());

    ComPtr<IDXGISurface> surface;
    _texture.As(&surface);

    DXGI_MAPPED_RECT rect;
	hr = surface->Map(&rect, DXGI_MAP_READ);
    if (SUCCEEDED(hr))
    {
		_screenInterface->render(rect.pBits, rect.Pitch);



        surface->Unmap();
    }


    _duplication->ReleaseFrame();

    return false;
}

