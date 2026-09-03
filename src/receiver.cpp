#define UNICODE
#define _UNICODE

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>
#include <cstdint>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")


// ============================================================
// Variáveis globais
// ============================================================

HWND g_hwnd = nullptr;

SOCKET g_socketClient = INVALID_SOCKET;

std::vector<unsigned char> g_framePixels;

uint32_t g_frameWidth = 0;
uint32_t g_frameHeight = 0;

uint64_t g_frameNumber = 0;

std::mutex g_frameMutex;

std::atomic<bool> g_running{ true };

std::atomic<int> g_fps{ 0 };

int g_fpsFrameCount = 0;
ULONGLONG g_fpsStartTime = GetTickCount64();


// ============================================================
// Receber exatamente a quantidade de bytes solicitada
// ============================================================

bool ReceiveExact(
    SOCKET socket,
    void* buffer,
    int size)
{
    char* destination =
        static_cast<char*>(buffer);

    int totalReceived = 0;

    while (totalReceived < size)
    {
        int received =
            recv(
                socket,
                destination + totalReceived,
                size - totalReceived,
                0
            );

        if (received == 0)
            return false;

        if (received == SOCKET_ERROR)
            return false;

        totalReceived += received;
    }

    return true;
}


// ============================================================
// Thread responsável por receber os frames
// ============================================================

