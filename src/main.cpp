// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#include "udp_server.h"
#include "h264_encoder.h"
#include "udp_audio_server.h"
#include "audio_capture.h"
#include "audio_encoder.h"
#include "audio_protocol.h"
#include "signaling_client.h"
#include "ffmpeg_check.h"
#include "stream_crypto.h"
#include "invite_code.h"
#include "client_ui.h"
#include "crash_report.h"
#include "version.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>

#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
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
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <coroutine>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;


// ============================================================
// MODULO "TRANSMITIR"
// ============================================================
//
// Este arquivo era o programa stream-client inteiro. No
// programa unico (Colae.exe) ele virou o motor do modo
// Transmitir: captura, encoder, rede e audio - sem janela
// propria. A janela, o Direct3D da tela e a interface sao do
// programa (ver receiver.cpp); client_ui.cpp liga as duas
// partes.
//
// Tudo fica no namespace "sender" pra nao se misturar com os
// nomes do modo Assistir (receiver.cpp tambem tem g_hwnd,
// g_running...).
// ============================================================

namespace sender
{


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

SignalingHost g_signalingHost;

// Par de chaves desta execucao (criptografia do canal). Criado
// na abertura do programa; nunca vai pra disco.
CryptoKeyPair g_hostKeys{};

// true = sem sessao por codigo: a conexao e por convite manual
// (ver invite_code.h) e o console aceita a RESPOSTA colada.
std::atomic<bool> g_inviteMode{ false };

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


// ------------------------------------------------------------
// Qualidade selecionável (definida no prompt inicial)
// ------------------------------------------------------------

// Atomicos: a interface pode trocar durante a transmissao
// (g_encoderRestartRequested avisa a thread do encoder).
std::atomic<uint32_t> g_targetFps{ 60 };

// 0 = resolucao nativa, sem reescalar
std::atomic<uint32_t> g_targetOutputHeight{ 0 };

// true = qualidade/FPS mudou durante a transmissao: a thread do
// encoder reinicia o encoder com os valores novos.
std::atomic<bool> g_encoderRestartRequested{ false };

// Sinaliza que o usuário já escolheu a tela/janela no seletor -
// usado pra atrasar o prompt de dispositivo de áudio, evitando
// os dois disputarem foco de janela ao mesmo tempo.
std::atomic<bool> g_captureItemSelected{ false };

// true = a fonte do som mudou (outra janela escolhida); a thread
// de audio reinicia a captura com a fonte nova.
std::atomic<bool> g_audioRestartRequested{ false };


// ============================================================
// GRAPHICS CAPTURE
// ============================================================

GraphicsCaptureItem g_captureItem{ nullptr };
Direct3D11CaptureFramePool g_framePool{ nullptr };

// Tamanho com que g_framePool foi criado/recriado por último -
// usado pra detectar quando precisa chamar Recreate().
int32_t g_framePoolWidth = 0;
int32_t g_framePoolHeight = 0;
GraphicsCaptureSession g_captureSession{ nullptr };

winrt::event_token g_frameArrivedToken{};

bool g_captureRunning = false;

// Deteccao de "janela preta" (ver CaptureLooksBlack): quando a
// captura atual comecou, se e de janela e se ja chegou alguma
// imagem com conteudo.
std::atomic<ULONGLONG> g_captureStartMs{ 0 };
std::atomic<HWND> g_captureWindow{ nullptr };
std::atomic<bool> g_captureSawContent{ false };

bool CaptureLooksBlack()
{
    return
        g_captureRunning &&
        g_captureWindow.load() != nullptr &&
        !g_captureSawContent &&
        GetTickCount64() - g_captureStartMs.load() > 2500;
}


HWND CaptureWindowHandle()
{
    return g_captureRunning ? g_captureWindow.load() : nullptr;
}



// A ultima tentativa de captura foi recusada pelo Windows (acesso
// negado). A tela de escolha oferece reabrir como administrador.
bool g_captureAccessDenied = false;


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
// IMAGEM DE "TRANSMISSAO PAUSADA"
//
// A janela compartilhada fechou: no lugar da ultima imagem
// congelada, quem assiste (e a previa daqui, e a da sala) ve um
// quadro escuro com o aviso. Desenhado uma vez com GDI e
// reenviado de tempos em tempos (ver SubmitPausedFrame).
// ============================================================

std::vector<unsigned char> g_pausedPixels;
uint32_t g_pausedWidth = 0;
uint32_t g_pausedHeight = 0;

void BuildPausedFrame(
    uint32_t width,
    uint32_t height)
{
    BITMAPINFO info{};

    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height); // de cima pra baixo
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;

