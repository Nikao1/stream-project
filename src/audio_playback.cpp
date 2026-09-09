#include "audio_playback.h"
#include "audio_protocol.h"

#include <mmdeviceapi.h>
#include <audioclient.h>

#include <iostream>

#pragma comment(lib, "ole32.lib")


// ============================================================
// GUID do subformato IEEE float (WAVE_FORMAT_IEEE_FLOAT)
//
// Normalmente viria de <ksmedia.h> (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
// mas isso pode exigir linkar uma lib extra dependendo do
// toolchain. Como o valor é um GUID padrão e estável do
// Windows, definimos aqui diretamente pra evitar esse
// problema de link.
// ============================================================

static const GUID kSubformatIeeeFloat =
    {
        0x00000003,
        0x0000,
        0x0010,
        { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }
    };


// Flags de canal padrao (evita depender de <mmreg.h>)
static constexpr DWORD kSpeakerFrontLeft  = 0x1;
static constexpr DWORD kSpeakerFrontRight = 0x2;


// ============================================================
// Constructor / Destructor
// ============================================================

AudioPlayback::AudioPlayback()
    : m_deviceEnumerator(nullptr),
      m_device(nullptr),
      m_audioClient(nullptr),
      m_renderClient(nullptr),
      m_running(false),
      m_comInitialized(false)
{
}


AudioPlayback::~AudioPlayback()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool AudioPlayback::Start()
{
    if (m_running)
    {
        return true;
    }


    m_running =
        true;


    m_thread =
        std::thread(
            &AudioPlayback::RenderThread,
            this
        );


    return true;
}


// ============================================================
// IsRunning
// ============================================================

bool AudioPlayback::IsRunning() const
{
    return m_running;
}


// ============================================================
// PushSamples
// ============================================================

void AudioPlayback::PushSamples(
    const std::vector<float>& samples)
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );


    m_pcmQueue.insert(
        m_pcmQueue.end(),
        samples.begin(),
        samples.end()
    );


    // ------------------------------------------------------
    // Limite de seguranca: no maximo ~300ms de audio
    // enfileirado. Se passar disso, o receiver esta
    // recebendo mais rapido do que consegue tocar (ou a
    // reprodução travou) - descarta o mais antigo pra não
    // acumular atraso crescente.
    // ------------------------------------------------------

    constexpr size_t MAX_QUEUED_SAMPLES =
        (AUDIO_SAMPLE_RATE * AUDIO_CHANNELS) *
        300 / 1000;


    while (m_pcmQueue.size() >
           MAX_QUEUED_SAMPLES)
    {
        m_pcmQueue.pop_front();
    }
}


// ============================================================
// RenderThread
// ============================================================

