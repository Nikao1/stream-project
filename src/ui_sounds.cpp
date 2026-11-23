#include "ui_sounds.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#ifdef _WIN32
#include "audio_playback.h"
#endif

namespace
{

constexpr int RATE = 48000;

constexpr double TWO_PI = 6.283185307179586;

// Notas (La maior: A5, C#6, E6 - agradavel e nao estridente).
constexpr double NOTE_A5 = 880.0;
constexpr double NOTE_CS6 = 1108.73;
constexpr double NOTE_E6 = 1318.51;

// Volume base: bem abaixo da voz das pessoas.
constexpr float BASE_GAIN = 0.22f;


// Soma uma nota (seno + um pouco do 2o harmonico, pra soar
// "redondo") em out, comecando em startSeconds.
//   attack: subida (s); decay: constante de queda exponencial
void AddNote(
    std::vector<float>& out,
    double startSeconds,
    double frequency,
    double length,
    double attack,
    double decay,
    float gain,
    double harmonic = 0.18)
{
    size_t start = static_cast<size_t>(startSeconds * RATE);
    size_t count = static_cast<size_t>(length * RATE);

    if (out.size() < (start + count) * 2)
    {
        out.resize((start + count) * 2, 0.0f);
    }

    for (size_t i = 0; i < count; ++i)
    {
        double t = static_cast<double>(i) / RATE;

        double envelope =
            (t < attack ? t / attack : 1.0) *
            std::exp(-(t - (t < attack ? t : attack)) / decay);

        // Fade final de 8 ms: nunca termina com estalo.
        double remaining = length - t;

        if (remaining < 0.008)
        {
            envelope *= remaining / 0.008;
        }

        double value =
            std::sin(TWO_PI * frequency * t) +
            harmonic * std::sin(TWO_PI * frequency * 2.0 * t);

        float sample =
            static_cast<float>(value * envelope / (1.0 + harmonic)) * gain;

        out[(start + i) * 2] += sample;
        out[(start + i) * 2 + 1] += sample;
    }
}


// "Ding": parciais inarmonicas de sino, queda longa.
void AddBell(
    std::vector<float>& out,
    double frequency,
    double length,
    float gain)
{
    AddNote(out, 0.0, frequency, length, 0.004, 0.12, gain, 0.0);
    AddNote(out, 0.0, frequency * 2.756, length * 0.6, 0.002, 0.05, gain * 0.25f, 0.0);
    AddNote(out, 0.0, frequency * 5.404, length * 0.3, 0.001, 0.025, gain * 0.1f, 0.0);
}


// "Pop": frequencia caindo rapido.
void AddPop(
    std::vector<float>& out,
    float gain)
{
    double length = 0.08;

    size_t count = static_cast<size_t>(length * RATE);

    out.resize(count * 2, 0.0f);

    double phase = 0.0;

    for (size_t i = 0; i < count; ++i)
    {
        double t = static_cast<double>(i) / RATE;

        double frequency = 1400.0 * std::exp(-t / 0.03) + 500.0;

        phase += TWO_PI * frequency / RATE;

        double envelope =
            (t < 0.003 ? t / 0.003 : 1.0) * std::exp(-t / 0.025);

        // Fade final: sem estalo.
        double remaining = length - t;

        if (remaining < 0.008)
        {
            envelope *= remaining / 0.008;
        }

        float sample =
            static_cast<float>(std::sin(phase) * envelope) * gain;

        out[i * 2] += sample;
        out[i * 2 + 1] += sample;
    }
}

} // namespace


std::vector<float> UiSoundSamples(
    UiSound sound)
{
    std::vector<float> out;

    const float g = BASE_GAIN;

    switch (sound)
    {
    case UiSound::MicOn:
        AddNote(out, 0.000, NOTE_A5, 0.11, 0.005, 0.07, g);
        AddNote(out, 0.065, NOTE_E6, 0.13, 0.005, 0.08, g);
        break;

    case UiSound::MicOff:
        AddNote(out, 0.000, NOTE_E6, 0.11, 0.005, 0.07, g);
        AddNote(out, 0.065, NOTE_A5, 0.13, 0.005, 0.08, g);
        break;

    case UiSound::VoiceJoin:
        AddNote(out, 0.000, NOTE_A5, 0.12, 0.006, 0.08, g * 0.85f);
        AddNote(out, 0.070, NOTE_CS6, 0.12, 0.006, 0.08, g * 0.85f);
        AddNote(out, 0.140, NOTE_E6, 0.16, 0.006, 0.10, g * 0.85f);
        break;

    case UiSound::VoiceLeave:
        AddNote(out, 0.000, NOTE_E6, 0.12, 0.006, 0.08, g * 0.85f);
        AddNote(out, 0.070, NOTE_CS6, 0.12, 0.006, 0.08, g * 0.85f);
        AddNote(out, 0.140, NOTE_A5, 0.16, 0.006, 0.10, g * 0.85f);
        break;

    case UiSound::StreamStart:
        AddBell(out, 1568.0, 0.35, g);
        break;

    case UiSound::ChatMessage:
        AddPop(out, g * 0.8f);
        break;
    }

    return out;
}


