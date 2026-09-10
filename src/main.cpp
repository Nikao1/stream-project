#include "udp_server.h"
#include "h264_encoder.h"
#include "udp_audio_server.h"
#include "audio_capture.h"
#include "audio_encoder.h"
#include "audio_protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shobjidl.h>

#include <d3d11.h>
#include <dxgi1_2.h>

#include <windows.graphics.capture.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.graphics.directx.direct3d11.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <coroutine>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;


// ============================================================
// GLOBALS
// ============================================================

HWND g_hwnd = nullptr;

com_ptr<ID3D11Device> g_d3dDevice;
com_ptr<ID3D11DeviceContext> g_d3dContext;

com_ptr<IDXGISwapChain1> g_swapChain;
com_ptr<ID3D11RenderTargetView> g_renderTargetView;

com_ptr<ID3D11Texture2D> g_lastCapturedTexture;

UdpServer g_udpServer;

H264Encoder g_h264Encoder;

UdpAudioServer g_udpAudioServer;

AudioCapture g_audioCapture;

AudioEncoder g_audioEncoder;

// ------------------------------------------------------------
// Relogio compartilhado entre video e audio.
//
// Definido UMA VEZ no inicio do programa. Ambos os pipelines
// timestampam seus frames/pacotes como "GetTickCount64() -
// g_streamClockStart", garantindo que os dois usem a MESMA
// origem de tempo - isso e o que permite o receiver alinhar
// video e audio (sincronizacao A/V).
// ------------------------------------------------------------

ULONGLONG g_streamClockStart =
    0;


// ============================================================
// GRAPHICS CAPTURE
// ============================================================

GraphicsCaptureItem g_captureItem{ nullptr };
Direct3D11CaptureFramePool g_framePool{ nullptr };
GraphicsCaptureSession g_captureSession{ nullptr };

winrt::event_token g_frameArrivedToken{};

bool g_captureRunning = false;


// ============================================================
// CAPTURE STATS
// ============================================================

std::atomic<uint64_t> g_frameCount{ 0 };

uint32_t g_captureWidth = 0;
uint32_t g_captureHeight = 0;

double g_captureFps = 0.0;

ULONGLONG g_fpsStartTime = 0;
uint64_t g_fpsFrameCounter = 0;


// ============================================================
// UDP
// ============================================================

std::atomic<bool> g_udpServerStarted{ false };
std::atomic<bool> g_udpReceiverConnected{ false };

std::thread g_udpThread;

std::atomic<bool> g_audioServerStarted{ false };
std::atomic<bool> g_audioReceiverConnected{ false };
std::atomic<bool> g_audioSendRunning{ false };

std::thread g_audioServerThread;
std::thread g_audioSendThread;


// ============================================================
// H264 ENCODER THREAD
// ============================================================

struct PendingEncodeFrame
{
    std::vector<unsigned char> pixels;

    uint32_t width = 0;
    uint32_t height = 0;

    bool valid = false;
};

PendingEncodeFrame g_pendingEncodeFrame;

std::mutex g_encodeMutex;
std::condition_variable g_encodeCv;

std::atomic<bool> g_encoderRunning{ false };

std::thread g_encoderThread;

uint32_t g_encoderWidth = 0;
uint32_t g_encoderHeight = 0;


// ============================================================
// PREVIEW DA CAPTURA
// ============================================================

std::vector<unsigned char> g_displayPixels;

std::mutex g_displayMutex;

uint32_t g_displayWidth = 0;
uint32_t g_displayHeight = 0;


// ============================================================
// DIRECT3D INTEROP
// ============================================================
//
// Interface usada para recuperar a interface DXGI
// de uma IDirect3DSurface.
//
// GUID correto:
// A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1
//
// Usamos "Custom" no nome para evitar conflito caso o
// Windows SDK já declare IDirect3DDxgiInterfaceAccess.
// ============================================================

struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
IDirect3DDxgiInterfaceAccessCustom : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetInterface(
        REFIID iid,
        void** p
    ) = 0;
};


// ============================================================
// DEBUG CONSOLE
// ============================================================

void CreateDebugConsole()
{
    if (!AllocConsole())
        return;

    FILE* fp = nullptr;

    freopen_s(
        &fp,
        "CONOUT$",
        "w",
        stdout
    );

    freopen_s(
        &fp,
        "CONOUT$",
        "w",
        stderr
    );

    freopen_s(
        &fp,
        "CONIN$",
        "r",
        stdin
    );

    SetConsoleTitleW(
        L"Stream Project - Debug"
    );

    std::cout
        << "Console de debug criada.\n\n";
}


// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void EncoderThread();

void UdpServerThread();

void UdpAudioServerThread();

void AudioSendThread();

winrt::fire_and_forget StartCaptureAsync();

bool CopyTextureToPixels(
    ID3D11Texture2D* texture,
    uint32_t contentWidth,
    uint32_t contentHeight,
    std::vector<unsigned char>& pixels,
    uint32_t& width,
    uint32_t& height
);

void SaveTextureAsBMP(
    ID3D11Texture2D* texture,
    const wchar_t* filename
);


// ============================================================
// H264 ENCODER THREAD
// ============================================================

