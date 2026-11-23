#include "mic_tester.h"

#include <cmath>
#include <cstring>
#include <vector>

MicTester::~MicTester()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_quit = true;
    }

    m_cv.notify_one();

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    StopAll();
}


void MicTester::Set(
    bool running,
    const std::wstring& microphone,
    const std::wstring& speaker,
    bool loopback,
    bool noiseSuppression)
{
    m_noiseSuppression = noiseSuppression;

    Wanted wanted;

    wanted.running = running;

    if (running)
    {
        wanted.microphone = microphone;
        wanted.speaker = speaker;
        wanted.loopback = loopback;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (wanted == m_wanted)
            return;

        m_wanted = wanted;
        m_dirty = true;

        // Thread so na primeira vez que o teste e usado.
        if (!m_thread.joinable() && running)
        {
            m_thread = std::thread(&MicTester::Loop, this);
        }
    }

    m_cv.notify_one();
}


void MicTester::StopAll()
{
    // Microfone primeiro (ele escreve na saida).
    if (m_capture)
    {
        m_capture->Stop();
        m_capture.reset();
    }

    m_capturing = false;

    m_loopback = false;

    if (m_playback)
    {
        m_playback->Stop();
        m_playback.reset();
    }

    m_level = 0.0f;
    m_voiceActivity = -1.0f;
}


void MicTester::Loop()
{
    for (;;)
    {
        Wanted wanted;

        {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_cv.wait(lock, [this] { return m_dirty || m_quit; });

            if (m_quit)
                return;

            m_dirty = false;

            wanted = m_wanted;
        }

        if (wanted == m_applied)
            continue;

        StopAll();

        if (wanted.running)
        {
            if (wanted.loopback)
            {
                m_playback = std::make_unique<AudioPlayback>();

                m_playback->SetLowLatency(true);
                m_playback->SetDevice(wanted.speaker);

                if (!m_playback->Start())
                {
                    m_playback.reset();
                }
            }

            if (!m_denoiser)
            {
                m_denoiser = std::make_unique<NoiseSuppressor>();
            }

            m_loopback = m_playback != nullptr;

            m_capture = std::make_unique<VoiceCapture>();

            m_capture->SetDevice(wanted.microphone);
            m_capture->SetSpeaker(wanted.speaker);

            m_capture->Start(
                [this](const float* samples, size_t count)
                {
                    OnFrame(samples, count);
                });

            m_capturing = true;
        }

        m_applied = wanted;
    }
}


void MicTester::OnFrame(
    const float* samples,
    size_t count)
{
    if (count != NoiseSuppressor::FRAME_SAMPLES)
        return;

    float clean[NoiseSuppressor::FRAME_SAMPLES];

    std::memcpy(clean, samples, sizeof(clean));

    if (m_noiseSuppression && m_denoiser && m_denoiser->IsValid())
    {
        m_voiceActivity = m_denoiser->Process(clean);
    }
    else
    {
        m_voiceActivity = -1.0f;
    }

    double energy = 0.0;

    for (float value : clean)
    {
        energy += static_cast<double>(value) * value;
    }

    float rms = static_cast<float>(std::sqrt(energy / count));

    m_level = rms > m_level * 0.85f ? rms : m_level * 0.85f;

    // Ouvir a propria voz: 16 kHz mono -> 48 kHz estereo
    // (interpolacao linear basta pra conferir o som).
    if (m_loopback && m_playback)
    {
        std::vector<float> out(count * 3 * 2);

        float previous = m_lastSample;

        for (size_t i = 0; i < count; ++i)
        {
            for (size_t k = 0; k < 3; ++k)
            {
                float t = static_cast<float>(k + 1) / 3.0f;

                float value = previous + (clean[i] - previous) * t;

                out[(i * 3 + k) * 2] = value;
                out[(i * 3 + k) * 2 + 1] = value;
            }

            previous = clean[i];
        }

        m_lastSample = previous;

        m_playback->PushSamples(out);
    }
}
