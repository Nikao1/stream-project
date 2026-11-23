#include "voice_mixer.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{

constexpr size_t SAMPLES_PER_MS =
    VoiceMixer::SAMPLE_RATE * VoiceMixer::CHANNELS / 1000;

// Comeca a tocar com isso guardado (absorve a variacao da rede).
constexpr size_t START_SAMPLES = 60 * SAMPLES_PER_MS;

// Passou disso: descarta o mais antigo ate sobrar KEEP.
constexpr size_t MAX_SAMPLES = 200 * SAMPLES_PER_MS;
constexpr size_t KEEP_SAMPLES = 80 * SAMPLES_PER_MS;

} // namespace


VoiceMixer::~VoiceMixer()
{
    Stop();
}


void VoiceMixer::Start(
    OutputFn output)
{
    if (m_running.exchange(true))
        return;

    m_output = std::move(output);

    m_thread =
        std::thread(&VoiceMixer::Loop, this);
}


void VoiceMixer::Stop()
{
    m_running = false;

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    m_sources.clear();
}


void VoiceMixer::Push(
    const std::string& id,
    const float* samples,
    size_t frames)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto found = m_sources.find(id);

    if (found == m_sources.end())
    {
        Source source;

        auto gain = m_gains.find(id);

        if (gain != m_gains.end())
        {
            source.gain = gain->second;
        }

        found =
            m_sources.emplace(id, std::move(source)).first;
    }

    Source& source = found->second;

    source.queue.insert(
        source.queue.end(),
        samples,
        samples + frames * CHANNELS);

    if (source.queue.size() > MAX_SAMPLES)
    {
        source.queue.erase(
            source.queue.begin(),
            source.queue.begin() +
                (source.queue.size() - KEEP_SAMPLES));
    }
}


void VoiceMixer::Remove(
    const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_sources.erase(id);
}


void VoiceMixer::SetGain(
    const std::string& id,
    float gain)
{
    gain = (std::max)(0.0f, (std::min)(2.0f, gain));

    std::lock_guard<std::mutex> lock(m_mutex);

    m_gains[id] = gain;

    auto found = m_sources.find(id);

    if (found != m_sources.end())
    {
        found->second.gain = gain;
    }
}


float VoiceMixer::GetLevel(
    const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto found = m_sources.find(id);

    return
        found == m_sources.end()
        ? 0.0f
        : found->second.level;
}


void VoiceMixer::MixBlock(
    std::vector<float>& out)
{
    const size_t blockSamples =
        static_cast<size_t>(BLOCK_FRAMES) * CHANNELS;

    out.assign(blockSamples, 0.0f);

    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& entry : m_sources)
    {
        Source& source = entry.second;

        if (!source.playing)
        {
            if (source.queue.size() >= START_SAMPLES)
            {
                source.playing = true;
            }
            else
            {
                // Caindo aos poucos enquanto espera.
                source.level *= 0.85f;

                continue;
            }
        }

        size_t take =
            (std::min)(blockSamples, source.queue.size());

        double energy = 0.0;

        for (size_t i = 0; i < take; ++i)
        {
            float sample = source.queue[i];

            energy += static_cast<double>(sample) * sample;

            out[i] += sample * source.gain;
        }

        source.queue.erase(
            source.queue.begin(),
            source.queue.begin() + take);

        // Fila acabou no meio: espera encher de novo.
        if (take < blockSamples)
        {
            source.playing = false;
        }

        float rms =
            take > 0
            ? static_cast<float>(std::sqrt(energy / take))
            : 0.0f;

        // Sobe na hora, desce devagar. Sem o volume escolhido
        // por quem ouve: o indicador de "falando" vale mesmo com
        // a pessoa silenciada.
        source.level =
            (std::max)(rms, source.level * 0.85f);
    }

    // Evita estourar quando varias pessoas falam alto juntas.
    for (float& sample : out)
    {
        sample = (std::max)(-1.0f, (std::min)(1.0f, sample));
    }
}


void VoiceMixer::Loop()
{
    using Clock = std::chrono::steady_clock;

    const auto block = std::chrono::milliseconds(10);

    auto next = Clock::now();

    std::vector<float> out;

    while (m_running)
    {
        MixBlock(out);

        if (m_output)
        {
            m_output(out);
        }

        next += block;

        auto now = Clock::now();

        // Muito atrasado (PC travou um pouco): recomeca o
        // relogio em vez de tentar compensar de uma vez.
        if (now - next > std::chrono::milliseconds(100))
        {
            next = now;
        }

        std::this_thread::sleep_until(next);
    }
}