void EncoderThread()
{
    std::cout
        << "Thread H264 encoder iniciada.\n";


    while (g_encoderRunning)
    {
        PendingEncodeFrame frame;


        // --------------------------------------------------------
        // Esperar por um frame novo
        // --------------------------------------------------------

        {
            std::unique_lock<std::mutex> lock(
                g_encodeMutex
            );


            g_encodeCv.wait(
                lock,
                []()
                {
                    return
                        !g_encoderRunning ||
                        g_pendingEncodeFrame.valid;
                }
            );


            if (!g_encoderRunning)
                break;


            // ----------------------------------------------------
            // Pegamos o frame mais recente
            // ----------------------------------------------------

            frame.pixels =
                std::move(
                    g_pendingEncodeFrame.pixels
                );


            frame.width =
                g_pendingEncodeFrame.width;


            frame.height =
                g_pendingEncodeFrame.height;


            g_pendingEncodeFrame.valid =
                false;
        }


        // --------------------------------------------------------
        // Se não existe receiver UDP, não codifica
        // --------------------------------------------------------

        if (!g_udpReceiverConnected)
        {
            if (g_h264Encoder.IsRunning())
            {
                std::cout
                    << "UDP: receiver desconectado. "
                    << "Parando encoder.\n";


                g_h264Encoder.Stop();


                g_encoderWidth = 0;
                g_encoderHeight = 0;
            }


            continue;
        }


        if (frame.pixels.empty() ||
            frame.width == 0 ||
            frame.height == 0)
        {
            continue;
        }


        // --------------------------------------------------------
        // Se resolução mudou, reiniciar encoder
        // --------------------------------------------------------

        if (!g_h264Encoder.IsRunning() ||
            g_encoderWidth != frame.width ||
            g_encoderHeight != frame.height)
        {
            if (g_h264Encoder.IsRunning())
            {
                std::cout
                    << "\nH264: resolucao mudou. "
                    << "Reiniciando encoder...\n";


                g_h264Encoder.Stop();
            }


            g_encoderWidth =
                frame.width;


            g_encoderHeight =
                frame.height;


            // ----------------------------------------------------
            // H264 NVENC
            //
            // 60 FPS
            // 8 Mbps
            // ----------------------------------------------------

            std::cout
                << "H264: iniciando encoder em "
                << frame.width
                << "x"
                << frame.height
                << "...\n";


            if (!g_h264Encoder.Start(
                    frame.width,
                    frame.height,
                    60,
                    16))
            {
                std::cout
                    << "H264: falha ao iniciar encoder.\n";


                g_encoderWidth = 0;
                g_encoderHeight = 0;


                continue;
            }


            std::cout
                << "H264: encoder iniciado em "
                << frame.width
                << "x"
                << frame.height
                << " @ 60 FPS, 8 Mbps.\n";
        }


        // --------------------------------------------------------
        // Enviar frame BGRA para FFmpeg/NVENC
        // --------------------------------------------------------

        if (!g_h264Encoder.EncodeFrame(
                frame.pixels.data(),
                static_cast<uint32_t>(
                    frame.pixels.size())))
        {
            std::cout
                << "H264: falha ao codificar frame.\n";


            g_h264Encoder.Stop();


            g_encoderWidth = 0;
            g_encoderHeight = 0;


            continue;
        }


        // --------------------------------------------------------
        // Recuperar frames H264 completos produzidos pelo FFmpeg.
        //
        // Pode haver mais de um frame pronto na fila (ex: se o
        // laço de captura demorou um pouco), então drenamos tudo.
        // --------------------------------------------------------

        EncodedFrame encodedFrame;


        while (g_h264Encoder.GetFrame(
                encodedFrame))
        {
            if (encodedFrame.data.empty())
            {
                continue;
            }


            // ------------------------------------------------
            // Enviar o frame H264 fragmentado via UDP
            // ------------------------------------------------

            uint32_t videoTimestampMs =
                static_cast<uint32_t>(
                    GetTickCount64() -
                    g_streamClockStart
                );


            if (!g_udpServer.SendVideoFrame(
                    encodedFrame.data.data(),
                    static_cast<uint32_t>(
                        encodedFrame.data.size()),
                    frame.width,
                    frame.height,
                    encodedFrame.isKeyframe,
                    videoTimestampMs))
            {
                std::cout
                    << "UDP: falha ao enviar "
                    << "video H264.\n";


                g_udpReceiverConnected =
                    false;


                g_h264Encoder.Stop();


                g_encoderWidth = 0;
                g_encoderHeight = 0;


                break;
            }
        }
    }


    // ============================================================
    // FINALIZAR ENCODER
    // ============================================================

    if (g_h264Encoder.IsRunning())
    {
        g_h264Encoder.Stop();
    }


    g_encoderWidth = 0;
    g_encoderHeight = 0;


    std::cout
        << "Thread H264 encoder encerrada.\n";
}


// ============================================================
// D3D11 DEVICE
// ============================================================

bool CreateD3DDevice()
{
    UINT creationFlags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT;


#ifdef _DEBUG

    creationFlags |=
        D3D11_CREATE_DEVICE_DEBUG;

#endif


    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };


    D3D_FEATURE_LEVEL featureLevel{};


    HRESULT hr =
        D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            creationFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            g_d3dDevice.put(),
            &featureLevel,
            g_d3dContext.put()
        );


    if (FAILED(hr))
    {
        std::cout
            << "D3D11CreateDevice falhou. HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";


        return false;
    }


    std::cout
        << "D3D11 Device criada.\n";


    std::cout
        << "Feature Level: 0x"
        << std::hex
        << featureLevel
        << std::dec
        << "\n";


    // ------------------------------------------------------------
    // Informações da GPU
    // ------------------------------------------------------------

    com_ptr<IDXGIDevice> dxgiDevice;


    hr =
        g_d3dDevice->QueryInterface(
            dxgiDevice.put()
        );


    if (SUCCEEDED(hr))
    {
        com_ptr<IDXGIAdapter> adapter;


        if (SUCCEEDED(
                dxgiDevice->GetAdapter(
                    adapter.put()
                )))
        {
            DXGI_ADAPTER_DESC desc{};


            if (SUCCEEDED(
                    adapter->GetDesc(
                        &desc
                    )))
            {
                std::wcout
                    << L"GPU: "
                    << desc.Description
                    << L"\n";
            }
        }
    }


    return true;
}


