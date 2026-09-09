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
#include "audio_protocol.h"
#include "audio_decoder.h"
#include "audio_playback.h"
#include "video_protocol.h"

#pragma comment(lib, "ws2_32.lib")


// ============================================================
// Configuração
// ============================================================

constexpr uint16_t UDP_PORT =
    5002;

constexpr uint16_t SERVER_UDP_PORT =
    5001;


// ============================================================
// Protocolo UDP (orientado a frame) - ver video_protocol.h
// ============================================================


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
// Endereço do servidor
//
// É preenchido no main() depois de configurar
// 127.0.0.1:5001.
// ============================================================

sockaddr_in g_serverAddress{};


// ============================================================
// Controle do stream
//
// false = ainda não recebemos vídeo válido
// true  = já recebemos pelo menos um pacote de vídeo válido
// ============================================================

std::atomic<bool>
g_streamReceived{ false };


// ============================================================
// Decoder
// ============================================================

H264Decoder g_decoder;

AudioDecoder g_audioDecoder;

AudioPlayback g_audioPlayback;


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
// Reassembly de frame
//
// Mantemos apenas o frame "atual" sendo montado. Se chegar um
// fragmento de um frameId mais novo antes do atual completar,
// o atual é descartado (favorece latência baixa: preferimos
// pular um frame a acumular atraso).
// ============================================================

struct PendingFrame
{
    uint32_t frameId = 0;
    uint32_t frameSize = 0;
    uint32_t fragCount = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool isKeyframe = false;
    bool active = false;

    std::vector<unsigned char> data;
    std::vector<bool> fragReceived;
    uint32_t fragReceivedCount = 0;
};

PendingFrame g_pendingFrame;


// ============================================================
// Estatísticas
// ============================================================

uint32_t g_lastCompletedFrameId =
    0;

bool g_haveCompletedFrame =
    false;

uint64_t g_framesCompleted =
    0;

