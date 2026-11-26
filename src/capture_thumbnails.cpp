// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#include "capture_thumbnails.h"

#include <dwmapi.h>

#include <algorithm>
#include <thread>

namespace
{

// Tamanho maximo da miniatura (mantendo a proporcao).
constexpr int THUMB_MAX_WIDTH = 320;
constexpr int THUMB_MAX_HEIGHT = 180;

// PW_RENDERFULLCONTENT: pega tambem o que e desenhado por GPU
// (navegadores, jogos em janela). Sem ele, muitas janelas saem
// pretas.
constexpr UINT PRINT_FULL_CONTENT = 0x00000002;


void FitInside(
    int width,
    int height,
    int& outWidth,
    int& outHeight)
{
    double scale =
        (std::min)(
            static_cast<double>(THUMB_MAX_WIDTH) / width,
            static_cast<double>(THUMB_MAX_HEIGHT) / height);

    if (scale > 1.0)
        scale = 1.0;

    outWidth = (std::max)(1, static_cast<int>(width * scale));
    outHeight = (std::max)(1, static_cast<int>(height * scale));
}


// Reduz a regiao (x, y, w, h) de "source" pra miniatura.
Thumbnail Shrink(
    HDC source,
    int x,
    int y,
    int width,
    int height)
{
    Thumbnail thumb;

    if (width <= 0 || height <= 0)
        return thumb;

    int thumbWidth = 0;
    int thumbHeight = 0;

    FitInside(width, height, thumbWidth, thumbHeight);

    BITMAPINFO info{};

    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = thumbWidth;
    info.bmiHeader.biHeight = -thumbHeight; // de cima pra baixo
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;

    HDC target = CreateCompatibleDC(source);

    HBITMAP bitmap =
        CreateDIBSection(
            target, &info, DIB_RGB_COLORS, &bits, nullptr, 0);

    if (!target || !bitmap || !bits)
    {
        if (bitmap) DeleteObject(bitmap);
        if (target) DeleteDC(target);

        return thumb;
    }

    HGDIOBJ old = SelectObject(target, bitmap);

    // HALFTONE: reducao com media dos pixels (sem serrilhado).
    SetStretchBltMode(target, HALFTONE);
    SetBrushOrgEx(target, 0, 0, nullptr);

    if (StretchBlt(
            target, 0, 0, thumbWidth, thumbHeight,
            source, x, y, width, height,
            SRCCOPY))
    {
        GdiFlush();

        size_t size =
            static_cast<size_t>(thumbWidth) * thumbHeight * 4;

        thumb.pixels.assign(
            static_cast<uint8_t*>(bits),
            static_cast<uint8_t*>(bits) + size);

        // O GDI deixa o alfa em 0: imagem opaca.
        for (size_t i = 3; i < size; i += 4)
        {
            thumb.pixels[i] = 255;
        }

        thumb.width = thumbWidth;
        thumb.height = thumbHeight;
    }

    SelectObject(target, old);
    DeleteObject(bitmap);
    DeleteDC(target);

    return thumb;
}


Thumbnail CaptureMonitor(
    HMONITOR monitor)
{
    MONITORINFO info{};

    info.cbSize = sizeof(info);

    if (!GetMonitorInfoW(monitor, &info))
        return Thumbnail{};

    const RECT& r = info.rcMonitor;

    HDC screen = GetDC(nullptr);

    Thumbnail thumb =
        Shrink(
            screen,
            r.left,
            r.top,
            r.right - r.left,
            r.bottom - r.top);

    ReleaseDC(nullptr, screen);

    return thumb;
}


Thumbnail CaptureWindow(
    HWND window)
{
    // Minimizada nao tem imagem; travada poderia prender a
    // thread esperando a janela se desenhar.
    if (!IsWindow(window) ||
        IsIconic(window) ||
        IsHungAppWindow(window))
    {
        return Thumbnail{};
    }

    RECT windowRect{};

    if (!GetWindowRect(window, &windowRect))
        return Thumbnail{};

    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;

    if (width <= 0 || height <= 0 ||
        width > 16384 || height > 16384)
    {
        return Thumbnail{};
    }

    // Parte visivel (sem a borda invisivel/sombra que o
    // GetWindowRect inclui no Windows 10/11).
    RECT visible = windowRect;

    DwmGetWindowAttribute(
        window,
        DWMWA_EXTENDED_FRAME_BOUNDS,
        &visible,
        sizeof(visible));

    HDC screen = GetDC(nullptr);

    HDC full = CreateCompatibleDC(screen);

    HBITMAP bitmap =
        CreateCompatibleBitmap(screen, width, height);

    Thumbnail thumb;

    if (full && bitmap)
    {
        HGDIOBJ old = SelectObject(full, bitmap);

        if (PrintWindow(window, full, PRINT_FULL_CONTENT))
        {
            thumb =
                Shrink(
                    full,
                    visible.left - windowRect.left,
                    visible.top - windowRect.top,
                    visible.right - visible.left,
                    visible.bottom - visible.top);
        }

        SelectObject(full, old);
    }

    if (bitmap) DeleteObject(bitmap);
    if (full) DeleteDC(full);

    ReleaseDC(nullptr, screen);

    return thumb;
}

} // namespace


std::shared_ptr<ThumbnailBatch> ThumbnailBatch::Start(
    const std::vector<CaptureTarget>& monitors,
    const std::vector<CaptureTarget>& windows)
{
    std::shared_ptr<ThumbnailBatch> batch =
        std::make_shared<ThumbnailBatch>();

    // A thread segura o lote vivo ate terminar, mesmo que o
    // seletor ja tenha pedido outro (ai este e so ignorado).
    std::thread(
        [batch, monitors, windows]()
        {
            batch->Run(monitors, windows);
        }).detach();

    return batch;
}


void ThumbnailBatch::Run(
    std::vector<CaptureTarget> monitors,
    std::vector<CaptureTarget> windows)
{
    // Telas primeiro (rapidas), depois as janelas.
    m_monitorThumbs.resize(monitors.size());
    m_windowThumbs.resize(windows.size());

    for (size_t i = 0; i < monitors.size(); ++i)
    {
        m_monitorThumbs[i] =
            CaptureMonitor(monitors[i].monitor);
    }

    for (size_t i = 0; i < windows.size(); ++i)
    {
        m_windowThumbs[i] =
            CaptureWindow(windows[i].window);
    }

    m_done = true;
}