// ============================================================
// GET WINRT DIRECT3D DEVICE
// ============================================================

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
CreateWinRTDirect3DDevice()
{
    com_ptr<IDXGIDevice> dxgiDevice;


    winrt::check_hresult(
        g_d3dDevice->QueryInterface(
            dxgiDevice.put()
        )
    );


    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device{
        nullptr
    };


    HRESULT hr =
        CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.get(),
            reinterpret_cast<IInspectable**>(
                put_abi(device)
            )
        );


    winrt::check_hresult(hr);


    return device;
}


// ============================================================
// GET D3D11 TEXTURE FROM CAPTURE FRAME
// ============================================================

com_ptr<ID3D11Texture2D>
GetTextureFromSurface(
    IDirect3DSurface surface)
{
    if (!surface)
    {
        std::cout
            << "Surface invalida.\n";

        return nullptr;
    }


    // --------------------------------------------------------
    // Obter interface DXGI da Surface
    // --------------------------------------------------------

    com_ptr<IDirect3DDxgiInterfaceAccessCustom>
        dxgiAccess;


    HRESULT hr =
        reinterpret_cast<IUnknown*>(
            winrt::get_abi(surface)
        )->QueryInterface(
            __uuidof(
                IDirect3DDxgiInterfaceAccessCustom
            ),
            dxgiAccess.put_void()
        );


    if (FAILED(hr))
    {
        std::cout
            << "QueryInterface DXGI access falhou. HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";


        return nullptr;
    }


    // --------------------------------------------------------
    // Obter ID3D11Texture2D
    // --------------------------------------------------------

    com_ptr<ID3D11Texture2D>
        texture;


    hr =
        dxgiAccess->GetInterface(
            __uuidof(ID3D11Texture2D),
            texture.put_void()
        );


    if (FAILED(hr))
    {
        std::cout
            << "GetInterface ID3D11Texture2D falhou. HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";


        return nullptr;
    }


    return texture;
}


// ============================================================
// COPY GPU TEXTURE -> CPU BGRA
// ============================================================

bool CopyTextureToPixels(
    ID3D11Texture2D* texture,
    uint32_t contentWidth,
    uint32_t contentHeight,
    std::vector<unsigned char>& pixels,
    uint32_t& width,
    uint32_t& height)
{
    if (!texture)
        return false;


    D3D11_TEXTURE2D_DESC desc{};


    texture->GetDesc(
        &desc
    );


    // --------------------------------------------------------
    // Usar o tamanho de CONTEÚDO (frame.ContentSize(), vindo
    // do chamador) em vez do tamanho bruto da textura.
    //
    // O buffer interno do Windows Graphics Capture pode ser
    // maior que o conteúdo válido (ex: reaproveitado de um
    // frame anterior maior, ou com padding de alinhamento) -
    // ContentSize() é a fonte da verdade sobre quantos pixels
    // realmente importam.
    // --------------------------------------------------------

    width =
        (contentWidth > 0 &&
         contentWidth <= desc.Width)
        ? contentWidth
        : desc.Width;


    height =
        (contentHeight > 0 &&
         contentHeight <= desc.Height)
        ? contentHeight
        : desc.Height;


    if (width == 0 ||
        height == 0)
    {
        return false;
    }


    // --------------------------------------------------------
    // Criar textura staging (no tamanho cheio da textura de
    // origem - só a REGIÃO de conteúdo é copiada pra ela)
    // --------------------------------------------------------

    D3D11_TEXTURE2D_DESC stagingDesc =
        desc;


    stagingDesc.Usage =
        D3D11_USAGE_STAGING;


    stagingDesc.BindFlags =
        0;


    stagingDesc.CPUAccessFlags =
        D3D11_CPU_ACCESS_READ;


    stagingDesc.MiscFlags =
        0;


    com_ptr<ID3D11Texture2D> stagingTexture;


    HRESULT hr =
        g_d3dDevice->CreateTexture2D(
            &stagingDesc,
            nullptr,
            stagingTexture.put()
        );


    if (FAILED(hr))
    {
        std::cout
            << "CreateTexture2D staging falhou. HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";


        return false;
    }


    // --------------------------------------------------------
    // GPU -> staging (só a região de CONTEÚDO válido, não a
    // textura inteira - evita copiar padding/lixo)
    // --------------------------------------------------------

    D3D11_BOX sourceBox{};

    sourceBox.left = 0;
    sourceBox.top = 0;
    sourceBox.front = 0;
    sourceBox.right = width;
    sourceBox.bottom = height;
    sourceBox.back = 1;


    g_d3dContext->CopySubresourceRegion(
        stagingTexture.get(),
        0,
        0,
        0,
        0,
        texture,
        0,
        &sourceBox
    );


    // --------------------------------------------------------
    // Map
    // --------------------------------------------------------

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
    {
        std::cout
            << "Map staging texture falhou. HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";


        return false;
    }


    // --------------------------------------------------------
    // Copiar linhas para memória contínua
    // --------------------------------------------------------

    const size_t rowSize =
        static_cast<size_t>(width) * 4;


    const size_t totalSize =
        rowSize *
        static_cast<size_t>(height);


    pixels.resize(
        totalSize
    );


    const unsigned char* source =
        static_cast<const unsigned char*>(
            mapped.pData
        );


    unsigned char* destination =
        pixels.data();


    for (uint32_t y = 0;
         y < height;
         ++y)
    {
        memcpy(
            destination + y * rowSize,
            source + y * mapped.RowPitch,
            rowSize
        );
    }


    // --------------------------------------------------------
    // Unmap
    // --------------------------------------------------------

    g_d3dContext->Unmap(
        stagingTexture.get(),
        0
    );


    return true;
}


