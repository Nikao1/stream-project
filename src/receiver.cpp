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
#include <cstring>

#include "h264_decoder.h"

#pragma comment(lib, "ws2_32.lib")


// ============================================================
// Configuração
// ============================================================

constexpr uint16_t UDP_PORT =
    5002;

constexpr uint16_t SERVER_UDP_PORT =
    5001;


// ============================================================
// Protocolo UDP
//
// Header:
//
// magic       4 bytes
// sequence    4 bytes
// payloadSize 4 bytes
// width       4 bytes
// height      4 bytes
// flags       4 bytes
//
// Total = 24 bytes
// ============================================================

#pragma pack(push, 1)

struct UdpVideoPacketHeader
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t payloadSize;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
};

#pragma pack(pop)


constexpr uint32_t UDP_MAGIC =
    0x5354524D;


constexpr uint32_t UDP_HEADER_SIZE =
    sizeof(UdpVideoPacketHeader);


constexpr uint32_t UDP_PACKET_SIZE =
    1400;


constexpr uint32_t UDP_PAYLOAD_SIZE =
    UDP_PACKET_SIZE -
    UDP_HEADER_SIZE;


// ============================================================
// Buffer UDP
// ============================================================

constexpr int UDP_RECEIVE_BUFFER_SIZE =
    64 * 1024 * 1024;


// ============================================================
// Variáveis globais
// ============================================================

HWND g_hwnd =
    nullptr;


SOCKET g_udpSocket =
    INVALID_SOCKET;


// ============================================================
// Decoder
// ============================================================

H264Decoder g_decoder;


// ============================================================
// Frame decodificado
// ============================================================

std::vector<unsigned char>
g_framePixels;


uint32_t g_frameWidth =
    0;


uint32_t g_frameHeight =
    0;


uint64_t g_frameNumber =
    0;


// ============================================================
// Mutex do frame
// ============================================================

std::mutex g_frameMutex;


// ============================================================
// Controle
// ============================================================

std::atomic<bool>
g_running{ true };


// ============================================================
// FPS
// ============================================================

std::atomic<int>
g_fps{ 0 };


int g_fpsFrameCount =
    0;


ULONGLONG g_fpsStartTime =
    GetTickCount64();


// ============================================================
// Estatísticas UDP
// ============================================================

uint32_t g_lastSequence =
    0;


bool g_haveSequence =
    false;


uint64_t g_lostPackets =
    0;


// ============================================================
// Processar frame decodificado
// ============================================================

