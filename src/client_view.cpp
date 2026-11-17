#include "client_view.h"

#include "ui_common.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstring>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;


namespace
{

HWND g_viewHwnd = nullptr;

ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain1> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_renderTarget;

// Previa da tela compartilhada.
ComPtr<ID3D11Texture2D> g_previewTexture;
ComPtr<ID3D11ShaderResourceView> g_previewView;

uint32_t g_previewWidth = 0;
uint32_t g_previewHeight = 0;

bool g_previewValid = false;


bool CreateRenderTarget()
{
    ComPtr<ID3D11Texture2D> backBuffer;

    if (FAILED(g_swapChain->GetBuffer(
            0,
            IID_PPV_ARGS(&backBuffer))))
    {
        return false;
    }

    return SUCCEEDED(
        g_device->CreateRenderTargetView(
            backBuffer.Get(),
            nullptr,
            &g_renderTarget));
}


bool EnsurePreviewTexture(
    uint32_t width,
    uint32_t height)
{
    if (g_previewTexture &&
        g_previewWidth == width &&
        g_previewHeight == height)
    {
        return true;
    }

    g_previewView.Reset();
    g_previewTexture.Reset();

    g_previewValid = false;

    D3D11_TEXTURE2D_DESC desc{};

    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;

    // BGRX: ignora o canal alfa dos pixels capturados (ele nem
    // sempre vem preenchido).
    desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;

    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(g_device->CreateTexture2D(
            &desc,
            nullptr,
            &g_previewTexture)))
    {
        return false;
    }

    if (FAILED(g_device->CreateShaderResourceView(
            g_previewTexture.Get(),
            nullptr,
            &g_previewView)))
    {
        g_previewTexture.Reset();

        return false;
    }

    g_previewWidth = width;
    g_previewHeight = height;

    return true;
}

} // namespace


// ============================================================
// Init / Shutdown
// ============================================================

bool ClientViewInit(
    HWND hwnd)
{
    g_viewHwnd = hwnd;

    HRESULT hr =
        D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &g_device,
            nullptr,
            &g_context
        );

    if (FAILED(hr))
    {
        // Sem placa/driver utilizavel: renderizador por
        // software do Windows. Lento, mas a interface abre.
        hr =
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &g_device,
                nullptr,
                &g_context
            );
    }

    if (FAILED(hr))
    {
        std::cout
            << "ClientView: D3D11CreateDevice falhou.\n";

        return false;
    }

    // A fabrica DXGI que criou o dispositivo e quem cria o
    // swap chain pra ele.
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;

    if (FAILED(g_device.As(&dxgiDevice)) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
    {
        std::cout
            << "ClientView: fabrica DXGI indisponivel.\n";

        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};

    // Largura/altura 0 = tamanho atual da janela.
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    if (FAILED(factory->CreateSwapChainForHwnd(
            g_device.Get(),
            hwnd,
            &desc,
            nullptr,
            nullptr,
            &g_swapChain)))
    {
        std::cout
            << "ClientView: CreateSwapChainForHwnd falhou.\n";

        return false;
    }

    // Alt+Enter nao alterna tela cheia por conta propria.
    factory->MakeWindowAssociation(
        hwnd,
        DXGI_MWA_NO_ALT_ENTER
    );

    if (!CreateRenderTarget())
    {
        std::cout
            << "ClientView: render target falhou.\n";

        return false;
    }

    return
        UiInit(
            hwnd,
            g_device.Get(),
            g_context.Get()
        );
}


void ClientViewShutdown()
{
    UiShutdown();

    g_previewView.Reset();
    g_previewTexture.Reset();
    g_renderTarget.Reset();
    g_swapChain.Reset();
    g_context.Reset();
    g_device.Reset();

    g_previewValid = false;
}


// ============================================================
// Resize
// ============================================================

void ClientViewResize(
    uint32_t width,
    uint32_t height)
{
    if (!g_swapChain ||
        width == 0 ||
        height == 0)
    {
        return;
    }

    // O swap chain so redimensiona sem nenhuma referencia aos
    // buffers antigos.
    g_context->OMSetRenderTargets(
        0,
        nullptr,
        nullptr
    );

    g_renderTarget.Reset();

    if (FAILED(g_swapChain->ResizeBuffers(
            0,
            width,
            height,
            DXGI_FORMAT_UNKNOWN,
            0)))
    {
        return;
    }

    CreateRenderTarget();
}


// ============================================================
// Previa
// ============================================================

void ClientViewSetPreview(
    const unsigned char* bgraPixels,
    uint32_t width,
    uint32_t height)
{
    if (!g_device ||
        !bgraPixels ||
        width == 0 ||
        height == 0)
    {
        return;
    }

    if (!EnsurePreviewTexture(width, height))
        return;

    D3D11_MAPPED_SUBRESOURCE mapped{};

    if (FAILED(g_context->Map(
            g_previewTexture.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped)))
    {
        return;
    }

    const size_t rowBytes =
        static_cast<size_t>(width) * 4;

    for (uint32_t y = 0; y < height; ++y)
    {
        std::memcpy(
            static_cast<unsigned char*>(mapped.pData) +
                static_cast<size_t>(y) * mapped.RowPitch,
            bgraPixels +
                static_cast<size_t>(y) * rowBytes,
            rowBytes
        );
    }

    g_context->Unmap(
        g_previewTexture.Get(),
        0
    );

    g_previewValid = true;
}


void ClientViewClearPreview()
{
    g_previewValid = false;
}


// ============================================================
// Frame
// ============================================================

bool ClientViewBeginFrame()
{
    if (!g_renderTarget)
        return false;

    ID3D11RenderTargetView* target =
        g_renderTarget.Get();

    g_context->OMSetRenderTargets(
        1,
        &target,
        nullptr
    );

    const float background[4] =
        { 0.035f, 0.04f, 0.05f, 1.0f };

    g_context->ClearRenderTargetView(
        target,
        background
    );

    UiBeginFrame();

    // A previa vai escurecida pra interface por cima continuar
    // legivel - e pra deixar claro que aquilo e so um espelho
    // do que esta sendo enviado.
    if (g_previewValid && g_previewView)
    {
        UiDrawBackgroundImage(
            reinterpret_cast<ImTextureID>(
                g_previewView.Get()),
            static_cast<float>(g_previewWidth),
            static_cast<float>(g_previewHeight),
            0.45f
        );
    }

    return true;
}


void ClientViewEndFrame()
{
    UiEndFrame();

    g_swapChain->Present(
        1,
        0
    );
}
