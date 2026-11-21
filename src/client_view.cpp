#include "client_view.h"

#include "ui_widgets.h"

#include <wrl/client.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

namespace sender
{

namespace
{

ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;

ComPtr<ID3D11Texture2D> g_previewTexture;
ComPtr<ID3D11ShaderResourceView> g_previewView;

uint32_t g_previewWidth = 0;
uint32_t g_previewHeight = 0;

bool g_previewValid = false;


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


void ClientViewInit(
    ID3D11Device* device,
    ID3D11DeviceContext* context)
{
    g_device = device;
    g_context = context;
}


void ClientViewShutdown()
{
    g_previewView.Reset();
    g_previewTexture.Reset();
    g_context.Reset();
    g_device.Reset();

    g_previewValid = false;
}


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


void ClientViewDrawBackground()
{
    // Escurecida pra interface por cima continuar legivel - e
    // pra deixar claro que aquilo e so um espelho do que esta
    // sendo enviado.
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
}

} // namespace sender