void ProcessDecodedFrame()
{
    std::vector<unsigned char>
        decodedFrame;


    // ========================================================
    // Pode haver mais de um frame esperando no decoder.
    // Pegamos todos, ficando no final com o mais recente.
    // ========================================================

    while (
        g_decoder.GetFrame(
            decodedFrame
        ))
    {
        if (decodedFrame.empty())
        {
            return;
        }


        uint32_t width =
            g_frameWidth;


        uint32_t height =
            g_frameHeight;


        // ====================================================
        // Atualizar frame exibido
        // ====================================================

        {
            std::lock_guard<std::mutex> lock(
                g_frameMutex
            );


            g_framePixels =
                std::move(
                    decodedFrame
                );


            g_frameNumber++;
        }


        // ====================================================
        // FPS
        // ====================================================

        g_fpsFrameCount++;


        ULONGLONG now =
            GetTickCount64();


        if (
            now -
            g_fpsStartTime >=
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
        // Atualizar título
        // ====================================================

        if (g_hwnd)
        {
            wchar_t title[256]{};


            swprintf_s(
                title,
                L"Stream Receiver UDP - H264 - FPS: %d - Frame: %llu - %ux%u - Lost: %llu",
                g_fps.load(),
                static_cast<unsigned long long>(
                    g_frameNumber
                ),
                width,
                height,
                static_cast<unsigned long long>(
                    g_lostPackets
                )
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


        decodedFrame.clear();
    }
}


// ============================================================
// Processar pacote UDP
// ============================================================

void ProcessUdpPacket(
    const char* buffer,
    int packetSize)
{
    // ========================================================
    // Tamanho mínimo
    // ========================================================

    if (
        packetSize <
        static_cast<int>(
            UDP_HEADER_SIZE
        ))
    {
        return;
    }


    // ========================================================
    // Ler header
    // ========================================================

    UdpVideoPacketHeader header{};


    std::memcpy(
        &header,
        buffer,
        sizeof(header)
    );


    // ========================================================
    // Converter ordem de bytes
    // ========================================================

    uint32_t magic =
        ntohl(header.magic);


    uint32_t sequence =
        ntohl(header.sequence);


    uint32_t payloadSize =
        ntohl(header.payloadSize);


    uint32_t width =
        ntohl(header.width);


    uint32_t height =
        ntohl(header.height);


    uint32_t flags =
        ntohl(header.flags);


    (void)flags;


    // ========================================================
    // Magic
    // ========================================================

    if (
        magic !=
        UDP_MAGIC)
    {
        return;
    }


    // ========================================================
    // Payload
    // ========================================================

    if (
        payloadSize == 0 ||
        payloadSize >
            UDP_PAYLOAD_SIZE)
    {
        return;
    }


    // ========================================================
    // Resolução
    // ========================================================

    if (
        width == 0 ||
        height == 0)
    {
        return;
    }


    // ========================================================
    // Evitar resoluções absurdas
    // ========================================================

    constexpr uint32_t MAX_WIDTH =
        7680;

    constexpr uint32_t MAX_HEIGHT =
        4320;


    if (
        width > MAX_WIDTH ||
        height > MAX_HEIGHT)
    {
        return;
    }


    // ========================================================
    // Tamanho real do pacote
    // ========================================================

    const int expectedSize =
        static_cast<int>(
            UDP_HEADER_SIZE +
            payloadSize
        );


    if (
        packetSize !=
        expectedSize)
    {
        return;
    }


    // ========================================================
    // Detectar mudança de resolução
    // ========================================================

    if (
        width != g_frameWidth ||
        height != g_frameHeight)
    {
        std::cout
            << "\n========================================\n"
            << "Nova resolucao recebida: "
            << width
            << "x"
            << height
            << "\n"
            << "Reiniciando decoder H264...\n"
            << "========================================\n";


        // ====================================================
        // Parar decoder anterior
        // ====================================================

        g_decoder.Stop();


        // ====================================================
        // Iniciar decoder
        //
        // IMPORTANTE:
        //
        // H264Decoder::Start() recebe somente:
        //
        // width
        // height
        //
        // O FPS nao e necessario aqui.
        // ====================================================

        if (!g_decoder.Start(
                width,
                height))
        {
            std::cout
                << "Falha ao iniciar decoder H264.\n";


            g_frameWidth =
                0;


            g_frameHeight =
                0;


            return;
        }


        // ====================================================
        // Atualizar resolução
        // ====================================================

        g_frameWidth =
            width;


        g_frameHeight =
            height;


        // ====================================================
        // Resetar sequência
        // ====================================================

        g_haveSequence =
            false;


        std::cout
            << "Decoder H264 iniciado com sucesso.\n";
    }


    // ========================================================
    // Detectar perda/desordem de pacote
    // ========================================================

    if (g_haveSequence)
    {
        uint32_t expected =
            g_lastSequence + 1;


        if (sequence != expected)
        {
            uint32_t lost =
                0;


            if (sequence > expected)
            {
                lost =
                    sequence -
                    expected;
            }


            g_lostPackets +=
                lost;


            std::cout
                << "\nUDP: perda/desordem detectada."
                << "\nEsperado: "
                << expected
                << "\nRecebido: "
                << sequence
                << "\nPacotes perdidos estimados: "
                << lost
                << "\nTotal perdido: "
                << g_lostPackets
                << "\n";
        }
    }


    g_lastSequence =
        sequence;


    g_haveSequence =
        true;


    // ========================================================
    // Payload MPEG-TS
    // ========================================================

    const char* payload =
        buffer +
        UDP_HEADER_SIZE;


    // ========================================================
    // Enviar MPEG-TS diretamente para FFmpeg
    //
    // IMPORTANTE:
    //
    // Não reconstruímos frame H264 aqui.
    //
    // O MPEG-TS continua sendo um stream.
    // ========================================================

    if (!g_decoder.WriteData(
            payload,
            payloadSize))
    {
        std::cout
            << "\nDecoder: falha ao escrever dados.\n";


        return;
    }


    // ========================================================
    // Verificar se o decoder já produziu frame
    // ========================================================

    ProcessDecodedFrame();
}


// ============================================================
// Thread UDP
// ============================================================

void ReceiverThread()
{
    std::cout
        << "Aguardando stream H264 UDP...\n";


    std::cout
        << "Porta local: "
        << UDP_PORT
        << "\n";


    std::cout
        << "========================================\n\n";


    std::vector<char> buffer(
        UDP_PACKET_SIZE
    );


    while (g_running)
    {
        sockaddr_in senderAddress{};


        int senderAddressSize =
            sizeof(senderAddress);


        int received =
            recvfrom(
                g_udpSocket,
                buffer.data(),
                static_cast<int>(
                    buffer.size()
                ),
                0,
                reinterpret_cast<
                    sockaddr*
                >(
                    &senderAddress
                ),
                &senderAddressSize
            );


        if (
            received ==
            SOCKET_ERROR)
        {
            if (!g_running)
                break;


            int error =
                WSAGetLastError();


            std::cout
                << "\nrecvfrom() erro: "
                << error
                << "\n";


            break;
        }


        if (received <= 0)
            continue;


        ProcessUdpPacket(
            buffer.data(),
            received
        );
    }


    std::cout
        << "\nThread UDP encerrada.\n";
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
        PAINTSTRUCT ps{};


        HDC hdc =
            BeginPaint(
                hwnd,
                &ps
            );


        // ====================================================
        // Bloquear frame durante renderização
        // ====================================================

        std::lock_guard<std::mutex> lock(
            g_frameMutex
        );


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
        // Fundo preto
        // ====================================================

        FillRect(
            hdc,
            &clientRect,
            static_cast<HBRUSH>(
                GetStockObject(
                    BLACK_BRUSH
                )
            )
        );


        uint32_t width =
            g_frameWidth;


        uint32_t height =
            g_frameHeight;


        // ====================================================
        // Frame válido
        // ====================================================

        if (
            !g_framePixels.empty() &&
            width > 0 &&
            height > 0 &&
            windowWidth > 0 &&
            windowHeight > 0)
        {
            BITMAPINFO bitmapInfo{};


            bitmapInfo.bmiHeader.biSize =
                sizeof(BITMAPINFOHEADER);


            bitmapInfo.bmiHeader.biWidth =
                static_cast<LONG>(
                    width
                );


            bitmapInfo.bmiHeader.biHeight =
                -static_cast<LONG>(
                    height
                );


            bitmapInfo.bmiHeader.biPlanes =
                1;


            bitmapInfo.bmiHeader.biBitCount =
                32;


            bitmapInfo.bmiHeader.biCompression =
                BI_RGB;


            // =================================================
            // Aspect ratio
            // =================================================

            double imageAspect =
                static_cast<double>(
                    width
                ) /
                static_cast<double>(
                    height
                );


            double windowAspect =
                static_cast<double>(
                    windowWidth
                ) /
                static_cast<double>(
                    windowHeight
                );


            int drawWidth =
                windowWidth;


            int drawHeight =
                windowHeight;


            int drawX =
                0;


            int drawY =
                0;


            // =================================================
            // Letterbox
            // =================================================

            if (
                imageAspect >
                windowAspect)
            {
                drawWidth =
                    windowWidth;


                drawHeight =
                    static_cast<int>(
                        windowWidth /
                        imageAspect
                    );


                drawY =
                    (
                        windowHeight -
                        drawHeight
                    ) / 2;
            }
            else
            {
                drawHeight =
                    windowHeight;


                drawWidth =
                    static_cast<int>(
                        windowHeight *
                        imageAspect
                    );


                drawX =
                    (
                        windowWidth -
                        drawWidth
                    ) / 2;
            }


            // =================================================
            // Qualidade da escala
            // =================================================

            SetStretchBltMode(
                hdc,
                HALFTONE
            );


            // =================================================
            // Renderizar BGRA
            // =================================================

            StretchDIBits(
                hdc,

                drawX,
                drawY,

                drawWidth,
                drawHeight,

                0,
                0,

                static_cast<int>(
                    width
                ),

                static_cast<int>(
                    height
                ),

                g_framePixels.data(),

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


    case WM_DESTROY:
    {
        // ====================================================
        // Parar aplicação
        // ====================================================

        g_running =
            false;


        // ====================================================
        // Desbloquear recvfrom()
        // ====================================================

        if (
            g_udpSocket !=
            INVALID_SOCKET)
        {
            shutdown(
                g_udpSocket,
                SD_BOTH
            );
        }


        // ====================================================
        // O decoder NÃO é parado aqui.
        //
        // O ReceiverThread pode ainda estar usando-o.
        // ====================================================

        PostQuitMessage(
            0
        );


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
// MAIN
// ============================================================

int main()
{
    std::cout
        << "========== RECEIVER H264 UDP ==========\n";


    std::cout
        << "Stream Project - H264/NVENC\n\n";


    // ========================================================
    // Winsock
    // ========================================================

    WSADATA wsaData{};


    if (
        WSAStartup(
            MAKEWORD(2, 2),
            &wsaData
        ) != 0)
    {
        std::cout
            << "Falha ao iniciar Winsock.\n";


        return 1;
    }


    // ========================================================
    // Socket UDP
    // ========================================================

    g_udpSocket =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );


    if (
        g_udpSocket ==
        INVALID_SOCKET)
    {
        std::cout
            << "Falha ao criar socket UDP.\n";


        WSACleanup();


        return 1;
    }


    // ========================================================
    // Buffer UDP
    // ========================================================

    int receiveBufferSize =
        UDP_RECEIVE_BUFFER_SIZE;


    if (
        setsockopt(
            g_udpSocket,
            SOL_SOCKET,
            SO_RCVBUF,
            reinterpret_cast<
                const char*
            >(
                &receiveBufferSize
            ),
            sizeof(receiveBufferSize)
        ) == SOCKET_ERROR)
    {
        std::cout
            << "Aviso: SO_RCVBUF falhou. Erro: "
            << WSAGetLastError()
            << "\n";
    }
    else
    {
        std::cout
            << "Buffer UDP solicitado: "
            << UDP_RECEIVE_BUFFER_SIZE /
                (1024 * 1024)
            << " MB\n";
    }


    // ========================================================
    // Bind
    // ========================================================

    sockaddr_in localAddress{};


    localAddress.sin_family =
        AF_INET;


    localAddress.sin_addr.s_addr =
        htonl(
            INADDR_ANY
        );


    localAddress.sin_port =
        htons(
            UDP_PORT
        );


    if (
        bind(
            g_udpSocket,
            reinterpret_cast<
                sockaddr*
            >(
                &localAddress
            ),
            sizeof(localAddress)
        ) == SOCKET_ERROR)
    {
        std::cout
            << "Falha no bind UDP.\n"
            << "Erro: "
            << WSAGetLastError()
            << "\n";


        closesocket(
            g_udpSocket
        );


        WSACleanup();


        return 1;
    }


    std::cout
        << "UDP receiver na porta "
        << UDP_PORT
        << "\n";


    // ========================================================
    // Endereço do servidor
    // ========================================================

    sockaddr_in serverAddress{};


    serverAddress.sin_family =
        AF_INET;


    serverAddress.sin_port =
        htons(
            SERVER_UDP_PORT
        );


    if (
        inet_pton(
            AF_INET,
            "127.0.0.1",
            &serverAddress.sin_addr
        ) != 1)
    {
        std::cout
            << "Falha ao configurar endereco do servidor.\n";


        closesocket(
            g_udpSocket
        );


        WSACleanup();


        return 1;
    }


    // ========================================================
    // HELLO
    // ========================================================

    const char hello[] =
        "HELLO";


    int sent =
        sendto(
            g_udpSocket,
            hello,
            static_cast<int>(
                std::strlen(hello)
            ),
            0,
            reinterpret_cast<
                sockaddr*
            >(
                &serverAddress
            ),
            sizeof(serverAddress)
        );


    if (
        sent ==
        SOCKET_ERROR)
    {
        std::cout
            << "Falha ao enviar HELLO.\n"
            << "Erro: "
            << WSAGetLastError()
            << "\n";


        closesocket(
            g_udpSocket
        );


        WSACleanup();


        return 1;
    }


    std::cout
        << "HELLO UDP enviado!\n";


    std::cout
        << "Aguardando servidor...\n\n";


    // ========================================================
    // Janela
    // ========================================================

    const wchar_t CLASS_NAME[] =
        L"StreamReceiverWindow";


    HINSTANCE hInstance =
        GetModuleHandleW(
            nullptr
        );


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


    if (
        !RegisterClassW(
            &wc
        ))
    {
        std::cout
            << "Falha ao registrar janela.\n";


        closesocket(
            g_udpSocket
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

            L"Stream Receiver H264",

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
            g_udpSocket
        );


        WSACleanup();


        return 1;
    }


    ShowWindow(
        g_hwnd,
        SW_SHOW
    );


    UpdateWindow(
        g_hwnd
    );


    // ========================================================
    // Thread UDP
    // ========================================================

    std::thread receiverThread(
        ReceiverThread
    );


    // ========================================================
    // Message loop
    // ========================================================

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


    // ========================================================
    // Encerrar
    // ========================================================

    g_running =
        false;


    // ========================================================
    // Desbloquear recvfrom()
    // ========================================================

    if (
        g_udpSocket !=
        INVALID_SOCKET)
    {
        shutdown(
            g_udpSocket,
            SD_BOTH
        );
    }


    // ========================================================
    // Esperar thread UDP
    // ========================================================

    if (
        receiverThread.joinable())
    {
        receiverThread.join();
    }


    // ========================================================
    // Agora o decoder pode ser encerrado com segurança
    // ========================================================

    g_decoder.Stop();


    // ========================================================
    // Fechar socket
    // ========================================================

    if (
        g_udpSocket !=
        INVALID_SOCKET)
    {
        closesocket(
            g_udpSocket
        );


        g_udpSocket =
            INVALID_SOCKET;
    }


    // ========================================================
    // Winsock
    // ========================================================

    WSACleanup();


    std::cout
        << "\nReceiver encerrado.\n";


    return 0;
}