#include "client_view.h"

#include "ui_widgets.h"

#include <wrl/client.h>

#include <cstring>
#include <vector>

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

std::vector<ComPtr<ID3D11ShaderResourceView>> g_thumbnails;

// Miniaturas trocadas neste frame: o desenho deste frame ainda
// usa as antigas, entao so sao soltas no comeco do proximo.
std::vector<ComPtr<ID3D11ShaderResourceView>> g_retiredThumbnails;


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
    g_thumbnails.clear();
    g_retiredThumbnails.clear();

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


// Largura a direita ocupada pelo painel da sala (a previa fica
// no resto).
float g_previewRightInset = 0.0f;

void ClientViewSetRightInset(
    float inset)
{
    g_previewRightInset = inset;
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
            0.45f,
            g_previewRightInset
        );
    }
}

void* ClientViewAddThumbnail(
    const uint8_t* bgraPixels,
    int width,
    int height)
{
    if (!g_device ||
        !bgraPixels ||
        width <= 0 ||
        height <= 0)
    {
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc{};

    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};

    data.pSysMem = bgraPixels;
    data.SysMemPitch = static_cast<UINT>(width) * 4;

    ComPtr<ID3D11Texture2D> texture;

    ComPtr<ID3D11ShaderResourceView> view;

    if (FAILED(g_device->CreateTexture2D(
            &desc, &data, &texture)) ||
        FAILED(g_device->CreateShaderResourceView(
            texture.Get(), nullptr, &view)))
    {
        return nullptr;
    }

    g_thumbnails.push_back(view);

    return view.Get();
}


void ClientViewClearThumbnails()
{
    for (auto& view : g_thumbnails)
    {
        g_retiredThumbnails.push_back(view);
    }

    g_thumbnails.clear();
}


void ClientViewNewFrame()
{
    g_retiredThumbnails.clear();
}

} // namespace sender