// ============================================================
// SAVE TEXTURE AS BMP
// ============================================================

#pragma pack(push, 1)

struct BMPFileHeader
{
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};


struct BMPInfoHeader
{
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};

#pragma pack(pop)


void SaveTextureAsBMP(
    ID3D11Texture2D* texture,
    const wchar_t* filename)
{
    if (!texture)
        return;


    std::vector<unsigned char> pixels;

    uint32_t width = 0;
    uint32_t height = 0;


    if (!CopyTextureToPixels(
            texture,
            0,
            0,
            pixels,
            width,
            height))
    {
        return;
    }


    BMPFileHeader fileHeader{};

    BMPInfoHeader infoHeader{};


    const uint32_t imageSize =
        width *
        height *
        4;


    fileHeader.bfType =
        0x4D42;


    fileHeader.bfOffBits =
        sizeof(BMPFileHeader) +
        sizeof(BMPInfoHeader);


    fileHeader.bfSize =
        fileHeader.bfOffBits +
        imageSize;


    infoHeader.biSize =
        sizeof(BMPInfoHeader);


    infoHeader.biWidth =
        static_cast<int32_t>(width);


    infoHeader.biHeight =
        -static_cast<int32_t>(height);


    infoHeader.biPlanes =
        1;


    infoHeader.biBitCount =
        32;


    infoHeader.biCompression =
        BI_RGB;


    infoHeader.biSizeImage =
        imageSize;


    std::ofstream file(
        filename,
        std::ios::binary
    );


    if (!file)
    {
        std::cout
            << "Nao foi possivel salvar BMP.\n";


        return;
    }


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
        pixels.size()
    );


    std::cout
        << "Frame salvo em BMP: "
        << width
        << "x"
        << height
        << "\n";
}


// ============================================================
// UDP SERVER THREAD
// ============================================================

void UdpServerThread()
{
    std::cout
        << "Iniciando servidor UDP...\n";


    if (!g_udpServer.Start(5001))
    {
        std::cout
            << "Falha ao iniciar servidor UDP.\n";


        g_udpServerStarted =
            false;


        return;
    }


    g_udpServerStarted =
        true;


    std::cout
        << "Servidor UDP iniciado na porta 5001.\n";


    std::cout
        << "Aguardando HELLO do receiver UDP...\n";


    while (g_udpServerStarted)
    {
        if (!g_udpReceiverConnected)
        {
            if (g_udpServer.WaitForClient())
            {
                if (!g_udpServerStarted)
                    break;


                g_udpReceiverConnected =
                    true;


                std::cout
                    << "\n========================================\n"
                    << "RECEIVER UDP CONECTADO!\n"
                    << "H264 NVENC -> MPEG-TS -> UDP\n"
                    << "========================================\n";
            }
        }
        else
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );
        }
    }


    std::cout
        << "Thread UDP encerrada.\n";
}


// ============================================================
// UDP AUDIO SERVER THREAD
// ============================================================
//
// Espelha UdpServerThread(), mas em socket/porta próprios
// (AUDIO_SERVER_UDP_PORT) - handshake HELLO independente do
// vídeo.
// ============================================================

void UdpAudioServerThread()
{
    std::cout
        << "Iniciando servidor UDP de audio...\n";


    if (!g_udpAudioServer.Start(
            AUDIO_SERVER_UDP_PORT))
    {
        std::cout
            << "Falha ao iniciar servidor UDP de audio.\n";


        g_audioServerStarted =
            false;


        return;
    }


    g_audioServerStarted =
        true;


    std::cout
        << "Aguardando HELLO do receiver (audio)...\n";


    while (g_audioServerStarted)
    {
        if (!g_audioReceiverConnected)
        {
            if (g_udpAudioServer.WaitForClient())
            {
                if (!g_audioServerStarted)
                    break;


                g_audioReceiverConnected =
                    true;


                std::cout
                    << "\n========================================\n"
                    << "RECEIVER DE AUDIO CONECTADO!\n"
                    << "Opus -> UDP\n"
                    << "========================================\n";
            }
        }
        else
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );
        }
    }


    std::cout
        << "Thread UDP de audio encerrada.\n";
}


// ============================================================
// AUDIO SEND THREAD
// ============================================================
//
// Captura (WASAPI loopback) -> encode (Opus) -> envia (UDP,
// socket próprio). Roda em thread separada da captura/envio
// de vídeo de propósito, pra não competir por CPU no mesmo
// laço e não ficar refém da cadência do vídeo.
// ============================================================

