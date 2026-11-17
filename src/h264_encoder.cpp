#include "h264_encoder.h"

#include <iostream>
#include <string>
#include <vector>

#include <dxgi.h>
#pragma comment(lib, "dxgi.lib")


// ============================================================
// DetectGpuVendor
// ============================================================
//
// Consulta o primeiro adaptador de video via DXGI pra escolher
// o encoder de hardware certo (NVENC/AMF/QSV). Isso é o que
// permite o projeto funcionar em qualquer GPU, não só NVIDIA.
// ============================================================

namespace
{
    GpuVendor DetectGpuVendor()
    {
        IDXGIFactory1* factory =
            nullptr;


        if (FAILED(
                CreateDXGIFactory1(
                    __uuidof(IDXGIFactory1),
                    reinterpret_cast<void**>(&factory)
                )))
        {
            return GpuVendor::Unknown;
        }


        GpuVendor result =
            GpuVendor::Unknown;


        IDXGIAdapter1* adapter =
            nullptr;


        if (SUCCEEDED(
                factory->EnumAdapters1(
                    0,
                    &adapter
                )))
        {
            DXGI_ADAPTER_DESC1 desc{};


            if (SUCCEEDED(
                    adapter->GetDesc1(&desc)))
            {
                switch (desc.VendorId)
                {
                    case 0x10DE: // NVIDIA
                        result = GpuVendor::Nvidia;
                        break;

                    case 0x1002: // AMD
                    case 0x1022:
                        result = GpuVendor::Amd;
                        break;

                    case 0x8086: // Intel
                        result = GpuVendor::Intel;
                        break;

                    default:
                        result = GpuVendor::Unknown;
                        break;
                }


                int size =
                    WideCharToMultiByte(
                        CP_UTF8, 0,
                        desc.Description, -1,
                        nullptr, 0,
                        nullptr, nullptr
                    );


                std::string narrowDesc(
                    size > 0 ? size - 1 : 0,
                    '\0'
                );


                if (size > 0)
                {
                    WideCharToMultiByte(
                        CP_UTF8, 0,
                        desc.Description, -1,
                        narrowDesc.data(), size,
                        nullptr, nullptr
                    );
                }


                std::cout
                    << "H264Encoder: GPU detectada - "
                    << narrowDesc
                    << "\n";
            }


            adapter->Release();
        }


        factory->Release();


        return result;
    }
}

