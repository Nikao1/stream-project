#include "audio_capture.h"
#include "audio_protocol.h"

#include <mmdeviceapi.h>
#include <audioclient.h>

#include <iostream>

#pragma comment(lib, "ole32.lib")


// ============================================================
// Constructor / Destructor
// ============================================================

AudioCapture::AudioCapture()
    : m_deviceEnumerator(nullptr),
      m_device(nullptr),
      m_audioClient(nullptr),
      m_captureClient(nullptr),
      m_mixFormat(nullptr),
      m_running(false),
      m_comInitialized(false)
{
}


AudioCapture::~AudioCapture()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool AudioCapture::Start()
{
    if (m_running)
    {
        return true;
    }


    m_running =
        true;


    m_thread =
        std::thread(
            &AudioCapture::CaptureThread,
            this
        );


    return true;
}


// ============================================================
// IsRunning
// ============================================================

bool AudioCapture::IsRunning() const
{
    return m_running;
}


// ============================================================
// GetFrame
// ============================================================

bool AudioCapture::GetFrame(
    std::vector<float>& outSamples)
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );


    if (m_frameQueue.empty())
    {
        return false;
    }


    outSamples =
        std::move(
            m_frameQueue.front()
        );


    m_frameQueue.pop_front();


    return true;
}


// ============================================================
// CaptureThread
// ============================================================
//
// Roda inteiramente nesta thread: inicialização COM, setup do
// WASAPI, loop de captura, e limpeza no final. COM é "por
// thread", então tudo isso precisa acontecer na mesma thread.
// ============================================================

void AudioCapture::CaptureThread()
{
    // ============================================================
    // COM
    // ============================================================

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
            << "AudioCapture: CoInitializeEx falhou. HRESULT: "
            << hr
            << "\n";

        m_running =
            false;

        return;
    }


    // ============================================================
    // Device enumerator
    // ============================================================

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
            << "AudioCapture: falha ao criar "
            << "IMMDeviceEnumerator. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    // ============================================================
    // Dispositivo de saída padrão (o que vai em loopback)
    // ============================================================

    hr =
        m_deviceEnumerator->GetDefaultAudioEndpoint(
            eRender,
            eConsole,
            &m_device
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao obter dispositivo "
            << "de audio padrao. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    // ============================================================
    // IAudioClient
    // ============================================================

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
            << "AudioCapture: falha ao ativar IAudioClient. "
            << "HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    hr =
        m_audioClient->GetMixFormat(
            &m_mixFormat
        );


    if (FAILED(hr) ||
        !m_mixFormat)
    {
        std::cout
            << "AudioCapture: falha ao obter mix format. "
            << "HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    std::cout
        << "AudioCapture: dispositivo padrao - "
        << m_mixFormat->nSamplesPerSec
        << " Hz, "
        << m_mixFormat->nChannels
        << " canal(is), "
        << m_mixFormat->wBitsPerSample
        << " bits.\n";


    if (m_mixFormat->nSamplesPerSec !=
        AUDIO_SAMPLE_RATE)
    {
        std::cout
            << "AudioCapture: AVISO - dispositivo nao esta em "
            << AUDIO_SAMPLE_RATE
            << " Hz. Audio sera enviado sem resample "
            << "(pode soar errado).\n";
    }


    // ============================================================
    // Inicializar em modo loopback
    //
    // Buffer de 200ms - folga suficiente pro polling, sem
    // acumular atraso perceptível.
    // ============================================================

    {
        constexpr REFERENCE_TIME bufferDuration =
            200 *
            10000; // 200ms em unidades de 100ns


        hr =
            m_audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK,
                bufferDuration,
                0,
                m_mixFormat,
                nullptr
            );
    }


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao inicializar "
            << "IAudioClient (loopback). HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    hr =
        m_audioClient->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(
                &m_captureClient
            )
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao obter "
            << "IAudioCaptureClient. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    hr =
        m_audioClient->Start();


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao iniciar captura. "
            << "HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    std::cout
        << "AudioCapture: captura de audio do sistema "
        << "iniciada (loopback).\n";


    // ============================================================
    // LOOP DE CAPTURA
    // ============================================================

    while (m_running)
    {
        UINT32 packetLength =
            0;


        hr =
            m_captureClient->GetNextPacketSize(
                &packetLength
            );


        if (FAILED(hr))
        {
            break;
        }


        while (packetLength != 0)
        {
            BYTE* data =
                nullptr;

            UINT32 numFrames =
                0;

            DWORD flags =
                0;


            hr =
                m_captureClient->GetBuffer(
                    &data,
                    &numFrames,
                    &flags,
                    nullptr,
                    nullptr
                );


            if (FAILED(hr))
            {
                break;
            }


            // ================================================
            // Converter para estereo float e acumular
            // ================================================

            {
                std::lock_guard<std::mutex> lock(
                    m_mutex
                );


                bool silent =
                    (flags &
                     AUDCLNT_BUFFERFLAGS_SILENT) != 0;


                const float* samples =
                    reinterpret_cast<const float*>(
                        data
                    );


                uint32_t srcChannels =
                    m_mixFormat->nChannels;


                for (UINT32 i = 0;
                     i < numFrames;
                     ++i)
                {
                    float left =
                        0.0f;

                    float right =
                        0.0f;


                    if (!silent &&
                        samples)
                    {
                        left =
                            samples[
                                i * srcChannels + 0];


                        right =
                            (srcChannels >= 2)
                            ? samples[
                                  i * srcChannels + 1]
                            : left;
                    }


                    m_pendingSamples.push_back(
                        left
                    );

                    m_pendingSamples.push_back(
                        right
                    );
                }


                // ------------------------------------------------
                // Extrair frames completos de AUDIO_FRAME_SAMPLES
                // ------------------------------------------------

                constexpr size_t samplesPerFrame =
                    AUDIO_FRAME_SAMPLES *
                    AUDIO_CHANNELS;


                while (m_pendingSamples.size() >=
                       samplesPerFrame)
                {
                    std::vector<float> frame(
                        m_pendingSamples.begin(),
                        m_pendingSamples.begin() +
                            samplesPerFrame
                    );


                    m_pendingSamples.erase(
                        m_pendingSamples.begin(),
                        m_pendingSamples.begin() +
                            samplesPerFrame
                    );


                    m_frameQueue.push_back(
                        std::move(frame)
                    );


                    // --------------------------------------------
                    // Limite de seguranca - nao deixa acumular
                    // atraso se o envio nao acompanhar.
                    // --------------------------------------------

                    constexpr size_t MAX_QUEUED_AUDIO_FRAMES =
                        10;


                    while (m_frameQueue.size() >
                           MAX_QUEUED_AUDIO_FRAMES)
                    {
                        m_frameQueue.pop_front();
                    }
                }
            }


            m_captureClient->ReleaseBuffer(
                numFrames
            );


            hr =
                m_captureClient->GetNextPacketSize(
                    &packetLength
                );


            if (FAILED(hr))
            {
                break;
            }
        }


        Sleep(5);
    }


    m_audioClient->Stop();


cleanup:

    if (m_captureClient)
    {
        m_captureClient->Release();
        m_captureClient = nullptr;
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

    if (m_mixFormat)
    {
        CoTaskMemFree(
            m_mixFormat
        );

        m_mixFormat = nullptr;
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

void AudioCapture::Stop()
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


    m_pendingSamples.clear();

    m_frameQueue.clear();
}