void ReceiverThread()
{
    std::cout
        << "Aguardando frames...\n\n";


    while (g_running)
    {
        // ====================================================
        // CABEÇALHO
        // ====================================================

        uint32_t networkWidth = 0;
        uint32_t networkHeight = 0;
        uint32_t networkDataSize = 0;


        // ====================================================
        // Receber largura
        // ====================================================

        if (!ReceiveExact(
            g_socketClient,
            &networkWidth,
            sizeof(networkWidth)))
        {
            break;
        }


        // ====================================================
        // Receber altura
        // ====================================================

        if (!ReceiveExact(
            g_socketClient,
            &networkHeight,
            sizeof(networkHeight)))
        {
            break;
        }


        // ====================================================
        // Receber tamanho dos pixels
        // ====================================================

        if (!ReceiveExact(
            g_socketClient,
            &networkDataSize,
            sizeof(networkDataSize)))
        {
            break;
        }


        // ====================================================
        // Converter Network Byte Order
        // ====================================================

        uint32_t width =
            ntohl(networkWidth);

        uint32_t height =
            ntohl(networkHeight);

        uint32_t dataSize =
            ntohl(networkDataSize);


        // ====================================================
        // Validar dimensões
        // ====================================================

        uint64_t expectedSize =
            static_cast<uint64_t>(width) *
            static_cast<uint64_t>(height) *
            4ULL;


        if (
            width == 0 ||
            height == 0 ||
            dataSize == 0)
        {
            std::cout
                << "Frame invalido recebido.\n";

            break;
        }


        if (
            expectedSize !=
            static_cast<uint64_t>(dataSize))
        {
            std::cout
                << "Tamanho de frame invalido.\n";

            std::cout
                << "Esperado: "
                << expectedSize
                << " bytes\n";

            std::cout
                << "Recebido: "
                << dataSize
                << " bytes\n";

            break;
        }


        // ====================================================
        // Receber pixels
        // ====================================================

        std::vector<unsigned char> pixels(
            dataSize
        );


        if (!ReceiveExact(
            g_socketClient,
            pixels.data(),
            static_cast<int>(dataSize)))
        {
            break;
        }


        // ====================================================
        // Atualizar frame global
        // ====================================================

        {
            std::lock_guard<std::mutex> lock(
                g_frameMutex
            );

            g_framePixels =
                std::move(pixels);

            g_frameWidth =
                width;

            g_frameHeight =
                height;

            g_frameNumber++;
        }


        // ====================================================
        // Calcular FPS
        // ====================================================

        g_fpsFrameCount++;

        ULONGLONG now =
            GetTickCount64();

        if (
            now - g_fpsStartTime >=
            1000)
        {
            g_fps =
                g_fpsFrameCount;

            g_fpsFrameCount =
                0;

            g_fpsStartTime =
                now;
        }


        // ====================================================
        // Mostrar informações no console
        // ====================================================

        std::cout
            << "\rFrame "
            << g_frameNumber
            << " recebido - "
            << width
            << "x"
            << height
            << " - "
            << dataSize
            << " bytes"
            << " - FPS: "
            << g_fps.load()
            << "   "
            << std::flush;


        // ====================================================
        // Avisar a janela que chegou um novo frame
        // ====================================================

        if (g_hwnd)
        {
            InvalidateRect(
                g_hwnd,
                nullptr,
                FALSE
            );

            // =================================================
            // Atualizar título da janela
            // =================================================

            wchar_t title[256]{};

            swprintf_s(
                title,
                L"Stream Receiver - FPS: %d - Frame: %llu - %ux%u",
                g_fps.load(),
                g_frameNumber,
                width,
                height
            );

            SetWindowTextW(
            g_hwnd,
            title
            );
                }
            }


    // ========================================================
    // Conexão encerrada
    // ========================================================

    if (g_running)
    {
        std::cout
            << "\n\nConexao encerrada pelo servidor.\n";
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
    // ========================================================
    // PAINT
    // ========================================================

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};

        HDC hdc =
            BeginPaint(
                hwnd,
                &ps
            );


        // ====================================================
        // Copiar dados do frame
        // ====================================================

        std::vector<unsigned char> pixels;

        uint32_t width = 0;
        uint32_t height = 0;

        uint64_t frameNumber = 0;


        {
            std::lock_guard<std::mutex> lock(
                g_frameMutex
            );

            if (!g_framePixels.empty())
            {
                pixels =
                    g_framePixels;

                width =
                    g_frameWidth;

                height =
                    g_frameHeight;

                frameNumber =
                    g_frameNumber;
            }
        }


        // ====================================================
        // Área da janela
        // ====================================================

        RECT clientRect{};

        GetClientRect(
            hwnd,
            &clientRect
        );


        int windowWidth =
            clientRect.right -
            clientRect.left;

        int windowHeight =
            clientRect.bottom -
            clientRect.top;


        // ====================================================
        // Fundo
        // ====================================================

        FillRect(
            hdc,
            &clientRect,
            static_cast<HBRUSH>(
                GetStockObject(BLACK_BRUSH)
            )
        );


        // ====================================================
        // Se já temos um frame
        // ====================================================

        if (
            !pixels.empty() &&
            width > 0 &&
            height > 0 &&
            windowWidth > 0 &&
            windowHeight > 0)
        {
            BITMAPINFO bitmapInfo{};

            bitmapInfo.bmiHeader.biSize =
                sizeof(BITMAPINFOHEADER);

            bitmapInfo.bmiHeader.biWidth =
                static_cast<LONG>(width);

            // Negativo = imagem top-down
            bitmapInfo.bmiHeader.biHeight =
                -static_cast<LONG>(height);

            bitmapInfo.bmiHeader.biPlanes =
                1;

            bitmapInfo.bmiHeader.biBitCount =
                32;

            bitmapInfo.bmiHeader.biCompression =
                BI_RGB;


            // =================================================
            // Calcular proporção para manter aspect ratio
            // =================================================

            double imageAspect =
                static_cast<double>(width) /
                static_cast<double>(height);

            double windowAspect =
                static_cast<double>(windowWidth) /
                static_cast<double>(windowHeight);


            int drawWidth =
                windowWidth;

            int drawHeight =
                windowHeight;

            int drawX = 0;
            int drawY = 0;


            if (imageAspect > windowAspect)
            {
                // Imagem mais larga

                drawWidth =
                    windowWidth;

                drawHeight =
                    static_cast<int>(
                        windowWidth /
                        imageAspect
                    );

                drawY =
                    (windowHeight -
                     drawHeight) / 2;
            }
            else
            {
                // Imagem mais alta

                drawHeight =
                    windowHeight;

                drawWidth =
                    static_cast<int>(
                        windowHeight *
                        imageAspect
                    );

                drawX =
                    (windowWidth -
                     drawWidth) / 2;
            }


            // =================================================
            // Desenhar frame
            // =================================================

            SetStretchBltMode(
                hdc,
                HALFTONE
            );


            StretchDIBits(
                hdc,

                drawX,
                drawY,

                drawWidth,
                drawHeight,

                0,
                0,

                static_cast<int>(width),
                static_cast<int>(height),

                pixels.data(),

                &bitmapInfo,

                DIB_RGB_COLORS,

                SRCCOPY
            );
        }


        EndPaint(
            hwnd,
            &ps
        );


        return 0;
    }


    // ========================================================
    // FECHAR JANELA
    // ========================================================

    case WM_DESTROY:
    {
        g_running = false;


        // ====================================================
        // Desbloquear recv()
        // ====================================================

        if (g_socketClient != INVALID_SOCKET)
        {
            shutdown(
                g_socketClient,
                SD_BOTH
            );
        }


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
// MAIN
// ============================================================

int main()
{
    std::cout << "========== RECEIVER NOVO ==========\n";
    std::cout << "ESTE EH O EXECUTAVEL QUE FOI COMPILADO AGORA!\n\n";

    // ========================================================
    // Inicializar Winsock
    // ========================================================

    WSADATA wsaData{};

    if (WSAStartup(
        MAKEWORD(2, 2),
        &wsaData) != 0)
    {
        std::cout
            << "Falha ao iniciar Winsock.\n";

        return 1;
    }


    // ========================================================
    // Criar socket
    // ========================================================

    g_socketClient =
        socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );

    if (g_socketClient == INVALID_SOCKET)
    {
        std::cout
            << "Falha ao criar socket.\n";

        WSACleanup();

        return 1;
    }


    // ========================================================
    // Endereço do servidor
    // ========================================================

    sockaddr_in serverAddress{};

    serverAddress.sin_family =
        AF_INET;

    serverAddress.sin_port =
        htons(5000);


    inet_pton(
        AF_INET,
        "127.0.0.1",
        &serverAddress.sin_addr
    );


    // ========================================================
    // Conectar
    // ========================================================

    std::cout
        << "Conectando ao servidor...\n";


    if (connect(
        g_socketClient,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress)) == SOCKET_ERROR)
    {
        std::cout
            << "Falha ao conectar ao servidor.\n";

        closesocket(
            g_socketClient
        );

        WSACleanup();

        return 1;
    }


    std::cout
        << "Conectado!\n";


    // ========================================================
    // Criar classe da janela
    // ========================================================

    const wchar_t CLASS_NAME[] =
        L"StreamReceiverWindow";


    HINSTANCE hInstance =
        GetModuleHandle(nullptr);


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
            GetStockObject(BLACK_BRUSH)
        );


    if (!RegisterClassW(&wc))
    {
        std::cout
            << "Falha ao registrar janela.\n";

        closesocket(
            g_socketClient
        );

        WSACleanup();

        return 1;
    }


    // ========================================================
    // Criar janela
    // ========================================================

    g_hwnd =
        CreateWindowExW(
            0,

            CLASS_NAME,

            L"Stream Receiver",

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
            << "Falha ao criar janela.\n";

        closesocket(
            g_socketClient
        );

        WSACleanup();

        return 1;
    }


    // ========================================================
    // Mostrar janela
    // ========================================================

    ShowWindow(
        g_hwnd,
        SW_SHOW
    );


    UpdateWindow(
        g_hwnd
    );


    // ========================================================
    // Iniciar thread de recepção
    // ========================================================

    std::thread receiverThread(
        ReceiverThread
    );


    // ========================================================
    // Message Loop
    // ========================================================

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


    // ========================================================
    // Encerrar
    // ========================================================

    g_running = false;


    if (g_socketClient != INVALID_SOCKET)
    {
        shutdown(
            g_socketClient,
            SD_BOTH
        );
    }


    if (receiverThread.joinable())
    {
        receiverThread.join();
    }


    if (g_socketClient != INVALID_SOCKET)
    {
        closesocket(
            g_socketClient
        );

        g_socketClient =
            INVALID_SOCKET;
    }


    WSACleanup();


    return 0;
}