    HDC dc = CreateCompatibleDC(nullptr);

    HBITMAP bitmap =
        dc
        ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0)
        : nullptr;

    g_pausedPixels.assign(static_cast<size_t>(width) * height * 4, 0);

    if (bitmap && bits)
    {
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

        RECT all{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

        HBRUSH background = CreateSolidBrush(RGB(14, 17, 22));

        FillRect(dc, &all, background);

        DeleteObject(background);

        SetBkMode(dc, TRANSPARENT);

        int titleSize = (std::max)(16, static_cast<int>(height / 16));

        HFONT title = CreateFontW(
            -titleSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        HFONT subtitle = CreateFontW(
            -(titleSize * 11 / 20), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        LONG middle = static_cast<LONG>(height / 2);

        RECT top{ 0, middle - titleSize * 3 / 2, all.right, middle };
        RECT bottom{ 0, middle + titleSize / 4, all.right, middle + titleSize * 3 / 2 };

        HGDIOBJ oldFont = SelectObject(dc, title);

        SetTextColor(dc, RGB(236, 238, 241));

        DrawTextW(dc, L"Transmiss\u00e3o pausada", -1, &top,
                  DT_CENTER | DT_BOTTOM | DT_SINGLELINE);

        SelectObject(dc, subtitle);

        SetTextColor(dc, RGB(150, 156, 166));

        DrawTextW(dc, L"A janela compartilhada foi fechada", -1, &bottom,
                  DT_CENTER | DT_TOP | DT_SINGLELINE);

        SelectObject(dc, oldFont);
        SelectObject(dc, oldBitmap);

        DeleteObject(title);
        DeleteObject(subtitle);

        GdiFlush();

        std::memcpy(g_pausedPixels.data(), bits, g_pausedPixels.size());

        // GDI deixa o alfa em 0: opaco.
        for (size_t i = 3; i < g_pausedPixels.size(); i += 4)
        {
            g_pausedPixels[i] = 255;
        }
    }

    if (bitmap)
        DeleteObject(bitmap);

    if (dc)
        DeleteDC(dc);

    g_pausedWidth = width;
    g_pausedHeight = height;
}


void SubmitPausedFrame()
{
    // Mesmo tamanho da ultima imagem (o encoder nao reinicia).
    uint32_t width = 0;
    uint32_t height = 0;

    {
        std::lock_guard<std::mutex> lock(g_displayMutex);

        width = g_displayWidth;
        height = g_displayHeight;
    }

    if (width < 64 || height < 64)
    {
        width = 1280;
        height = 720;
    }

    width &= ~1u;
    height &= ~1u;

    if (g_pausedWidth != width ||
        g_pausedHeight != height ||
        g_pausedPixels.empty())
    {
        BuildPausedFrame(width, height);
    }

    {
        std::lock_guard<std::mutex> lock(g_displayMutex);

        g_displayPixels = g_pausedPixels;
        g_displayWidth = width;
        g_displayHeight = height;
    }

    if (g_udpReceiverConnected)
    {
        {
            std::lock_guard<std::mutex> lock(g_encodeMutex);

            g_pendingEncodeFrame.pixels = g_pausedPixels;
            g_pendingEncodeFrame.width = width;
            g_pendingEncodeFrame.height = height;
            g_pendingEncodeFrame.valid = true;
        }

        g_encodeCv.notify_one();
    }
}


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
// PickBitrateMbps
// ============================================================
//
// Bitrate proporcional a qualidade escolhida. Antes era fixo
// em 16 Mbps: otimo na rede local, mas acima do upload de
// muita conexao domestica - e o que nao cabe no upload vira
// pacote perdido (tela preta/travada do outro lado).
// ============================================================

uint32_t PickBitrateMbps(
    uint32_t outputHeight,
    uint32_t fps)
{
    uint32_t mbps =
        outputHeight <= 480 ? 3 :
        outputHeight <= 720 ? 6 :
        12;

    // 30 FPS ou menos: metade dos frames, ~60% do bitrate.
    if (fps <= 30)
    {
        mbps = (mbps * 6 + 5) / 10;
    }

    return mbps < 2 ? 2 : mbps;
}


// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void EncoderThread();

void UdpServerThread();

void UdpAudioServerThread();

void AudioSendThread();

void StopCapture();

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

        bool qualityChanged =
            g_encoderRestartRequested.exchange(false);

        if (!g_h264Encoder.IsRunning() ||
            qualityChanged ||
            g_encoderWidth != frame.width ||
            g_encoderHeight != frame.height)
        {
            if (g_h264Encoder.IsRunning())
            {
                std::cout
                    << (qualityChanged
                        ? "\nH264: qualidade mudou. "
                        : "\nH264: resolucao mudou. ")
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
            // FPS e resolução de saída vêm da escolha do usuário
            // (prompt no início do programa).
            // ----------------------------------------------------

            std::cout
                << "H264: iniciando encoder em "
                << frame.width
                << "x"
                << frame.height
                << "...\n";


            // Uma leitura so: a interface pode trocar no meio.
            uint32_t targetFps =
                g_targetFps.load();

            uint32_t targetHeight =
                g_targetOutputHeight.load();

            if (!g_h264Encoder.Start(
                    frame.width,
                    frame.height,
                    targetFps,
                    PickBitrateMbps(
                        targetHeight > 0
                            ? targetHeight
                            : frame.height,
                        targetFps),
                    targetHeight))
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
                << " -> saida "
                << (targetHeight > 0
                    ? std::to_string(targetHeight) +
                      "p"
                    : "nativa")
                << " @ "
                << targetFps
                << " FPS.\n";
        }


        // --------------------------------------------------------
        // Throttle de FPS
        //
        // Descarta frames extras ANTES de gastar CPU/GPU
        // codificando eles - mais eficiente do que codificar e
        // descartar depois. g_targetFps vem da escolha do
        // usuário (prompt no início do programa).
        //
        // IMPORTANTE: usa std::chrono::steady_clock (alta
        // precisão), não GetTickCount64() - essa tem resolução
        // "grossa" (~15,6ms por padrão no Windows), bem na
        // margem do intervalo de 60fps (~16,7ms), o que causava
        // um efeito de "pula um, passa um" cortando o FPS real
        // pela metade.
        // --------------------------------------------------------

        {
            static std::chrono::steady_clock::time_point
                lastEncodeTimePoint{};


            auto now =
                std::chrono::steady_clock::now();


            double minIntervalMs =
                1000.0 /
                static_cast<double>(
                    g_targetFps.load()
                );


            double elapsedMs =
                std::chrono::duration<double, std::milli>(
                    now - lastEncodeTimePoint
                ).count();


            if (lastEncodeTimePoint.time_since_epoch().count() != 0 &&
                elapsedMs <
                minIntervalMs)
            {
                continue;
            }


            lastEncodeTimePoint =
                now;
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
                    g_h264Encoder.GetOutputWidth(),
                    g_h264Encoder.GetOutputHeight(),
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


    // --------------------------------------------------------
    // Largura e altura PARES
    //
    // O H.264 (no formato de cor que usamos) trabalha com
    // blocos de 2x2 pixels: com tamanho impar, o encoder recusa
    // o video. Janelas podem ter qualquer tamanho - inclusive
    // impar no meio da animacao de maximizar - entao a ultima
    // coluna/linha impar e simplesmente deixada de fora.
    // --------------------------------------------------------

    width &= ~1u;

    height &= ~1u;


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
        // Atende os espectadores: quem entrou, quem segue
        // assistindo e quem saiu. Espera ate ~200 ms por vez.
        if (!g_udpServer.Service())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );
        }


        // "Conectado" = pelo menos um espectador. E isso que
        // liga/desliga o encoder (sem ninguem assistindo, nao
        // gasta GPU codificando).
        g_udpReceiverConnected =
            g_udpServer.GetViewerCount() > 0;


        // Log: quantos pacotes os espectadores pediram de novo
        // (rede perdendo pacotes) - a cada 10 s, se mudou.
        static ULONGLONG lastRetransmitLog = 0;

        static uint64_t lastRetransmitted = 0;

        ULONGLONG nowMs =
            GetTickCount64();

        if (nowMs - lastRetransmitLog >= 10000)
        {
            uint64_t retransmitted =
                g_udpServer.GetRetransmittedPackets();

            if (retransmitted != lastRetransmitted)
            {
                std::cout
                    << "[rede] pacotes reenviados a pedido dos "
                    << "espectadores: "
                    << retransmitted - lastRetransmitted
                    << " nos ultimos 10 s\n";

                lastRetransmitted = retransmitted;
            }

            lastRetransmitLog = nowMs;
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
        if (!g_udpAudioServer.Service())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );
        }


        g_audioReceiverConnected =
            g_udpAudioServer.GetViewerCount() > 0;
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
    // --------------------------------------------------------
    // Espera o usuário escolher a tela/janela no seletor antes
    // de mostrar o prompt de dispositivo de áudio - evita os
    // dois disputarem foco de janela ao mesmo tempo (o que
    // fazia o seletor "piscar e fechar" sozinho).
    // --------------------------------------------------------

    while (!g_captureItemSelected &&
           g_audioSendRunning)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }


    if (!g_audioSendRunning)
    {
        return;
    }


    // O dispositivo de som ja foi escolhido na tela inicial
    // (AudioCapture::SelectDevice), antes de a captura comecar.


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
        // Outra janela escolhida: reinicia a captura de som com
        // a fonte nova (ver StartCaptureForTarget).
        if (g_audioRestartRequested.exchange(false))
        {
            g_audioCapture.Stop();

            if (!g_audioCapture.Start())
            {
                std::cout
                    << "AudioSendThread: falha ao reiniciar "
                    << "a captura de audio.\n";
            }
        }


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


    // ----------------------------------------------------------
    // Recriar o Frame Pool se o tamanho da fonte mudou
    //
    // IMPORTANTE: aqui usamos o ContentSize() BRUTO (sem
    // limitar pelo tamanho atual da textura). Quando a janela
    // CRESCE, a textura ainda está no tamanho ANTIGO (menor) -
    // se a gente limitasse o ContentSize a esse tamanho antigo,
    // nunca perceberíamos que precisa recriar. A cópia de
    // pixels (mais abaixo) continua usando a versão limitada,
    // por segurança contra ler fora dos limites da textura.
    // ----------------------------------------------------------

    if (
        contentSize.Width !=
            g_framePoolWidth ||
        contentSize.Height !=
            g_framePoolHeight)
    {
        std::cout
            << "\nTamanho da captura mudou: "
            << g_framePoolWidth
            << "x"
            << g_framePoolHeight
            << " -> "
            << contentSize.Width
            << "x"
            << contentSize.Height
            << ". Recriando Frame Pool...\n";


        sender.Recreate(
            CreateWinRTDirect3DDevice(),
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            3,
            contentSize
        );


        g_framePoolWidth =
            contentSize.Width;

        g_framePoolHeight =
            contentSize.Height;


        // O frame atual ainda é do tamanho antigo - descarta e
        // espera o próximo já vir no tamanho certo.
        return;
    }


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
        // Janela: confere (so ate a primeira imagem com
        // conteudo) se nao esta chegando toda preta. Uma grade de
        // ~2300 pontos - o cursor sozinho nao conta.
        if (!g_captureSawContent &&
            g_captureWindow.load() != nullptr &&
            width >= 64 &&
            height >= 36)
        {
            int lit = 0;

            for (uint32_t gy = 0; gy < 36 && lit < 4; ++gy)
            {
                uint32_t y = (gy * 2 + 1) * height / 72;

                for (uint32_t gx = 0; gx < 64; ++gx)
                {
                    uint32_t x = (gx * 2 + 1) * width / 128;

                    const unsigned char* p =
                        pixels.data() +
                        (static_cast<size_t>(y) * width + x) * 4;

                    if (p[0] > 12 || p[1] > 12 || p[2] > 12)
                    {
                        if (++lit >= 4)
                            break;
                    }
                }
            }

            if (lit >= 4)
            {
                g_captureSawContent = true;
            }
        }

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

        // (a janela e redesenhada pelo laco principal a cada
        // vsync - nao precisa pedir repintura aqui)


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


    // (O titulo da janela agora e do programa: SetWindowText
    // daqui, da thread de captura, faria ela esperar pela
    // thread da janela.)
}
// ============================================================
// StopCapture
// ============================================================
//
// Para a captura atual (se houver) e solta a tela/janela que
// estava sendo compartilhada. Usado ao trocar o que esta sendo
// compartilhado e ao fechar o programa.
// ============================================================