uint64_t g_framesDropped =
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
                L"Stream Receiver UDP - H264 - FPS: %d - Frame: %llu - %ux%u - Dropped: %llu",
                g_fps.load(),
                static_cast<unsigned long long>(
                    g_frameNumber
                ),
                width,
                height,
                static_cast<unsigned long long>(
                    g_framesDropped
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
// StartNewPendingFrame
// ============================================================
//
// Descarta o que estiver em montagem (se houver) e inicia a
// montagem de um novo frame a partir do header recebido.
// ============================================================

void StartNewPendingFrame(
    const VideoFrameFragmentHeader& header)
{
    if (g_pendingFrame.active &&
        g_pendingFrame.fragReceivedCount <
            g_pendingFrame.fragCount)
    {
        // Havia um frame incompleto em montagem: foi
        // "atropelado" por um frame mais novo. Conta como
        // descartado.

        g_framesDropped++;
    }


    g_pendingFrame.frameId =
        header.frameId;

    g_pendingFrame.frameSize =
        header.frameSize;

    g_pendingFrame.fragCount =
        header.fragCount;

    g_pendingFrame.width =
        header.width;

    g_pendingFrame.height =
        header.height;

    g_pendingFrame.isKeyframe =
        (header.flags &
         VIDEO_FLAG_KEYFRAME) != 0;

    g_pendingFrame.active =
        true;

    g_pendingFrame.fragReceivedCount =
        0;


    g_pendingFrame.data.assign(
        header.frameSize,
        0
    );


    g_pendingFrame.fragReceived.assign(
        header.fragCount,
        false
    );
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
            VIDEO_HEADER_SIZE
        ))
    {
        return;
    }


    // ========================================================
    // Ler header
    // ========================================================

    VideoFrameFragmentHeader header{};


    std::memcpy(
        &header,
        buffer,
        sizeof(header)
    );


    // ========================================================
    // Converter ordem de bytes
    // ========================================================

    header.magic =
        ntohl(header.magic);

    header.frameId =
        ntohl(header.frameId);

    header.fragIndex =
        ntohl(header.fragIndex);

    header.fragCount =
        ntohl(header.fragCount);

    header.frameSize =
        ntohl(header.frameSize);

    header.fragSize =
        ntohl(header.fragSize);

    header.width =
        ntohl(header.width);

    header.height =
        ntohl(header.height);

    header.flags =
        ntohl(header.flags);


    // ========================================================
    // Validações básicas
    // ========================================================

    if (header.magic !=
        VIDEO_PROTOCOL_MAGIC)
    {
        return;
    }


    if (header.fragCount == 0 ||
        header.fragIndex >=
            header.fragCount)
    {
        return;
    }


    if (header.frameSize == 0 ||
        header.fragSize == 0 ||
        header.fragSize >
            VIDEO_UDP_PAYLOAD_SIZE)
    {
        return;
    }


    if (header.width == 0 ||
        header.height == 0)
    {
        return;
    }


    constexpr uint32_t MAX_WIDTH =
        7680;

    constexpr uint32_t MAX_HEIGHT =
        4320;


    if (header.width > MAX_WIDTH ||
        header.height > MAX_HEIGHT)
    {
        return;
    }


    const int expectedSize =
        static_cast<int>(
            VIDEO_HEADER_SIZE +
            header.fragSize
        );


    if (packetSize !=
        expectedSize)
    {
        return;
    }


    // ========================================================
    // Detectar mudança de resolução
    // ========================================================

    if (
        header.width != g_frameWidth ||
        header.height != g_frameHeight)
    {
        std::cout
            << "\n========================================\n"
            << "Nova resolucao recebida: "
            << header.width
            << "x"
            << header.height
            << "\n"
            << "Reiniciando decoder H264...\n"
            << "========================================\n";


        g_decoder.Stop();


        if (!g_decoder.Start(
                header.width,
                header.height))
        {
            std::cout
                << "Falha ao iniciar decoder H264.\n";


            g_frameWidth = 0;
            g_frameHeight = 0;

            return;
        }


        g_frameWidth =
            header.width;

        g_frameHeight =
            header.height;


        // Qualquer reassembly em andamento não serve mais.

        g_pendingFrame =
            PendingFrame{};

        g_haveCompletedFrame =
            false;


        std::cout
            << "Decoder H264 iniciado com sucesso.\n";
    }


    // ========================================================
    // Descartar fragmentos de frames antigos (já concluídos
    // ou já abandonados).
    // ========================================================

    if (g_haveCompletedFrame &&
        header.frameId <=
            g_lastCompletedFrameId)
    {
        return;
    }


    // ========================================================
    // Novo frame começando?
    // ========================================================

    if (g_pendingFrame.active &&
        header.frameId <
            g_pendingFrame.frameId)
    {
        // Fragmento atrasado de um frame mais antigo que já
        // foi substituído pela montagem atual - ignora, não
        // reinicia a montagem em andamento.

        return;
    }


    if (!g_pendingFrame.active ||
        header.frameId !=
            g_pendingFrame.frameId)
    {
        StartNewPendingFrame(
            header
        );
    }


    // ========================================================
    // Fragmento duplicado?
    // ========================================================

    if (g_pendingFrame.fragReceived[
            header.fragIndex])
    {
        return;
    }


    // ========================================================
    // Copiar o payload deste fragmento na posição certa
    // ========================================================

    const uint32_t fragOffset =
        header.fragIndex *
        VIDEO_UDP_PAYLOAD_SIZE;


    if (fragOffset +
            header.fragSize >
        g_pendingFrame.frameSize)
    {
        // Header inconsistente - ignora o pacote.
        return;
    }


    std::memcpy(
        g_pendingFrame.data.data() +
            fragOffset,

        buffer +
            VIDEO_HEADER_SIZE,

        header.fragSize
    );


    g_pendingFrame.fragReceived[
        header.fragIndex] = true;

    g_pendingFrame.fragReceivedCount++;


    // ========================================================
    // Frame ainda incompleto: aguarda mais fragmentos.
    // ========================================================

    if (g_pendingFrame.fragReceivedCount <
        g_pendingFrame.fragCount)
    {
        return;
    }


    // ========================================================
    // FRAME COMPLETO - entregar ao decoder.
    //
    // Só chegamos aqui quando 100% dos fragmentos foram
    // recebidos, então o decoder nunca vê um frame parcial
    // ou corrompido por perda de pacote.
    // ========================================================

    g_lastCompletedFrameId =
        g_pendingFrame.frameId;

    g_haveCompletedFrame =
        true;

    g_framesCompleted++;

    g_pendingFrame.active =
        false;


    if (!g_decoder.WriteData(
            reinterpret_cast<const char*>(
                g_pendingFrame.data.data()
            ),
            static_cast<uint32_t>(
                g_pendingFrame.data.size()
            )))
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
// Enviar HELLO para o servidor
// ============================================================

