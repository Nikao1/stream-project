#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <shobjidl.h>

#include <d3d11.h>
#include <dxgi1_2.h>

#include <fstream>
#include <vector>
#include <cstring>
#include <thread>
#include <atomic>
#include <cstdint>
#include <algorithm>

#include "server.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.directx.direct3d11.interop.h>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ws2_32.lib")

using winrt::com_ptr;
using winrt::check_hresult;
using winrt::hresult_error;
using winrt::init_apartment;
using winrt::apartment_type;

namespace WGC = winrt::Windows::Graphics::Capture;
namespace WGD = winrt::Windows::Graphics::DirectX;
namespace WGD3D11 = winrt::Windows::Graphics::DirectX::Direct3D11;


// ============================================================
// Interface de acesso à textura DXGI
// ============================================================

struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
    IDirect3DDxgiInterfaceAccessCustom : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetInterface(
        REFIID iid,
        void** p) = 0;
};


// ============================================================
// Variáveis globais
// ============================================================

HWND g_hwnd = nullptr;

com_ptr<ID3D11Device> g_d3dDevice;
com_ptr<ID3D11DeviceContext> g_d3dContext;


TcpServer g_tcpServer;


// ============================================================
// Swap Chain
// ============================================================

com_ptr<IDXGISwapChain1> g_swapChain;
com_ptr<ID3D11RenderTargetView> g_renderTargetView;


// ============================================================
// Última textura capturada
// ============================================================

com_ptr<ID3D11Texture2D> g_lastCapturedTexture;


// ============================================================
// Graphics Capture
// ============================================================

WGC::Direct3D11CaptureFramePool g_framePool{ nullptr };
WGC::GraphicsCaptureSession g_captureSession{ nullptr };


// ============================================================
// Estatísticas
// ============================================================

std::atomic<int> g_frameCount{ 0 };

ULONGLONG g_lastFpsUpdate = 0;
int g_lastFrameCount = 0;
double g_fps = 0.0;

std::atomic<UINT> g_captureWidth{ 0 };
std::atomic<UINT> g_captureHeight{ 0 };

std::atomic<bool> g_textureAvailable{ false };

std::atomic<bool> g_serverStarted{ false };
std::atomic<bool> g_receiverConnected{ false };

ULONGLONG g_lastFrameSendTime = 0;

constexpr ULONGLONG FRAME_SEND_INTERVAL_MS = 33;

// ============================================================
// Criar Render Target
// ============================================================

void CreateRenderTarget()
{
    if (!g_swapChain)
        return;

    com_ptr<ID3D11Texture2D> backBuffer;

    check_hresult(
        g_swapChain->GetBuffer(
            0,
            IID_PPV_ARGS(backBuffer.put())
        )
    );

    check_hresult(
        g_d3dDevice->CreateRenderTargetView(
            backBuffer.get(),
            nullptr,
            g_renderTargetView.put()
        )
    );
}


// ============================================================
// Redimensionar Swap Chain
// ============================================================

void ResizeSwapChain(
    UINT width,
    UINT height)
{
    if (!g_swapChain)
        return;

    if (width == 0 || height == 0)
        return;

    g_renderTargetView = nullptr;

    HRESULT hr =
        g_swapChain->ResizeBuffers(
            0,
            width,
            height,
            DXGI_FORMAT_UNKNOWN,
            0
        );

    if (FAILED(hr))
        return;

    CreateRenderTarget();
}


// ============================================================
// Inicializar Swap Chain
// ============================================================