void AudioPlayback::RenderThread()
{
    // ========================================================
    // COM
    // ========================================================

    HRESULT hr =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );


    m_comInitialized =
        SUCCEEDED(hr);


    if (FAILED(hr) &&
        hr != RPC_E_CHANGED_MODE)
    {
        std::cout
            << "AudioPlayback: CoInitializeEx falhou. "
            << "HRESULT: "
            << hr
            << "\n";

        m_running =
            false;

        return;
    }


    // ========================================================
    // Device enumerator + dispositivo de saida padrao
    // ========================================================

    hr =
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(
                &m_deviceEnumerator
            )
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioPlayback: falha ao criar "
            << "IMMDeviceEnumerator. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    hr =
        m_deviceEnumerator->GetDefaultAudioEndpoint(
            eRender,
            eConsole,
            &m_device
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioPlayback: falha ao obter dispositivo "
            << "de saida padrao. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    hr =
        m_device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(
                &m_audioClient
            )
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioPlayback: falha ao ativar "
            << "IAudioClient. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    // ========================================================
    // Formato desejado: 48kHz, estereo, float32.
    //
    // Em modo compartilhado o Windows Audio Engine converte
    // automaticamente pro formato nativo do dispositivo, então
    // não precisamos consultar o mix format do dispositivo
    // como fizemos no loopback.
    // ========================================================

    {
        WAVEFORMATEXTENSIBLE format{};

        format.Format.wFormatTag =
            WAVE_FORMAT_EXTENSIBLE;

        format.Format.nChannels =
            static_cast<WORD>(AUDIO_CHANNELS);

        format.Format.nSamplesPerSec =
            AUDIO_SAMPLE_RATE;

        format.Format.wBitsPerSample =
            32;

        format.Format.nBlockAlign =
            static_cast<WORD>(
                format.Format.nChannels *
                format.Format.wBitsPerSample / 8
            );

        format.Format.nAvgBytesPerSec =
            format.Format.nSamplesPerSec *
            format.Format.nBlockAlign;

        format.Format.cbSize =
            22;

        format.Samples.wValidBitsPerSample =
            32;

        format.dwChannelMask =
            kSpeakerFrontLeft |
            kSpeakerFrontRight;

        format.SubFormat =
            kSubformatIeeeFloat;


        constexpr REFERENCE_TIME bufferDuration =
            200 *
            10000; // 200ms


        hr =
            m_audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                0,
                bufferDuration,
                0,
                reinterpret_cast<WAVEFORMATEX*>(
                    &format
                ),
                nullptr
            );
    }


    if (FAILED(hr))
    {
        std::cout
            << "AudioPlayback: falha ao inicializar "
            << "IAudioClient (48kHz/estereo/float nao "
            << "suportado?). HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    UINT32 bufferFrameCount;


    hr =
        m_audioClient->GetBufferSize(
            &bufferFrameCount
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioPlayback: falha ao obter tamanho "
            << "do buffer. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    hr =
        m_audioClient->GetService(
            __uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(
                &m_renderClient
            )
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioPlayback: falha ao obter "
            << "IAudioRenderClient. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    hr =
        m_audioClient->Start();


    if (FAILED(hr))
    {
        std::cout
            << "AudioPlayback: falha ao iniciar "
            << "reprodução. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    std::cout
        << "AudioPlayback: reprodução iniciada ("
        << AUDIO_SAMPLE_RATE
        << " Hz, "
        << AUDIO_CHANNELS
        << " canais, buffer de "
        << bufferFrameCount
        << " frames).\n";


    // ========================================================
    // LOOP DE REPRODUCAO
    // ========================================================

    while (m_running)
    {
        UINT32 padding =
            0;


        hr =
            m_audioClient->GetCurrentPadding(
                &padding
            );


        if (FAILED(hr))
        {
            break;
        }


        UINT32 framesAvailable =
            bufferFrameCount -
            padding;


        if (framesAvailable > 0)
        {
            BYTE* data =
                nullptr;


            hr =
                m_renderClient->GetBuffer(
                    framesAvailable,
                    &data
                );


            if (SUCCEEDED(hr))
            {
                float* out =
                    reinterpret_cast<float*>(
                        data
                    );


                uint32_t samplesNeeded =
                    framesAvailable *
                    AUDIO_CHANNELS;


                std::lock_guard<std::mutex> lock(
                    m_mutex
                );


                uint32_t samplesAvailable =
                    static_cast<uint32_t>(
                        m_pcmQueue.size()
                    );


                uint32_t samplesToCopy =
                    samplesAvailable <
                        samplesNeeded
                    ? samplesAvailable
                    : samplesNeeded;


                for (uint32_t i = 0;
                     i < samplesToCopy;
                     ++i)
                {
                    out[i] =
                        m_pcmQueue.front();

                    m_pcmQueue.pop_front();
                }


                // ----------------------------------------
                // Sem dado suficiente: completa com
                // silencio (evita tocar lixo de memoria).
                // ----------------------------------------

                for (uint32_t i = samplesToCopy;
                     i < samplesNeeded;
                     ++i)
                {
                    out[i] =
                        0.0f;
                }


                DWORD flags =
                    (samplesToCopy == 0)
                    ? AUDCLNT_BUFFERFLAGS_SILENT
                    : 0;


                m_renderClient->ReleaseBuffer(
                    framesAvailable,
                    flags
                );
            }
        }


        Sleep(5);
    }


    m_audioClient->Stop();


cleanup:

    if (m_renderClient)
    {
        m_renderClient->Release();
        m_renderClient = nullptr;
    }

    if (m_audioClient)
    {
        m_audioClient->Release();
        m_audioClient = nullptr;
    }

    if (m_device)
    {
        m_device->Release();
        m_device = nullptr;
    }

    if (m_deviceEnumerator)
    {
        m_deviceEnumerator->Release();
        m_deviceEnumerator = nullptr;
    }

    if (m_comInitialized)
    {
        CoUninitialize();

        m_comInitialized = false;
    }


    m_running =
        false;
}


// ============================================================
// Stop
// ============================================================

void AudioPlayback::Stop()
{
    m_running =
        false;


    if (m_thread.joinable())
    {
        m_thread.join();
    }


    std::lock_guard<std::mutex> lock(
        m_mutex
    );


    m_pcmQueue.clear();
}