bool SendHello()
{
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

            reinterpret_cast<sockaddr*>(
                &g_serverAddress
            ),

            sizeof(g_serverAddress)
        );


    if (
        sent ==
        SOCKET_ERROR)
    {
        std::cout
            << "Falha ao enviar HELLO. Erro: "
            << WSAGetLastError()
            << "\n";


        return false;
    }


    return true;
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
        VIDEO_UDP_PACKET_SIZE
    );


    // ========================================================
    // Controle do intervalo entre HELLOs
    // ========================================================

    ULONGLONG lastHelloTime =
        0;


    while (g_running)
    {
        // ====================================================
        // Reenviar HELLO periodicamente
        //
        // Enquanto ainda não recebemos vídeo, enviamos
        // um HELLO a cada 1 segundo.
        // ====================================================

        ULONGLONG now =
            GetTickCount64();


        if (
            !g_streamReceived &&
            (
                lastHelloTime == 0 ||
                now - lastHelloTime >= 1000
            ))
        {
            if (SendHello())
            {
                std::cout
                    << "HELLO UDP enviado para "
                    << "127.0.0.1:"
                    << SERVER_UDP_PORT
                    << "\n";
            }


            lastHelloTime =
                now;
        }


        // ====================================================
        // Esperar dados UDP por até 200 ms
        //
        // Usamos select() para não ficar bloqueado
        // indefinidamente no recvfrom().
        // ====================================================

        fd_set readSet{};


        FD_ZERO(
            &readSet
        );


        FD_SET(
            g_udpSocket,
            &readSet
        );


        timeval timeout{};


        timeout.tv_sec =
            0;


        timeout.tv_usec =
            200000;


        int result =
            select(
                0,
                &readSet,
                nullptr,
                nullptr,
                &timeout
            );


        if (!g_running)
            break;


        // ====================================================
        // Timeout
        // ====================================================

        if (result == 0)
            continue;


        // ====================================================
        // Erro
        // ====================================================

        if (result == SOCKET_ERROR)
        {
            int error =
                WSAGetLastError();


            if (g_running)
            {
                std::cout
                    << "\nselect() erro: "
                    << error
                    << "\n";
            }


            break;
        }


        // ====================================================
        // Há dados no socket
        // ====================================================

        if (!FD_ISSET(
                g_udpSocket,
                &readSet))
        {
            continue;
        }


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

                reinterpret_cast<sockaddr*>(
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


            continue;
        }


        if (received <= 0)
            continue;


        // ====================================================
        // Recebemos alguma coisa.
        //
        // ProcessUdpPacket() verifica se é um pacote
        // de vídeo válido.
        // ====================================================

        ProcessUdpPacket(
            buffer.data(),
            received
        );


        // ====================================================
        // Se já temos resolução configurada, significa que
        // recebemos pelo menos um pacote de vídeo válido.
        // ====================================================

        if (
            g_frameWidth > 0 &&
            g_frameHeight > 0)
        {
            if (!g_streamReceived)
            {
                g_streamReceived =
                    true;


                std::cout
                    << "\n========================================\n"
                    << "STREAM UDP RECEBIDO!\n"
                    << "Resolucao: "
                    << g_frameWidth
                    << "x"
                    << g_frameHeight
                    << "\n"
                    << "HELLO nao sera mais reenviado.\n"
                    << "========================================\n\n";
            }
        }
    }


    std::cout
        << "\nThread UDP encerrada.\n";
}


// ============================================================
// AudioReceiverThread
// ============================================================
//
// Auto-contida: cria seu próprio socket UDP (porta
// AUDIO_UDP_PORT), envia HELLO pro client (AUDIO_SERVER_UDP_PORT)
// e recebe/decodifica/toca o áudio - tudo em thread e socket
// independentes do vídeo, de propósito (evita que áudio fique
// enfileirado atrás do processamento de frames de vídeo).
// ============================================================