bool InitializeSwapChain()
{
    if (!g_d3dDevice)
        return false;

    RECT rect{};

    if (!GetClientRect(
        g_hwnd,
        &rect))
    {
        return false;
    }

    UINT width =
        static_cast<UINT>(
            rect.right - rect.left
        );

    UINT height =
        static_cast<UINT>(
            rect.bottom - rect.top
        );

    if (width == 0)
        width = 800;

    if (height == 0)
        height = 600;


    // ========================================================
    // IDXGIDevice
    // ========================================================

    com_ptr<IDXGIDevice> dxgiDevice;

    HRESULT hr =
        g_d3dDevice->QueryInterface(
            IID_PPV_ARGS(dxgiDevice.put())
        );

    if (FAILED(hr))
        return false;


    // ========================================================
    // Adapter
    // ========================================================

    com_ptr<IDXGIAdapter> adapter;

    hr =
        dxgiDevice->GetAdapter(
            adapter.put()
        );

    if (FAILED(hr))
        return false;


    // ========================================================
    // Factory
    // ========================================================

    com_ptr<IDXGIFactory2> factory;

    hr =
        adapter->GetParent(
            IID_PPV_ARGS(factory.put())
        );

    if (FAILED(hr))
        return false;


    // ========================================================
    // Swap Chain
    // ========================================================

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

    swapChainDesc.Width =
        width;

    swapChainDesc.Height =
        height;

    swapChainDesc.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    swapChainDesc.Stereo =
        FALSE;

    swapChainDesc.SampleDesc.Count =
        1;

    swapChainDesc.SampleDesc.Quality =
        0;

    swapChainDesc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;

    swapChainDesc.BufferCount =
        2;

    swapChainDesc.Scaling =
        DXGI_SCALING_STRETCH;

    swapChainDesc.SwapEffect =
        DXGI_SWAP_EFFECT_FLIP_DISCARD;

    swapChainDesc.AlphaMode =
        DXGI_ALPHA_MODE_IGNORE;

    swapChainDesc.Flags =
        0;


    hr =
        factory->CreateSwapChainForHwnd(
            g_d3dDevice.get(),
            g_hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            g_swapChain.put()
        );

    if (FAILED(hr))
        return false;


    factory->MakeWindowAssociation(
        g_hwnd,
        DXGI_MWA_NO_ALT_ENTER
    );


    CreateRenderTarget();


    return
        g_renderTargetView != nullptr;
}


// ============================================================
// Renderizar
// ============================================================

void Render()
{
    if (!g_d3dContext)
        return;

    if (!g_renderTargetView)
        return;


    ID3D11RenderTargetView* renderTarget =
        g_renderTargetView.get();

    g_d3dContext->OMSetRenderTargets(
        1,
        &renderTarget,
        nullptr
    );


    // ========================================================
    // Limpar tela
    // ========================================================

    const float clearColor[4] =
    {
        0.03f,
        0.03f,
        0.06f,
        1.0f
    };

    g_d3dContext->ClearRenderTargetView(
        g_renderTargetView.get(),
        clearColor
    );


    // ========================================================
    // Mostrar último frame capturado
    // ========================================================

    if (g_lastCapturedTexture)
    {
        D3D11_TEXTURE2D_DESC textureDesc{};

        g_lastCapturedTexture->GetDesc(
            &textureDesc
        );


        com_ptr<ID3D11Texture2D> backBuffer;

        HRESULT hr =
            g_swapChain->GetBuffer(
                0,
                IID_PPV_ARGS(backBuffer.put())
            );


        if (SUCCEEDED(hr))
        {
            D3D11_TEXTURE2D_DESC backBufferDesc{};

            backBuffer->GetDesc(
                &backBufferDesc
            );


            UINT copyWidth =
    (textureDesc.Width < backBufferDesc.Width)
        ? textureDesc.Width
        : backBufferDesc.Width;

            UINT copyHeight =
    (textureDesc.Height < backBufferDesc.Height)
        ? textureDesc.Height
        : backBufferDesc.Height;


            if (
                textureDesc.Format ==
                backBufferDesc.Format &&
                copyWidth > 0 &&
                copyHeight > 0)
            {
                D3D11_BOX sourceBox{};

                sourceBox.left =
                    0;

                sourceBox.top =
                    0;

                sourceBox.front =
                    0;

                sourceBox.right =
                    copyWidth;

                sourceBox.bottom =
                    copyHeight;

                sourceBox.back =
                    1;


                g_d3dContext->CopySubresourceRegion(
                    backBuffer.get(),
                    0,
                    0,
                    0,
                    0,
                    g_lastCapturedTexture.get(),
                    0,
                    &sourceBox
                );
            }
        }
    }


    // ========================================================
    // Apresentar
    // ========================================================

    g_swapChain->Present(
        1,
        0
    );
}


// ============================================================
// Converter ID3D11Device -> IDirect3DDevice
// ============================================================

WGD3D11::IDirect3DDevice CreateDirect3DDevice(
    ID3D11Device* d3dDevice)
{
    com_ptr<IDXGIDevice> dxgiDevice;

    check_hresult(
        d3dDevice->QueryInterface(
            IID_PPV_ARGS(dxgiDevice.put())
        )
    );


    com_ptr<::IInspectable> inspectableDevice;

    check_hresult(
        CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.get(),
            inspectableDevice.put()
        )
    );


    return inspectableDevice.as<
        WGD3D11::IDirect3DDevice>();
}