H264Encoder::H264Encoder()
    : m_width(0),
      m_height(0),
      m_fps(60),
      m_bitrateMbps(8),
      m_outputHeight(0),
      m_actualOutputWidth(0),
      m_actualOutputHeight(0),
      m_process(nullptr),
      m_stdinWrite(nullptr),
      m_stdoutRead(nullptr),
      m_currentAccessUnitKeyframe(false),
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
    uint32_t bitrateMbps,
    uint32_t outputHeight)
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

    m_outputHeight =
        outputHeight;


    // ----------------------------------------------------------
    // Tamanho real de saída (após escala, se houver)
    //
    // Espelha o cálculo do filtro "-vf scale=-2:H" do ffmpeg:
    // largura proporcional à altura, sempre arredondada pra
    // baixo em número par (exigência do H264/H264_nvenc).
    // ----------------------------------------------------------

    if (m_outputHeight > 0 &&
        m_outputHeight < m_height)
    {
        m_actualOutputHeight =
            m_outputHeight;


        double scale =
            static_cast<double>(m_outputHeight) /
            static_cast<double>(m_height);


        uint32_t scaledWidth =
            static_cast<uint32_t>(
                m_width * scale
            );


        // Arredondar pra baixo em número par
        m_actualOutputWidth =
            scaledWidth -
            (scaledWidth % 2);
    }
    else
    {
        // Sem escala (nativo, ou pediram uma altura maior que
        // a nativa - nesse caso não faz sentido fazer upscale)
        m_outputHeight =
            0;

        m_actualOutputWidth =
            m_width;

        m_actualOutputHeight =
            m_height;
    }


    // ----------------------------------------------------------
    // Detectar GPU e escolher o encoder de hardware certo
    // (NVENC/AMF/QSV) - isso é o que permite o projeto rodar em
    // qualquer GPU, não só NVIDIA.
    // ----------------------------------------------------------

    m_gpuVendor =
        DetectGpuVendor();


    const char* encoderName =
        "libx264 (software)";


    switch (m_gpuVendor)
    {
        case GpuVendor::Nvidia:
            encoderName = "h264_nvenc (NVIDIA)";
            break;

        case GpuVendor::Amd:
            encoderName = "h264_amf (AMD)";
            break;

        case GpuVendor::Intel:
            encoderName = "h264_qsv (Intel)";
            break;

        default:
            encoderName =
                "libx264 (software - GPU nao "
                "reconhecida)";
            break;
    }


    std::cout
        << "\n========================================\n"
        << "H264 ENCODER: "
        << encoderName
        << "\nResolucao: "
        << m_width
        << "x"
        << m_height
        << "\nFPS: "
        << m_fps
        << "\nBitrate: "
        << m_bitrateMbps
        << " Mbps\n"
        << "========================================\n";


    if (!StartProcess(false))
    {
        return false;
    }


    // ----------------------------------------------------------
    // Health check: encoders de hardware podem falhar de forma
    // assincrona (ex: driver desatualizado, sem NVENC/AMF/QSV
    // capaz) - o ffmpeg simplesmente encerra com erro logo no
    // inicio nesse caso. Se isso acontecer, tentamos de novo
    // com libx264 (software), que funciona em qualquer CPU.
    // ----------------------------------------------------------

    if (m_gpuVendor != GpuVendor::Unknown)
    {
        Sleep(700);


        DWORD exitCode =
            0;


        if (m_process &&
            GetExitCodeProcess(
                m_process,
                &exitCode) &&
            exitCode != STILL_ACTIVE)
        {
            std::cout
                << "H264Encoder: encoder de hardware "
                << "falhou ao iniciar (codigo "
                << exitCode
                << "). Tentando fallback por "
                << "software (libx264)...\n";


            Stop();


            if (!StartProcess(true))
            {
                return false;
            }


            std::cout
                << "H264Encoder: rodando com libx264 "
                << "(software).\n";
        }
    }


    return true;
}


// ============================================================
// BuildFfmpegCommand
// ============================================================
//
// Monta o comando do ffmpeg. A entrada (BGRA cru) e a saida
// (H264 Annex B) são iguais pra qualquer GPU - só o bloco do
// codec em si muda, pra usar o encoder de hardware certo
// (NVENC/AMF/QSV) ou cair pra libx264 (software, funciona em
// qualquer CPU) se pedido ou se nenhuma GPU reconhecida foi
// detectada.
// ============================================================

