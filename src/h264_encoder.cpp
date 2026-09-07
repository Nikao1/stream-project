#include "h264_encoder.h"

#include <iostream>
#include <string>
#include <vector>

H264Encoder::H264Encoder()
    : m_width(0),
      m_height(0),
      m_fps(60),
      m_bitrateMbps(8),
      m_process(nullptr),
      m_stdinWrite(nullptr),
      m_stdoutRead(nullptr),
      m_running(false)
{
}

H264Encoder::~H264Encoder()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool H264Encoder::Start(
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    uint32_t bitrateMbps)
{
    Stop();


    if (width == 0 ||
        height == 0 ||
        fps == 0 ||
        bitrateMbps == 0)
    {
        return false;
    }


    m_width =
        width;

    m_height =
        height;

    m_fps =
        fps;

    m_bitrateMbps =
        bitrateMbps;


    std::cout
        << "\n========================================\n"
        << "H264 NVENC\n"
        << "Resolucao: "
        << m_width
        << "x"
        << m_height
        << "\nFPS: "
        << m_fps
        << "\nBitrate: "
        << m_bitrateMbps
        << " Mbps\n"
        << "========================================\n";


    return StartProcess();
}


// ============================================================
// StartProcess
// ============================================================

bool H264Encoder::StartProcess()
{
    SECURITY_ATTRIBUTES sa{};

    sa.nLength =
        sizeof(SECURITY_ATTRIBUTES);

    sa.bInheritHandle =
        TRUE;

    sa.lpSecurityDescriptor =
        nullptr;


    HANDLE stdinRead =
        nullptr;

    HANDLE stdoutWrite =
        nullptr;

    HANDLE stderrHandle =
        nullptr;


    // ========================================================
    // STDIN PIPE
    // ========================================================

    if (!CreatePipe(
            &stdinRead,
            &m_stdinWrite,
            &sa,
            0))
    {
        std::cerr
            << "H264Encoder: CreatePipe stdin falhou. "
            << "Erro: "
            << GetLastError()
            << "\n";

        return false;
    }


    // O processo pai precisa manter somente o lado de escrita.

    if (!SetHandleInformation(
            m_stdinWrite,
            HANDLE_FLAG_INHERIT,
            0))
    {
        std::cerr
            << "H264Encoder: SetHandleInformation stdin falhou. "
            << "Erro: "
            << GetLastError()
            << "\n";


        CloseHandle(stdinRead);
        CloseHandle(m_stdinWrite);

        m_stdinWrite =
            nullptr;

        return false;
    }


    // ========================================================
    // STDOUT PIPE
    // ========================================================

    if (!CreatePipe(
            &m_stdoutRead,
            &stdoutWrite,
            &sa,
            0))
    {
        std::cerr
            << "H264Encoder: CreatePipe stdout falhou. "
            << "Erro: "
            << GetLastError()
            << "\n";


        CloseHandle(stdinRead);
        CloseHandle(m_stdinWrite);

        m_stdinWrite =
            nullptr;

        return false;
    }


    // O processo pai precisa manter somente o lado de leitura.

    if (!SetHandleInformation(
            m_stdoutRead,
            HANDLE_FLAG_INHERIT,
            0))
    {
        std::cerr
            << "H264Encoder: SetHandleInformation stdout falhou. "
            << "Erro: "
            << GetLastError()
            << "\n";


        CloseHandle(stdinRead);
        CloseHandle(m_stdinWrite);
        CloseHandle(m_stdoutRead);
        CloseHandle(stdoutWrite);

        m_stdinWrite =
            nullptr;

        m_stdoutRead =
            nullptr;

        return false;
    }


    // ========================================================
    // STDERR
    // ========================================================
    //
    // Como o FFmpeg é executado em background, não precisamos
    // que ele escreva mensagens no console.
    //
    // Em caso de erro, o código de saída/processo ainda pode
    // ser investigado posteriormente.
    // ========================================================

    stderrHandle =
        CreateFileW(
            L"NUL",
            GENERIC_WRITE,
            FILE_SHARE_READ |
            FILE_SHARE_WRITE,
            &sa,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );


    if (stderrHandle ==
        INVALID_HANDLE_VALUE)
    {
        stderrHandle =
            nullptr;
    }


    // ========================================================
    // COMANDO FFMPEG
    // ========================================================

    std::string command =
        "ffmpeg "
        "-hide_banner "
        "-loglevel error "

        // ----------------------------------------------------
        // Entrada:
        // BGRA cru vindo do Windows Graphics Capture
        // ----------------------------------------------------

        "-f rawvideo "
        "-pix_fmt bgra "
        "-video_size "
        + std::to_string(m_width)
        + "x"
        + std::to_string(m_height)
        + " "
        "-framerate "
        + std::to_string(m_fps)
        + " "
        "-i pipe:0 "

        // ----------------------------------------------------
        // H264 NVENC
        // ----------------------------------------------------

        "-an "
        "-c:v h264_nvenc "
        "-preset p1 "
        "-tune ll "

        // ----------------------------------------------------
        // CBR
        // ----------------------------------------------------

        "-rc cbr "
        "-b:v "
        + std::to_string(m_bitrateMbps)
        + "M "
        "-maxrate "
        + std::to_string(m_bitrateMbps)
        + "M "
        "-bufsize "
        + std::to_string(m_bitrateMbps * 2)
        + "M "

        // ----------------------------------------------------
        // Baixa latencia
        // ----------------------------------------------------

        "-bf 0 "
        "-g "
        + std::to_string(m_fps)
        + " "
        "-keyint_min "
        + std::to_string(m_fps)
        + " "
        "-forced-idr 1 "
        "-zerolatency 1 "

        // ----------------------------------------------------
        // MPEG-TS
        // ----------------------------------------------------

        "-f mpegts "
        "-muxdelay 0 "
        "-muxpreload 0 "
        "-flush_packets 1 "

        "pipe:1";


    std::vector<char> commandLine(
        command.begin(),
        command.end()
    );


    commandLine.push_back('\0');


    // ========================================================
    // STARTUPINFO
    // ========================================================

    STARTUPINFOA startupInfo{};

    startupInfo.cb =
        sizeof(STARTUPINFOA);

    startupInfo.dwFlags =
        STARTF_USESTDHANDLES;


    startupInfo.hStdInput =
        stdinRead;


    startupInfo.hStdOutput =
        stdoutWrite;


    if (stderrHandle)
    {
        startupInfo.hStdError =
            stderrHandle;
    }
    else
    {
        startupInfo.hStdError =
            GetStdHandle(
                STD_ERROR_HANDLE
            );
    }


    // ========================================================
    // CREATE PROCESS
    // ========================================================

    PROCESS_INFORMATION processInfo{};


    BOOL result =
        CreateProcessA(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo
        );


    // --------------------------------------------------------
    // Esses handles pertencem ao processo filho agora.
    // O pai não precisa mais deles.
    // --------------------------------------------------------

    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);


    if (stderrHandle)
    {
        CloseHandle(stderrHandle);

        stderrHandle =
            nullptr;
    }


    if (!result)
    {
        DWORD error =
            GetLastError();


        std::cerr
            << "H264Encoder: CreateProcess FFmpeg falhou. "
            << "Erro: "
            << error
            << "\n";


        CloseHandle(m_stdinWrite);
        CloseHandle(m_stdoutRead);


        m_stdinWrite =
            nullptr;

        m_stdoutRead =
            nullptr;


        return false;
    }


    // ========================================================
    // Fechar thread handle do processo
    // ========================================================

    CloseHandle(
        processInfo.hThread
    );


    m_process =
        processInfo.hProcess;


    m_running =
        true;


    // ========================================================
    // Limpar buffer de saída
    // ========================================================

    {
        std::lock_guard<std::mutex> lock(
            m_outputMutex
        );


        m_encodedData.clear();
    }


    // ========================================================
    // THREAD DE LEITURA
    // ========================================================
    //
    // Muito importante:
    //
    // O FFmpeg escreve MPEG-TS no stdout.
    //
    // Se ninguém drenar esse pipe, ele pode ficar cheio e
    // bloquear o encoder.
    //
    // Por isso temos uma thread dedicada exclusivamente
    // a ler stdout.
    // ========================================================

    m_readerThread =
        std::thread(
            &H264Encoder::ReaderThread,
            this
        );


    std::cout
        << "H264Encoder: FFmpeg + NVENC iniciado.\n";


    return true;
}


