// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#include "image_codec.h"

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>

// Identificadores do WIC escritos aqui (sem depender da
// windowscodecs.lib / versao do SDK).
namespace
{

const CLSID CLSID_WicFactory =
    { 0xcacaf262, 0x9370, 0x4615,
      { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };

const GUID GUID_ContainerJpeg =
    { 0x19e4a5aa, 0x5662, 0x4fc5,
      { 0xa0, 0xc0, 0x17, 0x58, 0x02, 0x8e, 0x10, 0x57 } };

const GUID GUID_Pixel24bppBGR =
    { 0x6fddc324, 0x4e03, 0x4bfe,
      { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c } };

const GUID GUID_Pixel32bppBGRA =
    { 0x6fddc324, 0x4e03, 0x4bfe,
      { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f } };

// Maior previa aceita ao decodificar (protege contra imagem
// absurda vinda da rede).
constexpr int MAX_DECODE_SIDE = 1024;


template <typename T>
void SafeRelease(
    T*& object)
{
    if (object)
    {
        object->Release();
        object = nullptr;
    }
}


// COM nesta thread enquanto o objeto existir.
struct ComScope
{
    HRESULT hr;

    ComScope()
        : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
    {
    }

    ~ComScope()
    {
        if (SUCCEEDED(hr))
        {
            CoUninitialize();
        }
    }
};


IWICImagingFactory* CreateFactory()
{
    IWICImagingFactory* factory = nullptr;

    if (FAILED(CoCreateInstance(
            CLSID_WicFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(IWICImagingFactory),
            reinterpret_cast<void**>(&factory))))
    {
        return nullptr;
    }

    return factory;
}

} // namespace


void ImageDownscaleBgra(
    const uint8_t* pixels,
    int width,
    int height,
    int strideBytes,
    int maxWidth,
    int maxHeight,
    std::vector<uint8_t>& outPixels,
    int& outWidth,
    int& outHeight)
{
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;

    if (!pixels || width <= 0 || height <= 0)
        return;

    double scale =
        (std::min)(
            1.0,
            (std::min)(
                static_cast<double>(maxWidth) / width,
                static_cast<double>(maxHeight) / height));

    outWidth = (std::max)(1, static_cast<int>(width * scale + 0.5));
    outHeight = (std::max)(1, static_cast<int>(height * scale + 0.5));

    outPixels.resize(static_cast<size_t>(outWidth) * outHeight * 4);

    // Cada pixel de saida = media de um bloco da original (com
    // passo pra nao ler a tela inteira: ate 4x4 amostras).
    for (int y = 0; y < outHeight; ++y)
    {
        int y0 = static_cast<int>(static_cast<long long>(y) * height / outHeight);
        int y1 = static_cast<int>(static_cast<long long>(y + 1) * height / outHeight);

        y1 = (std::max)(y1, y0 + 1);

        int stepY = (std::max)(1, (y1 - y0) / 4);

        for (int x = 0; x < outWidth; ++x)
        {
            int x0 = static_cast<int>(static_cast<long long>(x) * width / outWidth);
            int x1 = static_cast<int>(static_cast<long long>(x + 1) * width / outWidth);

            x1 = (std::max)(x1, x0 + 1);

            int stepX = (std::max)(1, (x1 - x0) / 4);

            unsigned sum[3] = { 0, 0, 0 };

            unsigned count = 0;

            for (int sy = y0; sy < y1; sy += stepY)
            {
                const uint8_t* row =
                    pixels + static_cast<size_t>(sy) * strideBytes;

                for (int sx = x0; sx < x1; sx += stepX)
                {
                    const uint8_t* p = row + static_cast<size_t>(sx) * 4;

                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];

                    count++;
                }
            }

            uint8_t* out =
                &outPixels[(static_cast<size_t>(y) * outWidth + x) * 4];

            out[0] = static_cast<uint8_t>(sum[0] / count);
            out[1] = static_cast<uint8_t>(sum[1] / count);
            out[2] = static_cast<uint8_t>(sum[2] / count);
            out[3] = 255;
        }
    }
}