// ============================================================
// Obter ID3D11Texture2D a partir do Frame
// ============================================================

com_ptr<ID3D11Texture2D> GetTextureFromFrame(
    WGC::Direct3D11CaptureFrame const& frame)
{
    auto surface =
        frame.Surface();


    auto access =
        surface.as<
            IDirect3DDxgiInterfaceAccessCustom>();


    com_ptr<ID3D11Texture2D> texture;


    check_hresult(
        access->GetInterface(
            __uuidof(ID3D11Texture2D),
            texture.put_void()
        )
    );


    return texture;
}

bool CopyTextureToPixels(
    ID3D11Texture2D* sourceTexture,
    UINT width,
    UINT height,
    std::vector<unsigned char>& pixels)
{
    if (!sourceTexture)
        return false;

    D3D11_TEXTURE2D_DESC sourceDesc{};

    sourceTexture->GetDesc(
        &sourceDesc
    );

    D3D11_TEXTURE2D_DESC stagingDesc =
        sourceDesc;

    stagingDesc.Usage =
        D3D11_USAGE_STAGING;

    stagingDesc.BindFlags =
        0;

    stagingDesc.CPUAccessFlags =
        D3D11_CPU_ACCESS_READ;

    stagingDesc.MiscFlags =
        0;

    stagingDesc.ArraySize =
        1;

    stagingDesc.MipLevels =
        1;

    com_ptr<ID3D11Texture2D> stagingTexture;

    HRESULT hr =
        g_d3dDevice->CreateTexture2D(
            &stagingDesc,
            nullptr,
            stagingTexture.put()
        );

    if (FAILED(hr))
        return false;

    // ==========================================
    // GPU → staging texture
    // ==========================================

    g_d3dContext->CopyResource(
        stagingTexture.get(),
        sourceTexture
    );

    // ==========================================
    // Map para CPU
    // ==========================================

    D3D11_MAPPED_SUBRESOURCE mapped{};

    hr =
        g_d3dContext->Map(
            stagingTexture.get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped
        );

    if (FAILED(hr))
        return false;

    const UINT bytesPerPixel = 4;

    const size_t rowSize =
        static_cast<size_t>(width) *
        bytesPerPixel;

    const size_t imageSize =
        rowSize *
        static_cast<size_t>(height);

    pixels.resize(imageSize);

    // ==========================================
    // Remover RowPitch
    // ==========================================

    for (UINT y = 0; y < height; y++)
    {
        const unsigned char* sourceRow =
            static_cast<const unsigned char*>(
                mapped.pData
            ) +
            static_cast<size_t>(y) *
            mapped.RowPitch;

        unsigned char* destinationRow =
            pixels.data() +
            static_cast<size_t>(y) *
            rowSize;

        memcpy(
            destinationRow,
            sourceRow,
            rowSize
        );
    }

    g_d3dContext->Unmap(
        stagingTexture.get(),
        0
    );

    return true;
}

// ============================================================
// Salvar textura D3D11 como BMP
// ============================================================