// ============================================================
// EncodeFrame
// ============================================================

bool H264Encoder::EncodeFrame(
    const void* bgraData,
    uint32_t dataSize)
{
    if (!m_running)
        return false;


    if (!m_stdinWrite)
        return false;


    if (!bgraData ||
        dataSize == 0)
    {
        return false;
    }


    const unsigned char* bytes =
        static_cast<
            const unsigned char*
        >(bgraData);


    uint32_t totalWritten =
        0;


    // ========================================================
    // WriteFile pode escrever menos bytes do que foi solicitado.
    //
    // Portanto precisamos continuar escrevendo até enviar
    // o frame inteiro.
    // ========================================================

    while (totalWritten < dataSize)
    {
        DWORD bytesWritten =
            0;


        DWORD remaining =
            dataSize -
            totalWritten;


        BOOL result =
            WriteFile(
                m_stdinWrite,

                bytes +
                    totalWritten,

                remaining,

                &bytesWritten,

                nullptr
            );


        if (!result ||
            bytesWritten == 0)
        {
            DWORD error =
                GetLastError();


            std::cerr
                << "H264Encoder: erro escrevendo frame. "
                << "Erro: "
                << error
                << "\n";


            m_running =
                false;


            return false;
        }


        totalWritten +=
            bytesWritten;
    }


    return true;
}