// ============================================================
// Player
// ============================================================

#ifdef _WIN32

namespace
{

// Blocos de 10 ms.
constexpr size_t BLOCK_FRAMES = RATE / 100;

} // namespace


UiSoundPlayer::UiSoundPlayer() = default;


UiSoundPlayer::~UiSoundPlayer()
{
    Stop();
}


void UiSoundPlayer::Start(
    const std::wstring& deviceId)
{
    if (m_starter.joinable())
    {
        m_starter.join();
    }

    std::lock_guard<std::mutex> lock(m_lifecycle);

    StartLocked(deviceId);
}


void UiSoundPlayer::StartAsync(
    const std::wstring& deviceId)
{
    if (m_starter.joinable())
    {
        m_starter.join();
    }

    m_starter =
        std::thread(
            [this, deviceId]()
            {
                std::lock_guard<std::mutex> lock(m_lifecycle);

                StartLocked(deviceId);
            });
}


void UiSoundPlayer::Stop()
{
    if (m_starter.joinable())
    {
        m_starter.join();
    }

    std::lock_guard<std::mutex> lock(m_lifecycle);

    StopLocked();
}


void UiSoundPlayer::StartLocked(
    const std::wstring& deviceId)
{
    StopLocked();

    m_playback = new AudioPlayback();

    m_playback->SetLowLatency(true);
    m_playback->SetDevice(deviceId);

    if (!m_playback->Start())
    {
        delete m_playback;

        m_playback = nullptr;

        return;
    }

    m_running = true;

    m_thread = std::thread(&UiSoundPlayer::Loop, this);
}


void UiSoundPlayer::StopLocked()
{
    m_running = false;

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    if (m_playback)
    {
        m_playback->Stop();

        delete m_playback;

        m_playback = nullptr;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    m_pending.clear();
    m_position = 0;
}


void UiSoundPlayer::Play(
    UiSound sound)
{
    if (!m_enabled || !m_running)
        return;

    std::vector<float> samples = UiSoundSamples(sound);

    float volume = m_volume;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Descarta o que ja tocou e mistura o novo a partir de agora.
    m_pending.erase(
        m_pending.begin(),
        m_pending.begin() + static_cast<std::ptrdiff_t>(m_position));

    m_position = 0;

    if (m_pending.size() < samples.size())
    {
        m_pending.resize(samples.size(), 0.0f);
    }

    for (size_t i = 0; i < samples.size(); ++i)
    {
        m_pending[i] += samples[i] * volume;
    }
}


void UiSoundPlayer::Loop()
{
    std::vector<float> block(BLOCK_FRAMES * 2);

    // Folga inicial: 30 ms de silencio (a placa nunca fica sem
    // nada pra tocar no meio de um som).
    std::fill(block.begin(), block.end(), 0.0f);

    for (int i = 0; i < 3; ++i)
    {
        m_playback->PushSamples(block);
    }

    auto next = std::chrono::steady_clock::now();

    while (m_running)
    {
        next += std::chrono::milliseconds(10);

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            for (size_t i = 0; i < block.size(); ++i)
            {
                float value =
                    m_position < m_pending.size()
                    ? m_pending[m_position++]
                    : 0.0f;

                block[i] = (std::max)(-1.0f, (std::min)(1.0f, value));
            }

            if (m_position >= m_pending.size())
            {
                m_pending.clear();
                m_position = 0;
            }
        }

        m_playback->PushSamples(block);

        std::this_thread::sleep_until(next);

        // Atrasou muito (PC travou): recomeca o relogio.
        if (std::chrono::steady_clock::now() - next >
            std::chrono::milliseconds(50))
        {
            next = std::chrono::steady_clock::now();
        }
    }
}

#endif