void AudioSendThread()
{
    g_audioCapture.SelectDeviceInteractively();


    if (!g_audioCapture.Start())
    {
        std::cout
            << "AudioSendThread: falha ao iniciar "
            << "captura de audio.\n";

        return;
    }


    if (!g_audioEncoder.Start())
    {
        std::cout
            << "AudioSendThread: falha ao iniciar "
            << "encoder Opus.\n";

        g_audioCapture.Stop();

        return;
    }


    while (g_audioSendRunning)
    {
        std::vector<float> pcmFrame;


        bool gotFrame =
            g_audioCapture.GetFrame(
                pcmFrame
            );


        if (!gotFrame)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(2)
            );

            continue;
        }


        if (!g_audioReceiverConnected)
        {
            // Sem receiver conectado ainda - descarta o
            // audio capturado pra não acumular atraso.
            continue;
        }


        std::vector<unsigned char> opusPacket;


        if (!g_audioEncoder.Encode(
                pcmFrame.data(),
                opusPacket))
        {
            continue;
        }


        uint32_t timestampMs =
            static_cast<uint32_t>(
                GetTickCount64() -
                g_streamClockStart
            );


        if (!g_udpAudioServer.SendAudioPacket(
                opusPacket.data(),
                static_cast<uint32_t>(
                    opusPacket.size()
                ),
                timestampMs))
        {
            g_audioReceiverConnected =
                false;
        }
    }


    g_audioEncoder.Stop();

    g_audioCapture.Stop();


    std::cout
        << "Thread de envio de audio encerrada.\n";
}


// ============================================================
// FRAME ARRIVED
// ============================================================

void OnFrameArrived(
    Direct3D11CaptureFramePool const& sender,
    winrt::Windows::Foundation::IInspectable const&)
{
    if (!g_captureRunning)
        return;


    auto frame =
        sender.TryGetNextFrame();


    if (!frame)
        return;


    auto surface =
        frame.Surface();


    if (!surface)
        return;


    // ----------------------------------------------------------
    // Surface -> ID3D11Texture2D
    // ----------------------------------------------------------

    com_ptr<ID3D11Texture2D> texture =
        GetTextureFromSurface(
            surface
        );


    if (!texture)
        return;


    g_lastCapturedTexture =
        texture;


    // ----------------------------------------------------------
    // Atualizar tamanho
    // ----------------------------------------------------------

    D3D11_TEXTURE2D_DESC desc{};


    texture->GetDesc(
        &desc
    );


    // ----------------------------------------------------------
    // ContentSize() é a fonte da verdade sobre quantos pixels
    // da textura são conteúdo válido (o buffer bruto pode ser
    // maior - ver comentário em CopyTextureToPixels).
    // ----------------------------------------------------------

    auto contentSize =
        frame.ContentSize();


    uint32_t contentWidth =
        (contentSize.Width > 0 &&
         static_cast<uint32_t>(contentSize.Width) <=
             desc.Width)
        ? static_cast<uint32_t>(contentSize.Width)
        : desc.Width;


    uint32_t contentHeight =
        (contentSize.Height > 0 &&
         static_cast<uint32_t>(contentSize.Height) <=
             desc.Height)
        ? static_cast<uint32_t>(contentSize.Height)
        : desc.Height;


    g_captureWidth =
        contentWidth;


    g_captureHeight =
        contentHeight;


    // ----------------------------------------------------------
    // Estatísticas
    // ----------------------------------------------------------

    g_frameCount++;

    g_fpsFrameCounter++;


    ULONGLONG now =
        GetTickCount64();


    if (g_fpsStartTime == 0)
    {
        g_fpsStartTime =
            now;
    }


    if (now - g_fpsStartTime >= 1000)
    {
        g_captureFps =
            static_cast<double>(
                g_fpsFrameCounter
            );


        g_fpsFrameCounter =
            0;


        g_fpsStartTime =
            now;
    }


    // ----------------------------------------------------------
    // Copiar GPU -> CPU
    // ----------------------------------------------------------

    std::vector<unsigned char> pixels;

    uint32_t width = 0;
    uint32_t height = 0;


    if (CopyTextureToPixels(
            texture.get(),
            contentWidth,
            contentHeight,
            pixels,
            width,
            height))
    {
        // ------------------------------------------------------
        // Atualizar preview
        // ------------------------------------------------------

        {
            std::lock_guard<std::mutex> lock(
                g_displayMutex
            );


            g_displayPixels =
                pixels;


            g_displayWidth =
                width;


            g_displayHeight =
                height;
        }


        // ------------------------------------------------------
        // Solicitar repaint
        // ------------------------------------------------------

        if (g_hwnd)
        {
            InvalidateRect(
                g_hwnd,
                nullptr,
                FALSE
            );
        }


        // ------------------------------------------------------
        // Se houver receiver, enviar ao encoder
        // ------------------------------------------------------

        if (g_udpReceiverConnected)
        {
            {
                std::lock_guard<std::mutex> lock(
                    g_encodeMutex
                );


                g_pendingEncodeFrame.pixels =
                    std::move(
                        pixels
                    );


                g_pendingEncodeFrame.width =
                    width;


                g_pendingEncodeFrame.height =
                    height;


                g_pendingEncodeFrame.valid =
                    true;
            }


            g_encodeCv.notify_one();
        }
    }


    // ----------------------------------------------------------
    // Atualizar título da janela
    // ----------------------------------------------------------

    if (g_hwnd)
    {
        wchar_t title[256]{};


        swprintf_s(
            title,
            L"STREAM PROJECT - "
            L"Capturando: %s | "
            L"Resolucao: %ux%u | "
            L"Frames: %llu | "
            L"FPS: %.1f | "
            L"UDP: %s | "
            L"H264: %s",
            g_captureRunning
                ? L"SIM"
                : L"NAO",
            g_captureWidth,
            g_captureHeight,
            g_frameCount.load(),
            g_captureFps,
            g_udpReceiverConnected
                ? L"Conectado"
                : L"Aguardando",
            g_h264Encoder.IsRunning()
                ? L"ON"
                : L"OFF"
        );


        SetWindowTextW(
            g_hwnd,
            title
        );


        InvalidateRect(
            g_hwnd,
            nullptr,
            FALSE
        );
    }
}