// ============================================================
// ReaderThread
// ============================================================

void H264Encoder::ReaderThread()
{
    unsigned char buffer[
        64 * 1024
    ];


    while (m_running)
    {
        DWORD bytesRead =
            0;


        BOOL result =
            ReadFile(
                m_stdoutRead,

                buffer,

                sizeof(buffer),

                &bytesRead,

                nullptr
            );


        if (!result)
        {
            DWORD error =
                GetLastError();


            // ------------------------------------------------
            // Durante o encerramento isso é esperado.
            // ------------------------------------------------

            if (!m_running)
                break;


            std::cerr
                << "H264Encoder: erro lendo stdout do FFmpeg. "
                << "Erro: "
                << error
                << "\n";


            break;
        }


        if (bytesRead == 0)
        {
            break;
        }


        // ====================================================
        // Guardar MPEG-TS produzido pelo FFmpeg
        // ====================================================

        {
            std::lock_guard<std::mutex> lock(
                m_outputMutex
            );


            m_encodedData.insert(
                m_encodedData.end(),

                buffer,

                buffer +
                    bytesRead
            );


            // ------------------------------------------------
            // Limite de segurança.
            //
            // 8 MB é suficiente para impedir crescimento
            // infinito caso o envio UDP fique muito lento.
            // ------------------------------------------------

            constexpr size_t MAX_BUFFER =
                8 * 1024 * 1024;


            if (m_encodedData.size() >
                MAX_BUFFER)
            {
                std::cerr
                    << "\nH264Encoder: buffer de saida "
                    << "excedeu 8 MB. "
                    << "Limpando buffer antigo.\n";


                m_encodedData.clear();
            }
        }
    }


    m_running =
        false;
}


// ============================================================
// GetEncodedData
// ============================================================

bool H264Encoder::GetEncodedData(
    std::vector<unsigned char>& output)
{
    std::lock_guard<std::mutex> lock(
        m_outputMutex
    );


    if (m_encodedData.empty())
    {
        return false;
    }


    output.swap(
        m_encodedData
    );


    return true;
}


// ============================================================
// Stop
// ============================================================

void H264Encoder::Stop()
{
    // ========================================================
    // Marcar como parado
    // ========================================================

    m_running =
        false;


    // ========================================================
    // Fechar stdin
    // ========================================================
    //
    // Isso envia EOF para o FFmpeg.
    //
    // O processo pode então finalizar naturalmente.
    // ========================================================

    if (m_stdinWrite)
    {
        CloseHandle(
            m_stdinWrite
        );


        m_stdinWrite =
            nullptr;
    }


    // ========================================================
    // Esperar ReaderThread
    // ========================================================

    if (m_readerThread.joinable())
    {
        m_readerThread.join();
    }


    // ========================================================
    // Fechar stdout
    // ========================================================

    if (m_stdoutRead)
    {
        CloseHandle(
            m_stdoutRead
        );


        m_stdoutRead =
            nullptr;
    }


    // ========================================================
    // Finalizar processo FFmpeg
    // ========================================================

    if (m_process)
    {
        DWORD exitCode =
            0;


        if (GetExitCodeProcess(
                m_process,
                &exitCode))
        {
            if (exitCode ==
                STILL_ACTIVE)
            {
                // ------------------------------------------------
                // Normalmente fechar stdin já deve ter feito
                // o FFmpeg terminar.
                //
                // Caso ainda esteja ativo, encerramos o processo.
                // ------------------------------------------------

                TerminateProcess(
                    m_process,
                    0
                );


                WaitForSingleObject(
                    m_process,
                    1000
                );
            }
        }


        CloseHandle(
            m_process
        );


        m_process =
            nullptr;
    }


    // ========================================================
    // Limpar saída acumulada
    // ========================================================

    {
        std::lock_guard<std::mutex> lock(
            m_outputMutex
        );


        m_encodedData.clear();
    }
}


// ============================================================
// IsRunning
// ============================================================

bool H264Encoder::IsRunning() const
{
    return m_running;
}