void StopCapture()
{
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

            g_frameArrivedToken =
                {};
        }

        g_framePool.Close();

        g_framePool =
            nullptr;
    }


    // Referencia a um recurso do sistema (a janela/tela
    // escolhida) - soltar explicitamente ajuda o Windows a
    // liberar o que rastreia sobre a captura mais cedo.
    g_captureItem =
        nullptr;
}


// ============================================================
// StartCaptureForTarget
// ============================================================
//
// Comeca a capturar a tela ou janela escolhida no seletor da
// interface (ver capture_targets). Chamar pela thread da
// janela.
//
// Diferente do seletor do Windows, aqui sabemos exatamente qual
// janela foi escolhida - o que permite capturar so o som dela.
// ============================================================

bool StartCaptureForTarget(
    const CaptureTarget& target,
    bool windowAudioOnly,
    std::string& outError)
{
    g_captureAccessDenied = false;

    if (!CaptureTargetStillValid(target))
    {
        outError =
            target.kind == CaptureTargetKind::Window
            ? "Essa janela foi fechada. Atualize a lista."
            : "Essa tela não está mais disponível. Atualize a "
              "lista.";

        return false;
    }

    try
    {
        // ----------------------------------------------------
        // Item de captura a partir da janela/monitor
        // ----------------------------------------------------

        auto interop =
            winrt::get_activation_factory<
                GraphicsCaptureItem,
                IGraphicsCaptureItemInterop>();

        GraphicsCaptureItem item{ nullptr };

        if (target.kind == CaptureTargetKind::Window)
        {
            winrt::check_hresult(
                interop->CreateForWindow(
                    target.window,
                    winrt::guid_of<
                        ABI::Windows::Graphics::Capture::
                            IGraphicsCaptureItem>(),
                    winrt::put_abi(item)
                )
            );
        }
        else
        {
            winrt::check_hresult(
                interop->CreateForMonitor(
                    target.monitor,
                    winrt::guid_of<
                        ABI::Windows::Graphics::Capture::
                            IGraphicsCaptureItem>(),
                    winrt::put_abi(item)
                )
            );
        }


        // Trocando de tela/janela: para a captura anterior so
        // depois que a nova foi criada com sucesso (se falhar,
        // a transmissao atual continua).
        StopCapture();


        auto device =
            CreateWinRTDirect3DDevice();

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
            << target.title
            << " ("
            << g_captureWidth
            << "x"
            << g_captureHeight
            << ")\n";


        g_framePool =
            Direct3D11CaptureFramePool::CreateFreeThreaded(
                device,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                3,
                size
            );

        g_framePoolWidth =
            size.Width;

        g_framePoolHeight =
            size.Height;

        g_captureSession =
            g_framePool.CreateCaptureSession(
                item
            );

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

        g_captureWindow =
            target.kind == CaptureTargetKind::Window
            ? target.window
            : nullptr;

        g_captureSawContent = false;

        g_captureStartMs = GetTickCount64();

        g_captureSession.StartCapture();


        // ----------------------------------------------------
        // Fonte do som
        //
        // Janela + opcao marcada: so o som do programa dela.
        // Senao, o som do dispositivo escolhido na tela inicial.
        // ----------------------------------------------------

        DWORD audioProcess =
            (target.kind == CaptureTargetKind::Window &&
             windowAudioOnly)
            ? target.processId
            : 0;

        g_audioCapture.SelectProcess(
            audioProcess
        );

        // Ja havia captura de audio rodando: reinicia com a
        // fonte nova. Na primeira vez, a thread de audio ainda
        // esta esperando e comeca sozinha.
        if (g_captureItemSelected)
        {
            g_audioRestartRequested =
                true;
        }

        g_captureItemSelected =
            true;

        std::cout
            << "CAPTURA INICIADA ("
            << (audioProcess != 0
                ? "som so da janela"
                : "som do computador")
            << ").\n";

        return true;
    }
    catch (const winrt::hresult_error& error)
    {
        HRESULT code =
            error.code();

        std::cout
            << "Falha ao capturar \""
            << target.title
            << "\" (" << target.detail << "): HRESULT 0x"
            << std::hex
            << static_cast<uint32_t>(code)
            << std::dec
            << " - "
            << winrt::to_string(error.message())
            << "\n";

        // Acesso negado. Duas causas conhecidas, e as duas somem
        // rodando como administrador:
        //  - o programa da janela foi aberto como administrador;
        //  - a opcao de privacidade do Windows 11 que bloqueia
        //    captura de tela por programas da area de trabalho.
        if (code == E_ACCESSDENIED)
        {
            g_captureAccessDenied =
                !ClientIsElevated();

            if (target.needsAdmin)
            {
                outError =
                    "O Windows não deixa capturar essa janela: o "
                    "programa dela foi aberto como administrador. "
                    "Compartilhe a tela inteira ou abra o Stream "
                    "Client como administrador.";
            }
            else if (g_captureAccessDenied)
            {
                outError =
                    "O Windows bloqueou a captura de tela neste "
                    "computador. Clique em \"Abrir como "
                    "administrador\" abaixo, ou permita em "
                    "Configurações > Privacidade e segurança > "
                    "Capturas de tela e gravação de tela e abra o "
                    "Stream Client de novo.";
            }
            else
            {
                outError =
                    "O Windows não deixa capturar isso (é uma "
                    "janela protegida do sistema). Escolha outra "
                    "janela ou a tela inteira.";
            }
        }
        else
        {
            outError =
                "Não foi possível capturar: " +
                winrt::to_string(error.message());
        }
    }
    catch (...)
    {
        outError =
            "Não foi possível capturar (erro desconhecido).";

        std::cout
            << outError
            << "\n";
    }

    return false;
}