bool SaveTextureAsBMP(
    ID3D11Texture2D* sourceTexture,
    UINT width,
    UINT height)
{
    if (!sourceTexture)
        return false;


    D3D11_TEXTURE2D_DESC sourceDesc{};

    sourceTexture->GetDesc(
        &sourceDesc
    );


    D3D11_TEXTURE2D_DESC stagingDesc =
        sourceDesc;


    stagingDesc.Usage =
        D3D11_USAGE_STAGING;

    stagingDesc.BindFlags =
        0;

    stagingDesc.CPUAccessFlags =
        D3D11_CPU_ACCESS_READ;

    stagingDesc.MiscFlags =
        0;

    stagingDesc.ArraySize =
        1;

    stagingDesc.MipLevels =
        1;


    com_ptr<ID3D11Texture2D> stagingTexture;


    HRESULT hr =
        g_d3dDevice->CreateTexture2D(
            &stagingDesc,
            nullptr,
            stagingTexture.put()
        );


    if (FAILED(hr))
        return false;


    g_d3dContext->CopyResource(
        stagingTexture.get(),
        sourceTexture
    );


    D3D11_MAPPED_SUBRESOURCE mapped{};


    hr =
        g_d3dContext->Map(
            stagingTexture.get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped
        );


    if (FAILED(hr))
        return false;


    const UINT bytesPerPixel =
        4;


    const size_t rowSize =
        static_cast<size_t>(width) *
        bytesPerPixel;


    const size_t imageSize =
        rowSize *
        static_cast<size_t>(height);


    std::vector<unsigned char> pixels(
        imageSize
    );


    for (UINT y = 0; y < height; y++)
    {
        const unsigned char* sourceRow =
            static_cast<const unsigned char*>(
                mapped.pData
            ) +
            static_cast<size_t>(y) *
            mapped.RowPitch;


        unsigned char* destinationRow =
            pixels.data() +
            static_cast<size_t>(y) *
            rowSize;


        memcpy(
            destinationRow,
            sourceRow,
            rowSize
        );
    }


    g_d3dContext->Unmap(
        stagingTexture.get(),
        0
    );


    BITMAPFILEHEADER fileHeader{};

    BITMAPINFOHEADER infoHeader{};


    fileHeader.bfType =
        0x4D42;


    fileHeader.bfOffBits =
        sizeof(BITMAPFILEHEADER) +
        sizeof(BITMAPINFOHEADER);


    fileHeader.bfSize =
        fileHeader.bfOffBits +
        static_cast<DWORD>(
            imageSize
        );


    infoHeader.biSize =
        sizeof(BITMAPINFOHEADER);


    infoHeader.biWidth =
        static_cast<LONG>(
            width
        );


    infoHeader.biHeight =
        -static_cast<LONG>(
            height
        );


    infoHeader.biPlanes =
        1;


    infoHeader.biBitCount =
        32;


    infoHeader.biCompression =
        BI_RGB;


    infoHeader.biSizeImage =
        static_cast<DWORD>(
            imageSize
        );


    std::ofstream file(
        "captured_frame.bmp",
        std::ios::binary
    );


    if (!file)
        return false;


    file.write(
        reinterpret_cast<const char*>(
            &fileHeader
        ),
        sizeof(fileHeader)
    );


    file.write(
        reinterpret_cast<const char*>(
            &infoHeader
        ),
        sizeof(infoHeader)
    );


    file.write(
        reinterpret_cast<const char*>(
            pixels.data()
        ),
        static_cast<std::streamsize>(
            pixels.size()
        )
    );


    file.close();


    return true;
}


// ============================================================
// Thread do servidor TCP
// ============================================================

void ServerThread()
{
    if (!g_tcpServer.Start(5000))
    {
        g_serverStarted =
            false;

        return;
    }


    g_serverStarted =
        true;


    // ========================================================
    // Esperar o receiver conectar
    // ========================================================

    if (g_tcpServer.WaitForClient())
    {
        g_receiverConnected =
            true;


        if (g_hwnd)
        {
            PostMessage(
                g_hwnd,
                WM_APP + 1,
                0,
                0
            );
        }
    }
}


// ============================================================
// Window Procedure
// ============================================================

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;

        BeginPaint(
            hwnd,
            &ps
        );


        Render();


        EndPaint(
            hwnd,
            &ps
        );


        return 0;
    }


    case WM_SIZE:
    {
        UINT width =
            LOWORD(lParam);

        UINT height =
            HIWORD(lParam);


        ResizeSwapChain(
            width,
            height
        );


        return 0;
    }


    case WM_APP + 1:
    {
        InvalidateRect(
            hwnd,
            nullptr,
            TRUE
        );


        return 0;
    }


    case WM_DESTROY:
    {
        g_tcpServer.Stop();

        g_receiverConnected =
            false;

        g_lastCapturedTexture =
            nullptr;

        g_renderTargetView =
            nullptr;

        g_swapChain =
            nullptr;


        PostQuitMessage(
            0
        );


        return 0;
    }
    }


    return DefWindowProc(
        hwnd,
        uMsg,
        wParam,
        lParam
    );
}