void AudioReceiverThread()
{
    // ========================================================
    // Socket
    // ========================================================

    SOCKET audioSocket =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );


    if (audioSocket ==
        INVALID_SOCKET)
    {
        std::cout
            << "AudioReceiver: falha ao criar socket. "
            << "Erro: "
            << WSAGetLastError()
            << "\n";

        return;
    }


    sockaddr_in localAddress{};

    localAddress.sin_family =
        AF_INET;

    localAddress.sin_addr.s_addr =
        htonl(INADDR_ANY);

    localAddress.sin_port =
        htons(AUDIO_UDP_PORT);


    if (bind(
            audioSocket,
            reinterpret_cast<sockaddr*>(
                &localAddress
            ),
            sizeof(localAddress)
        ) == SOCKET_ERROR)
    {
        std::cout
            << "AudioReceiver: falha no bind. Erro: "
            << WSAGetLastError()
            << "\n";

        closesocket(audioSocket);

        return;
    }


    sockaddr_in serverAddress{};

    serverAddress.sin_family =
        AF_INET;

    serverAddress.sin_port =
        htons(AUDIO_SERVER_UDP_PORT);


    inet_pton(
        AF_INET,
        "127.0.0.1",
        &serverAddress.sin_addr
    );


    std::cout
        << "AudioReceiver: escutando na porta "
        << AUDIO_UDP_PORT
        << ", servidor em 127.0.0.1:"
        << AUDIO_SERVER_UDP_PORT
        << "\n";


    // ========================================================
    // Decoder + playback
    // ========================================================

    if (!g_audioDecoder.Start())
    {
        std::cout
            << "AudioReceiver: falha ao iniciar "
            << "decoder Opus.\n";

        closesocket(audioSocket);

        return;
    }


    if (!g_audioPlayback.Start())
    {
        std::cout
            << "AudioReceiver: falha ao iniciar "
            << "reprodução.\n";

        g_audioDecoder.Stop();

        closesocket(audioSocket);

        return;
    }


    // ========================================================
    // Estado de sequência (deteccao de perda p/ PLC)
    // ========================================================

    bool haveSequence =
        false;

    uint32_t lastSequence =
        0;

    bool streamReceived =
        false;


    std::vector<char> buffer(
        AUDIO_UDP_PACKET_SIZE
    );


    ULONGLONG lastHelloTime =
        0;


    while (g_running)
    {
        // ====================================================
        // Reenviar HELLO periodicamente ate receber audio
        // ====================================================

        ULONGLONG now =
            GetTickCount64();


        if (!streamReceived &&
            (lastHelloTime == 0 ||
             now - lastHelloTime >= 1000))
        {
            sendto(
                audioSocket,
                "HELLO",
                5,
                0,
                reinterpret_cast<sockaddr*>(
                    &serverAddress
                ),
                sizeof(serverAddress)
            );


            lastHelloTime =
                now;
        }


        // ====================================================
        // Esperar dados por ate 200ms
        // ====================================================

        fd_set readSet{};

        FD_ZERO(&readSet);

        FD_SET(
            audioSocket,
            &readSet
        );


        timeval timeout{};

        timeout.tv_sec = 0;

        timeout.tv_usec = 200000;


        int result =
            select(
                0,
                &readSet,
                nullptr,
                nullptr,
                &timeout
            );


        if (!g_running)
            break;


        if (result <= 0)
            continue;


        sockaddr_in fromAddress{};

        int fromSize =
            sizeof(fromAddress);


        int received =
            recvfrom(
                audioSocket,
                buffer.data(),
                static_cast<int>(
                    buffer.size()
                ),
                0,
                reinterpret_cast<sockaddr*>(
                    &fromAddress
                ),
                &fromSize
            );


        if (received <
            static_cast<int>(
                AUDIO_HEADER_SIZE
            ))
        {
            continue;
        }


        // ====================================================
        // Parse header
        // ====================================================

        AudioPacketHeader header{};

        std::memcpy(
            &header,
            buffer.data(),
            AUDIO_HEADER_SIZE
        );


        header.magic =
            ntohl(header.magic);

        header.sequence =
            ntohl(header.sequence);

        header.timestampMs =
            ntohl(header.timestampMs);

        header.payloadSize =
            ntohl(header.payloadSize);


        if (header.magic !=
            AUDIO_PROTOCOL_MAGIC)
        {
            continue;
        }


        if (header.payloadSize == 0 ||
            header.payloadSize >
                AUDIO_UDP_MAX_PAYLOAD)
        {
            continue;
        }


        if (received !=
            static_cast<int>(
                AUDIO_HEADER_SIZE +
                header.payloadSize))
        {
            continue;
        }


        if (!streamReceived)
        {
            streamReceived =
                true;


            std::cout
                << "\n========================================\n"
                << "STREAM DE AUDIO RECEBIDO!\n"
                << "========================================\n\n";
        }


        // ====================================================
        // Deteccao de perda -> PLC (packet loss concealment)
        // ====================================================

        if (haveSequence)
        {
            uint32_t expected =
                lastSequence + 1;


            if (header.sequence >
                expected)
            {
                uint32_t missing =
                    header.sequence -
                    expected;


                // Limite de seguranca - nao gera concealment
                // absurdo em reconexoes/saltos grandes.

                if (missing > 10)
                {
                    missing = 10;
                }


                for (uint32_t i = 0;
                     i < missing;
                     ++i)
                {
                    std::vector<float> concealed;


                    if (g_audioDecoder.DecodeLost(
                            concealed))
                    {
                        g_audioPlayback.PushSamples(
                            concealed
                        );
                    }
                }
            }
        }


        haveSequence =
            true;

        lastSequence =
            header.sequence;


        // ====================================================
        // Decode + tocar
        // ====================================================

        std::vector<float> pcm;


        if (g_audioDecoder.Decode(
                reinterpret_cast<const unsigned char*>(
                    buffer.data() +
                        AUDIO_HEADER_SIZE
                ),
                header.payloadSize,
                pcm))
        {
            g_audioPlayback.PushSamples(
                pcm
            );
        }
    }


    g_audioPlayback.Stop();

    g_audioDecoder.Stop();

    closesocket(
        audioSocket
    );


    std::cout
        << "\nThread de audio encerrada.\n";
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
    case WM_ERASEBKGND:
    {
        // ========================================================
        // Evita o "flash" de limpeza de fundo do Windows antes do
        // WM_PAINT rodar - o WM_PAINT já cuida de preencher tudo
        // (com double buffer), então esse erase default é
        // redundante e é uma das causas do flicker.
        // ========================================================

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
        // Tudo é desenhado num bitmap fora da tela primeiro.
        // Só no final copiamos o resultado pronto pra tela
        // (BitBlt), de uma vez só - sem "flash" de fundo preto
        // visível entre frames.
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


        // ====================================================
        // Fundo preto (no buffer fora da tela)
        // ====================================================

        FillRect(
            memDC,
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
            height > 0)
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
                memDC,
                COLORONCOLOR
            );


            // =================================================
            // Renderizar BGRA (no buffer fora da tela)
            // =================================================

            StretchDIBits(
                memDC,

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


        // ====================================================
        // Copiar o buffer pronto pra tela - uma única operação,
        // sem estados intermediários visíveis.
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


    case WM_DESTROY:
    {
        // ====================================================
        // Parar aplicação
        // ====================================================

        g_running =
            false;


        // ====================================================
        // Desbloquear recvfrom()/select()
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
    // Salvar endereço do servidor globalmente
    // ========================================================

    g_serverAddress =
        serverAddress;


    std::cout
        << "Servidor configurado: "
        << "127.0.0.1:"
        << SERVER_UDP_PORT
        << "\n";


    std::cout
        << "HELLO sera enviado automaticamente "
        << "a cada 1 segundo ate o stream iniciar.\n\n";


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


    std::thread audioReceiverThread(
        AudioReceiverThread
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
    // Desbloquear recvfrom()/select()
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


    if (
        audioReceiverThread.joinable())
    {
        audioReceiverThread.join();
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