// ============================================================
// CREATE CAPTURE - ASYNC
// ============================================================

winrt::fire_and_forget StartCaptureAsync()
{
    try
    {
        std::cout
            << "\nIniciando Graphics Capture Picker...\n";


        auto device =
            CreateWinRTDirect3DDevice();


        GraphicsCapturePicker picker;


        picker.as<IInitializeWithWindow>()->Initialize(
            g_hwnd
        );


        std::cout
            << "Aguardando selecao da janela...\n";


        // --------------------------------------------------------
        // Usar co_await para não bloquear a message loop
        // --------------------------------------------------------

        auto item =
            co_await picker.PickSingleItemAsync();


        if (!item)
        {
            std::cout
                << "Nenhuma janela selecionada.\n";


            PostMessageW(
                g_hwnd,
                WM_CLOSE,
                0,
                0
            );


            co_return;
        }


        g_captureItem =
            item;


        auto size =
            item.Size();


        g_captureWidth =
            size.Width;


        g_captureHeight =
            size.Height;


        std::cout
            << "Captura selecionada: "
            << g_captureWidth
            << "x"
            << g_captureHeight
            << "\n";


        // --------------------------------------------------------
        // Criar Frame Pool
        // --------------------------------------------------------

        std::cout
            << "Criando Frame Pool...\n";


        g_framePool =
            Direct3D11CaptureFramePool::CreateFreeThreaded(
                device,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                3,
                size
            );


        // --------------------------------------------------------
        // Criar sessão
        // --------------------------------------------------------

        g_captureSession =
            g_framePool.CreateCaptureSession(
                item
            );


        // --------------------------------------------------------
        // Registrar callback
        // --------------------------------------------------------

        g_frameArrivedToken =
            g_framePool.FrameArrived(
                &OnFrameArrived
            );


        g_captureRunning =
            true;


        g_fpsStartTime =
            GetTickCount64();


        g_fpsFrameCounter =
            0;


        // --------------------------------------------------------
        // Iniciar captura
        // --------------------------------------------------------

        g_captureSession.StartCapture();


        std::cout
            << "========================================\n"
            << "CAPTURA INICIADA!\n"
            << "========================================\n";
    }
    catch (const winrt::hresult_error& error)
    {
        std::wcerr
            << L"Erro ao iniciar captura: "
            << error.message()
            << L"\n";


        if (g_hwnd)
        {
            PostMessageW(
                g_hwnd,
                WM_CLOSE,
                0,
                0
            );
        }
    }
    catch (...)
    {
        std::cout
            << "Erro desconhecido ao iniciar captura.\n";


        if (g_hwnd)
        {
            PostMessageW(
                g_hwnd,
                WM_CLOSE,
                0,
                0
            );
        }
    }
}