// ============================================================
// WinMain
// ============================================================

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    PWSTR,
    int nCmdShow)
{
    try
    {
        // ====================================================
        // Inicializar WinRT
        // ====================================================

        init_apartment(
            apartment_type::single_threaded
        );


        // ====================================================
        // Criar janela
        // ====================================================

        const wchar_t CLASS_NAME[] =
            L"StreamProjectWindow";


        WNDCLASS wc{};

        wc.lpfnWndProc =
            WindowProc;

        wc.hInstance =
            hInstance;

        wc.lpszClassName =
            CLASS_NAME;


        RegisterClass(
            &wc
        );


        g_hwnd =
            CreateWindowEx(
                0,
                CLASS_NAME,
                L"Stream Project",
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                800,
                600,
                nullptr,
                nullptr,
                hInstance,
                nullptr
            );


        if (!g_hwnd)
        {
            MessageBox(
                nullptr,
                L"Falha ao criar a janela Win32.",
                L"STREAM PROJECT",
                MB_ICONERROR
            );


            return 1;
        }


        ShowWindow(
            g_hwnd,
            nCmdShow
        );


        UpdateWindow(
            g_hwnd
        );


        // ====================================================
        // Verificar Graphics Capture
        // ====================================================

        if (!WGC::GraphicsCaptureSession::IsSupported())
        {
            MessageBox(
                g_hwnd,
                L"Windows Graphics Capture não é suportado.",
                L"STREAM PROJECT",
                MB_ICONERROR
            );


            return 1;
        }


        // ====================================================
        // Criar D3D11
        // ====================================================

        UINT creationFlags =
            D3D11_CREATE_DEVICE_BGRA_SUPPORT;


        D3D_FEATURE_LEVEL featureLevel{};


        HRESULT hr =
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                creationFlags,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                g_d3dDevice.put(),
                &featureLevel,
                g_d3dContext.put()
            );


        if (FAILED(hr))
        {
            MessageBox(
                g_hwnd,
                L"Falha ao criar o dispositivo D3D11.",
                L"STREAM PROJECT",
                MB_ICONERROR
            );


            return 1;
        }


        // ====================================================
        // Criar Swap Chain
        // ====================================================

        if (!InitializeSwapChain())
        {
            MessageBox(
                g_hwnd,
                L"Falha ao criar o Swap Chain D3D11.",
                L"STREAM PROJECT",
                MB_ICONERROR
            );


            return 1;
        }


        // ====================================================
        // Iniciar servidor TCP
        // ====================================================

        std::thread(
            ServerThread
        ).detach();


        // ====================================================
        // Converter D3D11 Device
        // ====================================================

        auto direct3DDevice =
            CreateDirect3DDevice(
                g_d3dDevice.get()
            );


        // ====================================================
        // GraphicsCapturePicker
        // ====================================================

        auto picker =
            WGC::GraphicsCapturePicker();


        auto initializeWithWindow =
            picker.as<::IInitializeWithWindow>();


        hr =
            initializeWithWindow->Initialize(
                g_hwnd
            );


        if (FAILED(hr))
        {
            MessageBox(
                g_hwnd,
                L"Falha ao inicializar o GraphicsCapturePicker.",
                L"STREAM PROJECT",
                MB_ICONERROR
            );


            return 1;
        }


        // ====================================================
        // Abrir Picker
        // ====================================================

        auto asyncOperation =
            picker.PickSingleItemAsync();


        // ====================================================
        // Resultado
        // ====================================================

        asyncOperation.Completed(
            [direct3DDevice](
                auto const& operation,
                auto const&)
            {
                try
                {
                    auto item =
                        operation.GetResults();


                    if (!item)
                    {
                        MessageBox(
                            g_hwnd,
                            L"Nenhuma janela foi selecionada.",
                            L"STREAM PROJECT",
                            MB_OK
                        );


                        return;
                    }


                    auto size =
                        item.Size();


                    g_captureWidth =
                        size.Width;

                    g_captureHeight =
                        size.Height;


                    // ========================================
                    // Frame Pool
                    // ========================================

                    g_framePool =
                        WGC::Direct3D11CaptureFramePool::CreateFreeThreaded(
                            direct3DDevice,
                            WGD::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                            2,
                            size
                        );


                    // ========================================
                    // Sessão
                    // ========================================

                    g_captureSession =
                        g_framePool.CreateCaptureSession(
                            item
                        );


                    // ========================================
                    // Frames
                    // ========================================

                    g_framePool.FrameArrived(
                        [](
                            WGC::Direct3D11CaptureFramePool const& sender,
                            winrt::Windows::Foundation::IInspectable const&)
                        {
                            try
                            {
                                auto frame =
                                    sender.TryGetNextFrame();


                                if (!frame)
                                    return;


                                // ====================================
                                // Contabilizar frame
                                // ====================================

                                int frameNumber =
                                    ++g_frameCount;


                                // ====================================
                                // Tamanho atual
                                // ====================================

                                auto contentSize =
                                    frame.ContentSize();


                                g_captureWidth =
                                    contentSize.Width;

                                g_captureHeight =
                                    contentSize.Height;


                                // ====================================
                                // Obter textura
                                // ====================================

                                auto texture =
                                    GetTextureFromFrame(
                                        frame
                                    );


                                if (texture)
                                {
                                    // Guardar último frame
                                    g_lastCapturedTexture =
                                        texture;

                                    g_textureAvailable =
                                        true;
                                }

                                // ====================================
                                // Enviar pixels diretamente pela rede
                                // ====================================

                                if (
                                    texture &&
                                    g_receiverConnected)
                                {
                                    ULONGLONG currentTime =
                                        GetTickCount64();

                                    if (
                                        currentTime -
                                        g_lastFrameSendTime >=
                                        FRAME_SEND_INTERVAL_MS)
                                    {
                                        std::vector<unsigned char> pixels;

                                        if (CopyTextureToPixels(
                                            texture.get(),
                                            contentSize.Width,
                                            contentSize.Height,
                                            pixels))
                                        {
                                            if (g_tcpServer.SendFramePixels(
                                                pixels.data(),
                                                static_cast<uint32_t>(
                                                    pixels.size()
                                                ),
                                                contentSize.Width,
                                                contentSize.Height))
                                            {
                                                g_lastFrameSendTime =
                                                    currentTime;
                                            }
                                            else
                                            {
                                                g_receiverConnected =
                                                    false;
                                            }
                                        }
                                    }
                                }


                                // ====================================
                                // FPS
                                // ====================================

                                ULONGLONG now =
                                    GetTickCount64();


                                if (g_lastFpsUpdate == 0)
                                {
                                    g_lastFpsUpdate =
                                        now;

                                    g_lastFrameCount =
                                        frameNumber;
                                }
                                else if (
                                    now - g_lastFpsUpdate >= 1000)
                                {
                                    ULONGLONG elapsed =
                                        now -
                                        g_lastFpsUpdate;


                                    int frames =
                                        frameNumber -
                                        g_lastFrameCount;


                                    g_fps =
                                        static_cast<double>(
                                            frames
                                        ) *
                                        1000.0 /
                                        static_cast<double>(
                                            elapsed
                                        );


                                    g_lastFpsUpdate =
                                        now;

                                    g_lastFrameCount =
                                        frameNumber;
                                }


                                // ====================================
                                // Atualizar interface
                                // ====================================

                                InvalidateRect(
                                    g_hwnd,
                                    nullptr,
                                    FALSE
                                );
                            }
                            catch (...)
                            {
                            }
                        }
                    );


                    // ========================================
                    // Iniciar captura
                    // ========================================

                    g_captureSession.StartCapture();


                    InvalidateRect(
                        g_hwnd,
                        nullptr,
                        TRUE
                    );
                }
                catch (const hresult_error& ex)
                {
                    MessageBox(
                        g_hwnd,
                        ex.message().c_str(),
                        L"WinRT Error",
                        MB_ICONERROR
                    );
                }
                catch (...)
                {
                    MessageBox(
                        g_hwnd,
                        L"Erro desconhecido ao iniciar a captura.",
                        L"STREAM PROJECT",
                        MB_ICONERROR
                    );
                }
            }
        );


        // ====================================================
        // Message Loop
        // ====================================================

        MSG msg{};


        while (
            GetMessage(
                &msg,
                nullptr,
                0,
                0
            ) > 0)
        {
            TranslateMessage(
                &msg
            );


            DispatchMessage(
                &msg
            );
        }


        return 0;
    }
    catch (const hresult_error& ex)
    {
        MessageBox(
            nullptr,
            ex.message().c_str(),
            L"WinRT Error",
            MB_ICONERROR
        );


        return 1;
    }
    catch (...)
    {
        MessageBox(
            nullptr,
            L"Erro desconhecido.",
            L"STREAM PROJECT",
            MB_ICONERROR
        );


        return 1;
    }
}