std::string H264Encoder::BuildFfmpegCommand(
    bool useSoftwareFallback)
{
    // --------------------------------------------------------
    // Entrada + escala (iguais pra qualquer encoder)
    // --------------------------------------------------------

    std::string command =
        "ffmpeg "
        "-hide_banner "
        "-loglevel error "

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

        // "-2" na largura deixa o ffmpeg calcular mantendo a
        // proporção original, sempre em número par (exigência
        // do H264).

        + (m_outputHeight > 0
            // Largura explicita (a mesma que vai no header de
            // rede), em vez de "-2": o ffmpeg arredonda o -2
            // pro par MAIS PROXIMO, e nossa conta arredonda pra
            // baixo - as vezes dava 2 pixels de diferenca
            // entre o video real e o tamanho anunciado.
            ? "-vf scale=" +
              std::to_string(m_actualOutputWidth) +
              ":" +
              std::to_string(m_actualOutputHeight) +
              " "
            : "")

        + "-an ";


    // --------------------------------------------------------
    // Bloco do codec - varia por fabricante de GPU
    // --------------------------------------------------------

    GpuVendor vendor =
        useSoftwareFallback
        ? GpuVendor::Unknown
        : m_gpuVendor;


    // --------------------------------------------------------
    // Tamanho do buffer de controle de taxa (VBV)
    //
    // Define o quanto UM frame pode passar da media. Antes era
    // 2 SEGUNDOS de video: um keyframe podia sair com centenas
    // de KB (centenas de pacotes UDP de uma vez). Na internet,
    // uma rajada dessas perde pacotes facil - e um pacote
    // perdido inutiliza o frame inteiro, congelando a imagem
    // ate o proximo keyframe. Em 1080p60 isso aparecia como
    // travadas curtas de vez em quando.
    //
    // Com ~2 frames de buffer (o que hosts de streaming em
    // tempo real costumam usar), o keyframe fica so um pouco
    // maior que um frame comum. Custo: o keyframe sai com um
    // pouco menos de detalhe, recuperado nos frames seguintes.
    // --------------------------------------------------------

    const uint32_t vbvKbits =
        (m_bitrateMbps * 1000u * 2u) /
        (m_fps > 0 ? m_fps : 60u);

    const std::string vbv =
        "-bufsize " +
        std::to_string(vbvKbits < 100u ? 100u : vbvKbits) +
        "k ";


    switch (vendor)
    {
        case GpuVendor::Nvidia:
        {
            // p1 e o mais rapido/pior qualidade do NVENC.
            // Usamos p4 (meio-termo) - o NVENC e um chip de
            // encode dedicado, entao subir o preset custa
            // muito pouca GPU (poucos ms a mais por frame),
            // mas melhora bastante a qualidade em movimento.
            // "tune ll" mantem baixa latencia independente do
            // preset. Adaptive Quantization (spatial/temporal)
            // aloca mais bits pras partes complexas/em
            // movimento da imagem em vez de gastar igual em
            // tudo - custo de GPU desprezivel.

            command +=
                "-c:v h264_nvenc "
                "-preset p4 "
                "-tune ll "
                "-spatial-aq 1 "
                "-temporal-aq 1 "
                "-aq-strength 8 "
                "-rc cbr "
                "-b:v " + std::to_string(m_bitrateMbps) + "M "
                "-maxrate " + std::to_string(m_bitrateMbps) + "M " +
                vbv +
                "-bf 0 "
                "-forced-idr 1 "
                "-zerolatency 1 ";

            break;
        }

        case GpuVendor::Amd:
        {
            // AMF: "usage ultralowlatency" já configura um
            // perfil de baixa latência adequado pra streaming
            // em tempo real (equivalente ao "tune ll" do
            // NVENC).

            command +=
                "-c:v h264_amf "
                "-usage ultralowlatency "
                "-quality speed "
                "-rc cbr "
                "-b:v " + std::to_string(m_bitrateMbps) + "M "
                "-maxrate " + std::to_string(m_bitrateMbps) + "M " +
                vbv +
                "-bf 0 ";

            break;
        }

        case GpuVendor::Intel:
        {
            // QSV: "look_ahead 0" desativa o lookahead (que
            // adiciona atraso pra melhor qualidade) - queremos
            // a menor latência possível aqui.

            command +=
                "-c:v h264_qsv "
                "-preset veryfast "
                "-look_ahead 0 "
                "-rc cbr "
                "-b:v " + std::to_string(m_bitrateMbps) + "M "
                "-maxrate " + std::to_string(m_bitrateMbps) + "M " +
                vbv +
                "-bf 0 ";

            break;
        }

        default:
        {
            // Fallback por SOFTWARE (qualquer CPU, sem
            // depender de GPU). "veryfast" + "zerolatency" e a
            // combinacao padrao do libx264 pra streaming em
            // tempo real - troca qualidade por velocidade o
            // suficiente pra rodar sem acumular atraso numa
            // CPU comum.

            command +=
                "-c:v libx264 "
                "-preset veryfast "
                "-tune zerolatency "
                "-x264-params nal-hrd=cbr "
                "-b:v " + std::to_string(m_bitrateMbps) + "M "
                "-maxrate " + std::to_string(m_bitrateMbps) + "M " +
                vbv +
                "-bf 0 ";

            break;
        }
    }


    // --------------------------------------------------------
    // GOP + saida Annex B puro (iguais pra qualquer encoder)
    //
    // Sem container. Cada NAL unit é delimitada por start
    // codes (00 00 01 / 00 00 00 01), o que permite montar o
    // framing por frame no lado do UDP/receiver sem o overhead
    // e a fragilidade do MPEG-TS sobre transporte não
    // confiável.
    // --------------------------------------------------------

    command +=
        "-g " + std::to_string(m_fps) + " "
        "-keyint_min " + std::to_string(m_fps) + " "
        "-f h264 "
        "pipe:1";


    return command;
}