// ============================================================
// WINDOW PROC
// ============================================================

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (uMsg)
    {
    // ========================================================
    // INICIAR CAPTURA
    // ========================================================

    case WM_APP + 1:
    {
        StartCaptureAsync();

        return 0;
    }


    // ========================================================
    // PAINT
    // ========================================================

    case WM_ERASEBKGND:
    {
        // Evita o "flash" de fundo antes do WM_PAINT (que já
        // cuida de tudo com double buffer).
        return 1;
    }


    case WM_PAINT:
    {
        PAINTSTRUCT ps{};


        HDC hdc =
            BeginPaint(
                hwnd,
                &ps
            );


        RECT rect{};


        GetClientRect(
            hwnd,
            &rect
        );


        int windowWidth =
            rect.right -
            rect.left;


        int windowHeight =
            rect.bottom -
            rect.top;


        if (windowWidth <= 0 ||
            windowHeight <= 0)
        {
            EndPaint(
                hwnd,
                &ps
            );

            return 0;
        }


        // ====================================================
        // DOUBLE BUFFER
        //
        // Desenha tudo num bitmap fora da tela primeiro, e só
        // no final copia o resultado pronto pra tela de uma
        // vez (BitBlt) - elimina o flicker do fundo preto
        // aparecendo antes do frame.
        // ====================================================

        static HDC memDC =
            nullptr;

        static HBITMAP memBitmap =
            nullptr;

        static HBITMAP oldBitmap =
            nullptr;

        static int memWidth =
            0;

        static int memHeight =
            0;


        if (!memDC ||
            memWidth != windowWidth ||
            memHeight != windowHeight)
        {
            if (memDC)
            {
                SelectObject(
                    memDC,
                    oldBitmap
                );

                DeleteObject(
                    memBitmap
                );

                DeleteDC(
                    memDC
                );
            }


            memDC =
                CreateCompatibleDC(
                    hdc
                );

            memBitmap =
                CreateCompatibleBitmap(
                    hdc,
                    windowWidth,
                    windowHeight
                );

            oldBitmap =
                static_cast<HBITMAP>(
                    SelectObject(
                        memDC,
                        memBitmap
                    )
                );


            memWidth =
                windowWidth;

            memHeight =
                windowHeight;
        }


        // ----------------------------------------------------
        // Copiar frame atual
        // ----------------------------------------------------

        std::vector<unsigned char> pixels;

        uint32_t width = 0;
        uint32_t height = 0;


        {
            std::lock_guard<std::mutex> lock(
                g_displayMutex
            );


            if (!g_displayPixels.empty() &&
                g_displayWidth > 0 &&
                g_displayHeight > 0)
            {
                pixels =
                    g_displayPixels;


                width =
                    g_displayWidth;


                height =
                    g_displayHeight;
            }
        }


        // ----------------------------------------------------
        // Ainda não temos frame
        // ----------------------------------------------------

        if (pixels.empty() ||
            width == 0 ||
            height == 0)
        {
            FillRect(
                memDC,
                &rect,
                static_cast<HBRUSH>(
                    GetStockObject(
                        BLACK_BRUSH
                    )
                )
            );
        }
        else
        {
            // ------------------------------------------------
            // Informações do bitmap
            // ------------------------------------------------

            BITMAPINFO bmi{};


            bmi.bmiHeader.biSize =
                sizeof(BITMAPINFOHEADER);


            bmi.bmiHeader.biWidth =
                static_cast<LONG>(width);


            // Negativo = top-down
            bmi.bmiHeader.biHeight =
                -static_cast<LONG>(height);


            bmi.bmiHeader.biPlanes =
                1;


            bmi.bmiHeader.biBitCount =
                32;


            bmi.bmiHeader.biCompression =
                BI_RGB;


            double scaleX =
                static_cast<double>(
                    windowWidth
                ) /
                static_cast<double>(
                    width
                );


            double scaleY =
                static_cast<double>(
                    windowHeight
                ) /
                static_cast<double>(
                    height
                );


            double scale =
                (scaleX < scaleY)
                    ? scaleX
                    : scaleY;


            int drawWidth =
                static_cast<int>(
                    width * scale
                );


            int drawHeight =
                static_cast<int>(
                    height * scale
                );


            int drawX =
                (windowWidth -
                 drawWidth) / 2;


            int drawY =
                (windowHeight -
                 drawHeight) / 2;


            // ------------------------------------------------
            // Fundo preto (no buffer fora da tela)
            // ------------------------------------------------

            FillRect(
                memDC,
                &rect,
                static_cast<HBRUSH>(
                    GetStockObject(
                        BLACK_BRUSH
                    )
                )
            );


            // ------------------------------------------------
            // Qualidade/velocidade da escala
            // ------------------------------------------------

            SetStretchBltMode(
                memDC,
                COLORONCOLOR
            );


            // ------------------------------------------------
            // Desenhar frame (no buffer fora da tela)
            // ------------------------------------------------

            StretchDIBits(
                memDC,
                drawX,
                drawY,
                drawWidth,
                drawHeight,
                0,
                0,
                static_cast<int>(width),
                static_cast<int>(height),
                pixels.data(),
                &bmi,
                DIB_RGB_COLORS,
                SRCCOPY
            );
        }


        // ====================================================
        // Copiar o buffer pronto pra tela - uma única operação.
        // ====================================================

        BitBlt(
            hdc,

            0,
            0,

            windowWidth,
            windowHeight,

            memDC,

            0,
            0,

            SRCCOPY
        );


        EndPaint(
            hwnd,
            &ps
        );


        return 0;
    }


    // ========================================================
    // DESTROY
    // ========================================================

    case WM_DESTROY:
    {
        std::cout
            << "\nEncerrando Stream Project...\n";


        // ----------------------------------------------------
        // Parar captura
        // ----------------------------------------------------

        g_captureRunning =
            false;


        if (g_captureSession)
        {
            g_captureSession.Close();


            g_captureSession =
                nullptr;
        }


        if (g_framePool)
        {
            if (g_frameArrivedToken.value != 0)
            {
                try
                {
                    g_framePool.FrameArrived(
                        g_frameArrivedToken
                    );
                }
                catch (...)
                {
                }
            }


            g_framePool.Close();


            g_framePool =
                nullptr;
        }


        // ----------------------------------------------------
        // Limpar preview
        // ----------------------------------------------------

        {
            std::lock_guard<std::mutex> lock(
                g_displayMutex
            );


            g_displayPixels.clear();

            g_displayWidth = 0;
            g_displayHeight = 0;
        }


        // ----------------------------------------------------
        // Parar encoder
        // ----------------------------------------------------

        g_encoderRunning =
            false;


        g_encodeCv.notify_all();


        if (g_encoderThread.joinable())
        {
            g_encoderThread.join();
        }


        g_h264Encoder.Stop();


        // ----------------------------------------------------
        // Parar UDP
        // ----------------------------------------------------

        g_udpReceiverConnected =
            false;


        g_udpServerStarted =
            false;


        g_udpServer.Stop();


        // ----------------------------------------------------
        // Esperar thread UDP
        // ----------------------------------------------------

        if (g_udpThread.joinable())
        {
            g_udpThread.join();
        }


        // ----------------------------------------------------
        // Parar audio
        // ----------------------------------------------------

        g_audioReceiverConnected =
            false;


        g_audioServerStarted =
            false;


        g_audioSendRunning =
            false;


        g_udpAudioServer.Stop();


        if (g_audioServerThread.joinable())
        {
            g_audioServerThread.join();
        }


        if (g_audioSendThread.joinable())
        {
            g_audioSendThread.join();
        }


        // ----------------------------------------------------
        // Limpar D3D
        // ----------------------------------------------------

        g_lastCapturedTexture =
            nullptr;


        g_swapChain =
            nullptr;


        g_renderTargetView =
            nullptr;


        g_d3dContext =
            nullptr;


        g_d3dDevice =
            nullptr;


        PostQuitMessage(0);


        return 0;
    }
    }


    return DefWindowProcW(
        hwnd,
        uMsg,
        wParam,
        lParam
    );
}


