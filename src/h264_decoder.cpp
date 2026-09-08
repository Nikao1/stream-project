#include "h264_decoder.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstring>


H264Decoder::H264Decoder()
    : m_width(0),
      m_height(0),
      m_process(nullptr),
      m_stdinWrite(nullptr),
      m_stdoutRead(nullptr),
      m_running(false)
{
}


H264Decoder::~H264Decoder()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool H264Decoder::Start(
    uint32_t width,
    uint32_t height)
{
    Stop();


    if (width == 0 ||
        height == 0)
    {
        return false;
    }


    m_width =
        width;

    m_height =
        height;


    std::cout
        << "\n========================================\n"
        << "H264 DECODER\n"
        << "Resolucao: "
        << width
        << "x"
        << height
        << "\n========================================\n";


    return StartProcess();
}


// ============================================================
// StartProcess
// ============================================================

bool H264Decoder::StartProcess()
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
    // STDIN
    // ========================================================

    if (!CreatePipe(
            &stdinRead,
            &m_stdinWrite,
            &sa,
            0))
    {
        std::cerr
            << "H264Decoder: CreatePipe stdin falhou. "
            << "Erro: "
            << GetLastError()
            << "\n";

        return false;
    }


    if (!SetHandleInformation(
            m_stdinWrite,
            HANDLE_FLAG_INHERIT,
            0))
    {
        std::cerr
            << "H264Decoder: SetHandleInformation stdin falhou. "
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
    // STDOUT
    // ========================================================

    if (!CreatePipe(
            &m_stdoutRead,
            &stdoutWrite,
            &sa,
            0))
    {
        std::cerr
            << "H264Decoder: CreatePipe stdout falhou. "
            << "Erro: "
            << GetLastError()
            << "\n";


        CloseHandle(stdinRead);
        CloseHandle(m_stdinWrite);


        m_stdinWrite =
            nullptr;


        return false;
    }


    if (!SetHandleInformation(
            m_stdoutRead,
            HANDLE_FLAG_INHERIT,
            0))
    {
        std::cerr
            << "H264Decoder: SetHandleInformation stdout falhou. "
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
        // Baixa latencia
        // ----------------------------------------------------

        "-fflags nobuffer "
        "-flags low_delay "

        // ----------------------------------------------------
        // Decodificacao por HARDWARE (D3D11VA)
        //
        // Sem isso, o FFmpeg decodifica H264 por software
        // (CPU), o que costuma travar por volta de 30-45 FPS
        // em 1080p dependendo do processador. D3D11VA usa o
        // decoder de video dedicado da GPU (funciona com
        // NVIDIA/AMD/Intel no Windows).
        // ----------------------------------------------------

        "-hwaccel d3d11va "

        // ----------------------------------------------------
        // Entrada H264 Annex B puro (sem container)
        //
        // O receiver só escreve neste pipe frames completos
        // e íntegros (já reassemblados a partir dos fragmentos
        // UDP), então o demuxer nunca vê dado corrompido por
        // perda de pacote.
        // ----------------------------------------------------

        "-f h264 "
        "-i pipe:0 "

        // ----------------------------------------------------
        // Somente video
        // ----------------------------------------------------

        "-an "

        // ----------------------------------------------------
        // Saida BGRA
        // ----------------------------------------------------

        "-pix_fmt bgra "

        // ----------------------------------------------------
        // Nao duplicar frames
        // ----------------------------------------------------

        "-fps_mode passthrough "

        // ----------------------------------------------------
        // Raw video
        // ----------------------------------------------------

        "-f rawvideo "
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


    PROCESS_INFORMATION processInfo{};


    // ========================================================
    // CRIAR PROCESSO
    // ========================================================

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


    // O processo filho já recebeu esses handles.

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
            << "H264Decoder: nao foi possivel iniciar FFmpeg. "
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


    CloseHandle(
        processInfo.hThread
    );


    m_process =
        processInfo.hProcess;


    // ========================================================
    // Limpar buffers
    // ========================================================

    {
        std::lock_guard<std::mutex> lock(
            m_frameMutex
        );


        m_decoderBuffer.clear();
        m_latestFrame.clear();
    }


    m_running =
        true;


    // ========================================================
    // THREAD DE LEITURA
    // ========================================================

    m_readerThread =
        std::thread(
            &H264Decoder::ReaderThread,
            this
        );


    std::cout
        << "H264Decoder: FFmpeg iniciado.\n";


    return true;
}


// ============================================================
// WriteData
// ============================================================

bool H264Decoder::WriteData(
    const void* data,
    uint32_t size)
{
    if (!m_running)
        return false;


    if (!m_stdinWrite)
        return false;


    if (!data ||
        size == 0)
    {
        return false;
    }


    const unsigned char* bytes =
        static_cast<
            const unsigned char*
        >(data);


    uint32_t totalWritten =
        0;


    // ========================================================
    // Garantir que todos os bytes MPEG-TS sejam enviados
    // para o stdin do FFmpeg.
    // ========================================================

    while (totalWritten < size)
    {
        DWORD bytesWritten =
            0;


        DWORD remaining =
            size -
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
                << "H264Decoder: erro escrevendo dados "
                << "no FFmpeg. Erro: "
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

void H264Decoder::ReaderThread()
{
    const size_t frameSize =
        static_cast<size_t>(
            m_width
        ) *
        static_cast<size_t>(
            m_height
        ) *
        4;


    if (frameSize == 0)
    {
        m_running =
            false;

        return;
    }


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


            if (!m_running)
                break;


            std::cerr
                << "H264Decoder: erro lendo stdout "
                << "do FFmpeg. Erro: "
                << error
                << "\n";


            break;
        }


        if (bytesRead == 0)
        {
            break;
        }


        // ====================================================
        // Adicionar bytes recebidos ao buffer
        // ====================================================

        m_decoderBuffer.insert(
            m_decoderBuffer.end(),

            buffer,

            buffer +
                bytesRead
        );


        // ====================================================
        // Extrair frames completos
        // ====================================================

        while (
            m_decoderBuffer.size() >=
            frameSize)
        {
            std::vector<unsigned char> frame;

            frame.resize(
                frameSize
            );


            std::memcpy(
                frame.data(),

                m_decoderBuffer.data(),

                frameSize
            );


            // ------------------------------------------------
            // Em vez de erase(), movemos apenas o restante
            // para o começo do buffer.
            // ------------------------------------------------

            const size_t remaining =
                m_decoderBuffer.size() -
                frameSize;


            if (remaining > 0)
            {
                std::memmove(
                    m_decoderBuffer.data(),

                    m_decoderBuffer.data() +
                        frameSize,

                    remaining
                );
            }


            m_decoderBuffer.resize(
                remaining
            );


            // =================================================
            // Guardar somente o frame mais recente
            // =================================================

            {
                std::lock_guard<std::mutex> lock(
                    m_frameMutex
                );


                m_latestFrame =
                    std::move(frame);
            }
        }
    }


    m_running =
        false;
}


// ============================================================
// GetFrame
// ============================================================

bool H264Decoder::GetFrame(
    std::vector<unsigned char>& frame)
{
    std::lock_guard<std::mutex> lock(
        m_frameMutex
    );


    if (m_latestFrame.empty())
    {
        return false;
    }


    frame.swap(
        m_latestFrame
    );


    return true;
}


// ============================================================
// Stop
// ============================================================

void H264Decoder::Stop()
{
    m_running =
        false;


    // ========================================================
    // Fechar stdin do FFmpeg
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
    // Esperar thread de leitura
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
    // Encerrar processo FFmpeg
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
    // Limpar buffers
    // ========================================================

    {
        std::lock_guard<std::mutex> lock(
            m_frameMutex
        );


        m_decoderBuffer.clear();

        m_latestFrame.clear();
    }
}


// ============================================================
// IsRunning
// ============================================================

bool H264Decoder::IsRunning() const
{
    return m_running;
}