// ============================================================
// StartProcess
// ============================================================

bool H264Encoder::StartProcess(
    bool useSoftwareFallback)
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
        BuildFfmpegCommand(
            useSoftwareFallback
        );


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


        m_nalBuffer.clear();

        m_currentAccessUnit.clear();

        m_currentAccessUnitKeyframe =
            false;

        m_frameQueue.clear();

        m_skipUntilKeyframe =
            false;
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
// FindStartCode
// ============================================================
//
// Procura o próximo start code Annex B (00 00 01 ou 00 00 00 01)
// a partir de "from". Retorna o offset do primeiro byte do
// start code, ou SIZE_MAX se não encontrar (NAL ainda incompleta,
// aguardando mais dados do FFmpeg).
//
// scLen recebe o tamanho do start code encontrado (3 ou 4).
// ============================================================

namespace
{
    constexpr size_t kNotFound =
        static_cast<size_t>(-1);


    size_t FindStartCode(
        const std::vector<unsigned char>& buffer,
        size_t from,
        size_t& scLen)
    {
        if (buffer.size() < 3)
        {
            return kNotFound;
        }


        for (size_t i = from;
             i + 3 <= buffer.size();
             ++i)
        {
            if (buffer[i] != 0 ||
                buffer[i + 1] != 0)
            {
                continue;
            }


            if (buffer[i + 2] == 1)
            {
                scLen = 3;
                return i;
            }


            if (i + 4 <= buffer.size() &&
                buffer[i + 2] == 0 &&
                buffer[i + 3] == 1)
            {
                scLen = 4;
                return i;
            }
        }


        return kNotFound;
    }
}


// ============================================================
// ExtractAccessUnits
// ============================================================
//
// Percorre m_nalBuffer, separa as NAL units já completas
// (delimitadas por start codes) e agrupa em access units
// (frames).
//
// PREMISSA: cada frame gerado pelo NVENC nesta configuração
// (sem slicing) produz no máximo uma NAL unit de video (VCL,
// tipo 1 ou 5), opcionalmente precedida por SPS(7)/PPS(8)/SEI(6)
// nos keyframes. Por isso: fechamos o access unit atual assim
// que encontramos uma NAL VCL.
//
// Chamado com m_outputMutex já travado.
// ============================================================