// ============================================================
// WINMAIN
// ============================================================

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    PWSTR,
    int nCmdShow)
{
    // =========================================================
    // CONSOLE DE DEBUG
    // =========================================================

    // =========================================================
    // DPI AWARENESS
    // =========================================================
    //
    // CRÍTICO pra captura de tela em monitores com escala
    // (125%, 150%, etc - comum em notebooks e monitores 4K).
    //
    // Sem isso, o Windows "virtualiza" o processo pra pensar
    // que está rodando a 100% de escala, fazendo APIs de
    // tamanho de tela/janela (incluindo o Graphics Capture
    // Picker) reportarem dimensões diferentes das que a GPU
    // realmente captura em pixels físicos - causando a
    // distorção/esticamento na imagem recebida.
    //
    // Precisa ser chamado ANTES de qualquer janela ser criada
    // ou qualquer API de DPI ser consultada.
    // =========================================================

    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    );


    CreateDebugConsole();


    // =========================================================
    // RELOGIO COMPARTILHADO (video + audio)
    // =========================================================

    g_streamClockStart =
        GetTickCount64();


    std::cout
        << "=============================================\n"
        << "       STREAM PROJECT - SERVER\n"
        << "=============================================\n"
        << "H264 NVENC + MPEG-TS + UDP\n"
        << "=============================================\n\n";


    // =========================================================
    // WINSOCK
    // =========================================================

    WSADATA wsaData{};


    int wsaResult =
        WSAStartup(
            MAKEWORD(2, 2),
            &wsaData
        );


    if (wsaResult != 0)
    {
        std::cout
            << "WSAStartup falhou. Erro: "
            << wsaResult
            << "\n";


        return 1;
    }


    // =========================================================
    // D3D11
    // =========================================================

    if (!CreateD3DDevice())
    {
        WSACleanup();

        return 1;
    }


    // =========================================================
    // WINDOW CLASS
    // =========================================================

    const wchar_t CLASS_NAME[] =
        L"StreamProjectWindow";


    WNDCLASSW wc{};


    wc.lpfnWndProc =
        WindowProc;


    wc.hInstance =
        hInstance;


    wc.lpszClassName =
        CLASS_NAME;


    wc.hCursor =
        LoadCursor(
            nullptr,
            IDC_ARROW
        );


    wc.hbrBackground =
        static_cast<HBRUSH>(
            GetStockObject(
                BLACK_BRUSH
            )
        );


    if (!RegisterClassW(&wc))
    {
        std::cout
            << "RegisterClassW falhou.\n";


        WSACleanup();

        return 1;
    }


    // =========================================================
    // WINDOW
    // =========================================================

    g_hwnd =
        CreateWindowExW(
            0,
            CLASS_NAME,
            L"STREAM PROJECT",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1000,
            700,
            nullptr,
            nullptr,
            hInstance,
            nullptr
        );


    if (!g_hwnd)
    {
        std::cout
            << "CreateWindowExW falhou.\n";


        WSACleanup();

        return 1;
    }


    ShowWindow(
        g_hwnd,
        nCmdShow
    );


    UpdateWindow(
        g_hwnd
    );


    // =========================================================
    // UDP SERVER THREAD
    // =========================================================

    g_udpThread =
        std::thread(
            UdpServerThread
        );


    // =========================================================
    // AUDIO SERVER + SEND THREADS
    // =========================================================

    g_audioServerThread =
        std::thread(
            UdpAudioServerThread
        );


    g_audioSendRunning =
        true;


    g_audioSendThread =
        std::thread(
            AudioSendThread
        );


    // =========================================================
    // H264 ENCODER THREAD
    // =========================================================

    g_encoderRunning =
        true;


    g_encoderThread =
        std::thread(
            EncoderThread
        );


    // =========================================================
    // INICIAR CAPTURA
    // =========================================================

    PostMessageW(
        g_hwnd,
        WM_APP + 1,
        0,
        0
    );


    // =========================================================
    // MESSAGE LOOP
    // =========================================================

    MSG msg{};


    while (
        GetMessageW(
            &msg,
            nullptr,
            0,
            0
        ) > 0)
    {
        TranslateMessage(
            &msg
        );


        DispatchMessageW(
            &msg
        );
    }


    // =========================================================
    // CLEANUP
    // =========================================================

    g_encoderRunning =
        false;


    g_encodeCv.notify_all();


    if (g_encoderThread.joinable())
    {
        g_encoderThread.join();
    }


    g_h264Encoder.Stop();


    g_udpServerStarted =
        false;


    g_udpReceiverConnected =
        false;


    g_udpServer.Stop();


    if (g_udpThread.joinable())
    {
        g_udpThread.join();
    }


    g_audioServerStarted =
        false;


    g_audioReceiverConnected =
        false;


    g_audioSendRunning =
        false;


    g_udpAudioServer.Stop();


    if (g_audioServerThread.joinable())
    {
        g_audioServerThread.join();
    }


    if (g_audioSendThread.joinable())
    {
        g_audioSendThread.join();
    }


    WSACleanup();


    std::cout
        << "Stream Project encerrado.\n";


    return 0;
}