bool ImageEncodeJpeg(
    const uint8_t* bgraPixels,
    int width,
    int height,
    float quality,
    std::vector<uint8_t>& outJpeg)
{
    outJpeg.clear();

    if (!bgraPixels || width <= 0 || height <= 0)
        return false;

    ComScope com;

    // O JPEG nao tem alfa: BGRA -> BGR.
    std::vector<uint8_t> bgr(static_cast<size_t>(width) * height * 3);

    for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i)
    {
        bgr[i * 3 + 0] = bgraPixels[i * 4 + 0];
        bgr[i * 3 + 1] = bgraPixels[i * 4 + 1];
        bgr[i * 3 + 2] = bgraPixels[i * 4 + 2];
    }

    IWICImagingFactory* factory = CreateFactory();
    IStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* options = nullptr;

    bool ok = false;

    HRESULT hr = factory ? S_OK : E_FAIL;

    if (SUCCEEDED(hr))
        hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);

    if (SUCCEEDED(hr))
        hr = factory->CreateEncoder(GUID_ContainerJpeg, nullptr, &encoder);

    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);

    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&frame, &options);

    if (SUCCEEDED(hr) && options)
    {
        PROPBAG2 option{};

        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");

        // (zerado = VariantInit, sem puxar a oleaut32)
        VARIANT value{};

        value.vt = VT_R4;
        value.fltVal = (std::max)(0.0f, (std::min)(1.0f, quality));

        options->Write(1, &option, &value);
    }

    if (SUCCEEDED(hr))
        hr = frame->Initialize(options);

    if (SUCCEEDED(hr))
        hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));

    WICPixelFormatGUID format = GUID_Pixel24bppBGR;

    if (SUCCEEDED(hr))
        hr = frame->SetPixelFormat(&format);

    if (SUCCEEDED(hr) &&
        !IsEqualGUID(format, GUID_Pixel24bppBGR))
    {
        hr = E_FAIL;
    }

    if (SUCCEEDED(hr))
    {
        hr =
            frame->WritePixels(
                static_cast<UINT>(height),
                static_cast<UINT>(width * 3),
                static_cast<UINT>(bgr.size()),
                bgr.data());
    }

    if (SUCCEEDED(hr))
        hr = frame->Commit();

    if (SUCCEEDED(hr))
        hr = encoder->Commit();

    if (SUCCEEDED(hr))
    {
        STATSTG stat{};

        HGLOBAL memory = nullptr;

        if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) &&
            SUCCEEDED(GetHGlobalFromStream(stream, &memory)))
        {
            size_t size = static_cast<size_t>(stat.cbSize.QuadPart);

            const void* data = GlobalLock(memory);

            if (data && size > 0)
            {
                outJpeg.assign(
                    static_cast<const uint8_t*>(data),
                    static_cast<const uint8_t*>(data) + size);

                ok = true;
            }

            if (data)
            {
                GlobalUnlock(memory);
            }
        }
    }

    SafeRelease(options);
    SafeRelease(frame);
    SafeRelease(encoder);
    SafeRelease(stream);
    SafeRelease(factory);

    return ok;
}