void H264Encoder::ExtractAccessUnits()
{
    size_t scLen = 0;


    size_t nalStart =
        FindStartCode(
            m_nalBuffer,
            0,
            scLen
        );


    if (nalStart == kNotFound)
    {
        return;
    }


    for (;;)
    {
        size_t nextScLen = 0;


        size_t nextStart =
            FindStartCode(
                m_nalBuffer,
                nalStart + scLen,
                nextScLen
            );


        if (nextStart == kNotFound)
        {
            // A NAL atual ainda não terminou - pode chegar
            // mais dado do FFmpeg na próxima leitura.
            break;
        }


        // ----------------------------------------------------
        // Tipo da NAL (5 bits menos significativos do primeiro
        // byte após o start code).
        // ----------------------------------------------------

        uint8_t nalType =
            m_nalBuffer[nalStart + scLen] &
            0x1F;


        bool isVcl =
            (nalType >= 1 &&
             nalType <= 5);


        bool isParamSet =
            (nalType == 7 ||  // SPS
             nalType == 8);   // PPS


        if (nalType == 5 ||
            isParamSet)
        {
            m_currentAccessUnitKeyframe =
                true;
        }


        // ----------------------------------------------------
        // Adiciona essa NAL (start code + payload) ao access
        // unit em montagem.
        // ----------------------------------------------------

        m_currentAccessUnit.insert(
            m_currentAccessUnit.end(),
            m_nalBuffer.begin() + nalStart,
            m_nalBuffer.begin() + nextStart
        );


        nalStart = nextStart;
        scLen = nextScLen;


        if (isVcl)
        {
            // ------------------------------------------------
            // Fecha o access unit: é um frame completo.
            // ------------------------------------------------

            EncodedFrame frame;

            frame.data =
                std::move(
                    m_currentAccessUnit
                );

            frame.isKeyframe =
                m_currentAccessUnitKeyframe;


            // --------------------------------------------------
            // Fila de saida
            //
            // Se o envio nao acompanhar o encoder, a fila
            // enche. Antes, os frames mais antigos eram
            // descartados em silencio - mas um frame comum
            // depende do anterior, e sem ele o receiver
            // decodificava imagem corrompida (ou o decoder
            // parava). Agora, se for preciso descartar, descarta
            // tudo ate o proximo keyframe: a imagem congela por
            // ate 1s, mas volta limpa.
            //
            // O limite cobre a rajada de um keyframe sem
            // descartar nada no uso normal.
            // --------------------------------------------------

            constexpr size_t MAX_QUEUED_FRAMES =
                6;

            bool isKeyframe =
                frame.isKeyframe;

            if (m_skipUntilKeyframe &&
                !isKeyframe)
            {
                ++m_droppedFrames;
            }
            else
            {
                m_skipUntilKeyframe =
                    false;

                m_frameQueue.push_back(
                    std::move(frame)
                );

                if (m_frameQueue.size() >
                    MAX_QUEUED_FRAMES)
                {
                    m_droppedFrames +=
                        m_frameQueue.size();

                    m_frameQueue.clear();

                    m_skipUntilKeyframe =
                        true;

                    std::cout
                        << "H264Encoder: envio atrasado - "
                        << "descartando ate o proximo "
                        << "keyframe (total descartado: "
                        << m_droppedFrames
                        << ").\n";
                }
            }


            m_currentAccessUnit.clear();

            m_currentAccessUnitKeyframe =
                false;
        }
    }


    // ========================================================
    // Remove do buffer os bytes já processados. O que sobra é
    // o começo de uma NAL ainda incompleta.
    // ========================================================

    m_nalBuffer.erase(
        m_nalBuffer.begin(),
        m_nalBuffer.begin() + nalStart
    );
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
        // Acumular bytes Annex B crus e tentar extrair frames
        // completos.
        // ====================================================

        {
            std::lock_guard<std::mutex> lock(
                m_outputMutex
            );


            m_nalBuffer.insert(
                m_nalBuffer.end(),

                buffer,

                buffer +
                    bytesRead
            );


            ExtractAccessUnits();


            // ------------------------------------------------
            // Limite de segurança para o buffer de bytes ainda
            // não agrupados (não deveria crescer muito, já que
            // é drenado a cada NAL completa).
            // ------------------------------------------------

            constexpr size_t MAX_NAL_BUFFER =
                4 * 1024 * 1024;


            if (m_nalBuffer.size() >
                MAX_NAL_BUFFER)
            {
                std::cerr
                    << "\nH264Encoder: buffer Annex B "
                    << "excedeu 4 MB. Limpando.\n";


                m_nalBuffer.clear();
            }
        }
    }


    m_running =
        false;
}


// ============================================================
// GetFrame
// ============================================================

bool H264Encoder::GetFrame(
    EncodedFrame& outFrame)
{
    std::lock_guard<std::mutex> lock(
        m_outputMutex
    );


    if (m_frameQueue.empty())
    {
        return false;
    }


    outFrame =
        std::move(
            m_frameQueue.front()
        );


    m_frameQueue.pop_front();


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


        m_nalBuffer.clear();

        m_currentAccessUnit.clear();

        m_currentAccessUnitKeyframe =
            false;

        m_frameQueue.clear();

        m_skipUntilKeyframe =
            false;
    }
}


// ============================================================
// IsRunning
// ============================================================

bool H264Encoder::IsRunning() const
{
    return m_running;
}