// ============================================================
// EngineStart / EngineStop
// ============================================================
//
// O motor do modo Transmitir. Liga na primeira vez que a
// pessoa entra em "Transmitir" (abre as portas UDP 5001/5003,
// consulta o STUN, sobe as threads) e desliga quando o
// programa fecha. Winsock, apartment WinRT e DPI ja foram
// preparados pelo programa.
// ============================================================

bool EngineStart()
{
    // Relogio compartilhado (video + audio): timestamps
    // relativos a este instante.
    g_streamClockStart =
        GetTickCount64();

    std::cout
        << "\n=============================================\n"
        << "  MODO TRANSMITIR\n"
        << "  H264 (NVENC/AMF/QSV/x264) + UDP\n"
        << "=============================================\n\n";


    // Direct3D da captura (dispositivo proprio, separado do
    // da tela: a captura e o encoder nao disputam a fila de
    // desenho da interface).
    if (!CreateD3DDevice())
    {
        return false;
    }


    // Par de chaves desta execucao (criptografia do canal).
    if (!CryptoGenerateKeyPair(g_hostKeys))
    {
        std::cout
            << "Falha ao gerar as chaves de criptografia.\n";

        return false;
    }

    g_udpServer.SetHostKeys(&g_hostKeys);

    g_udpAudioServer.SetHostKeys(&g_hostKeys);


    g_udpThread =
        std::thread(
            UdpServerThread
        );

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

    g_encoderRunning =
        true;

    g_encoderThread =
        std::thread(
            EncoderThread
        );

    // (a qualidade, o FPS, o dispositivo de som e o que vai
    // ser compartilhado sao escolhidos na interface - ver
    // client_ui.cpp.)

    return true;
}


void EngineStop()
{
    std::cout
        << "\nEncerrando o modo Transmitir...\n";


    // Captura
    StopCapture();

    {
        std::lock_guard<std::mutex> lock(
            g_displayMutex
        );

        g_displayPixels.clear();

        g_displayWidth = 0;
        g_displayHeight = 0;
    }


    // Encoder
    g_encoderRunning =
        false;

    g_encodeCv.notify_all();

    if (g_encoderThread.joinable())
    {
        g_encoderThread.join();
    }

    g_h264Encoder.Stop();


    // Rede (video)
    g_udpReceiverConnected =
        false;

    g_udpServerStarted =
        false;

    g_signalingHost.Stop();

    g_udpServer.Stop();

    if (g_udpThread.joinable())
    {
        g_udpThread.join();
    }


    // Audio
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


    // Direct3D da captura
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

    std::cout
        << "Modo Transmitir encerrado.\n";
}

} // namespace sender