bool ImageDecodeToBgra(
    const uint8_t* data,
    size_t size,
    std::vector<uint8_t>& outPixels,
    int& outWidth,
    int& outHeight)
{
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;

    if (!data || size == 0 || size > 1024 * 1024)
        return false;

    ComScope com;

    IWICImagingFactory* factory = CreateFactory();
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    bool ok = false;

    HRESULT hr = factory ? S_OK : E_FAIL;

    if (SUCCEEDED(hr))
        hr = factory->CreateStream(&stream);

    if (SUCCEEDED(hr))
    {
        hr =
            stream->InitializeFromMemory(
                const_cast<BYTE*>(data),
                static_cast<DWORD>(size));
    }

    if (SUCCEEDED(hr))
    {
        hr =
            factory->CreateDecoderFromStream(
                stream,
                nullptr,
                WICDecodeMetadataCacheOnDemand,
                &decoder);
    }

    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);

    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);

    if (SUCCEEDED(hr))
    {
        hr =
            converter->Initialize(
                frame,
                GUID_Pixel32bppBGRA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom);
    }

    UINT width = 0;
    UINT height = 0;

    if (SUCCEEDED(hr))
        hr = converter->GetSize(&width, &height);

    if (SUCCEEDED(hr) &&
        width > 0 && height > 0 &&
        width <= MAX_DECODE_SIDE && height <= MAX_DECODE_SIDE)
    {
        outPixels.resize(static_cast<size_t>(width) * height * 4);

        hr =
            converter->CopyPixels(
                nullptr,
                width * 4,
                static_cast<UINT>(outPixels.size()),
                outPixels.data());

        if (SUCCEEDED(hr))
        {
            outWidth = static_cast<int>(width);
            outHeight = static_cast<int>(height);

            ok = true;
        }
        else
        {
            outPixels.clear();
        }
    }

    SafeRelease(converter);
    SafeRelease(frame);
    SafeRelease(decoder);
    SafeRelease(stream);
    SafeRelease(factory);

    return ok;
}


bool ImageLoadSquareFile(
    const wchar_t* path,
    int side,
    std::vector<uint8_t>& outPixels)
{
    outPixels.clear();

    if (!path || side <= 0)
        return false;

    ComScope com;

    IWICImagingFactory* factory = CreateFactory();
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapClipper* clipper = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;

    bool ok = false;

    HRESULT hr = factory ? S_OK : E_FAIL;

    if (SUCCEEDED(hr))
    {
        hr =
            factory->CreateDecoderFromFilename(
                path,
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnDemand,
                &decoder);
    }

    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);

    UINT width = 0;
    UINT height = 0;

    if (SUCCEEDED(hr))
        hr = frame->GetSize(&width, &height);

    if (SUCCEEDED(hr) && (width == 0 || height == 0))
        hr = E_FAIL;

    // Quadrado do meio.
    if (SUCCEEDED(hr))
        hr = factory->CreateBitmapClipper(&clipper);

    if (SUCCEEDED(hr))
    {
        UINT square = width < height ? width : height;

        WICRect rect{};

        rect.X = static_cast<INT>((width - square) / 2);
        rect.Y = static_cast<INT>((height - square) / 2);
        rect.Width = static_cast<INT>(square);
        rect.Height = static_cast<INT>(square);

        hr = clipper->Initialize(frame, &rect);
    }

    if (SUCCEEDED(hr))
        hr = factory->CreateBitmapScaler(&scaler);

    if (SUCCEEDED(hr))
    {
        hr =
            scaler->Initialize(
                clipper,
                static_cast<UINT>(side),
                static_cast<UINT>(side),
                WICBitmapInterpolationModeFant);
    }

    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);

    if (SUCCEEDED(hr))
    {
        hr =
            converter->Initialize(
                scaler,
                GUID_Pixel32bppBGRA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom);
    }

    if (SUCCEEDED(hr))
    {
        outPixels.resize(static_cast<size_t>(side) * side * 4);

        hr =
            converter->CopyPixels(
                nullptr,
                static_cast<UINT>(side) * 4,
                static_cast<UINT>(outPixels.size()),
                outPixels.data());

        ok = SUCCEEDED(hr);

        if (!ok)
        {
            outPixels.clear();
        }
    }

    // Transparencia (PNG): sobre fundo cinza-escuro.
    if (ok)
    {
        for (size_t i = 0; i < outPixels.size(); i += 4)
        {
            unsigned alpha = outPixels[i + 3];

            for (int c = 0; c < 3; ++c)
            {
                outPixels[i + c] =
                    static_cast<uint8_t>(
                        (outPixels[i + c] * alpha + 40 * (255 - alpha)) / 255);
            }

            outPixels[i + 3] = 255;
        }
    }

    SafeRelease(converter);
    SafeRelease(scaler);
    SafeRelease(clipper);
    SafeRelease(frame);
    SafeRelease(decoder);
    SafeRelease(factory);

    return ok;
}
