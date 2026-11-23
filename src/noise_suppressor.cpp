#include "noise_suppressor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "rnnoise.h"

namespace
{

constexpr size_t RATIO = 3;                 // 16 kHz -> 48 kHz
constexpr size_t FRAME_48K = 960;           // 20 ms a 48 kHz
constexpr size_t RNN_FRAME = 480;           // 10 ms (RNNoise)

// Passa-baixa (sinc com janela de Blackman) pras conversoes:
// corta em ~7,2 kHz (abaixo dos 8 kHz que 16 kHz comporta).
struct LowPass
{
    float taps[63];

    LowPass()
    {
        const int count = 63;
        const double center = (count - 1) / 2.0;
        const double cutoff = 7200.0 / 48000.0;
        const double pi = 3.141592653589793;

        double sum = 0.0;

        for (int i = 0; i < count; ++i)
        {
            double x = i - center;

            double sinc =
                x == 0.0
                ? 2.0 * cutoff
                : std::sin(2.0 * pi * cutoff * x) / (pi * x);

            double window =
                0.42 -
                0.5 * std::cos(2.0 * pi * i / (count - 1)) +
                0.08 * std::cos(4.0 * pi * i / (count - 1));

            taps[i] = static_cast<float>(sinc * window);

            sum += taps[i];
        }

        // Ganho 1 em baixas frequencias.
        for (float& tap : taps)
        {
            tap = static_cast<float>(tap / sum);
        }
    }
};

const LowPass& Filter()
{
    static const LowPass filter;

    return filter;
}

} // namespace


NoiseSuppressor::NoiseSuppressor()
{
    m_state = rnnoise_create(nullptr);
}


NoiseSuppressor::~NoiseSuppressor()
{
    if (m_state)
    {
        rnnoise_destroy(m_state);
    }
}


void NoiseSuppressor::Upsample(
    const float* in,
    float* out)
{
    const float* h = Filter().taps;

    constexpr size_t HISTORY = TAPS - 1;

    // historico + sinal com zeros entre as amostras (x3 de ganho
    // pra compensar os zeros).
    float extended[HISTORY + FRAME_48K];

    std::memcpy(extended, m_upHistory, sizeof(float) * HISTORY);

    for (size_t i = 0; i < FRAME_48K; ++i)
    {
        extended[HISTORY + i] =
            i % RATIO == 0 ? in[i / RATIO] * static_cast<float>(RATIO) : 0.0f;
    }

    for (size_t m = 0; m < FRAME_48K; ++m)
    {
        float acc = 0.0f;

        const float* x = extended + HISTORY + m;

        for (size_t k = 0; k < TAPS; ++k)
        {
            acc += h[k] * x[-static_cast<std::ptrdiff_t>(k)];
        }

        out[m] = acc;
    }

    std::memcpy(
        m_upHistory, extended + FRAME_48K, sizeof(float) * HISTORY);
}


void NoiseSuppressor::Downsample(
    const float* in,
    float* out)
{
    const float* h = Filter().taps;

    constexpr size_t HISTORY = TAPS - 1;

    float extended[HISTORY + FRAME_48K];

    std::memcpy(extended, m_downHistory, sizeof(float) * HISTORY);
    std::memcpy(extended + HISTORY, in, sizeof(float) * FRAME_48K);

    for (size_t n = 0; n < FRAME_SAMPLES; ++n)
    {
        float acc = 0.0f;

        const float* x = extended + HISTORY + n * RATIO;

        for (size_t k = 0; k < TAPS; ++k)
        {
            acc += h[k] * x[-static_cast<std::ptrdiff_t>(k)];
        }

        out[n] = acc;
    }

    std::memcpy(
        m_downHistory, extended + FRAME_48K, sizeof(float) * HISTORY);
}


float NoiseSuppressor::Process(
    float* samples)
{
    if (!m_state)
        return -1.0f;

    float high[FRAME_48K];

    Upsample(samples, high);

    float vad = 0.0f;

    if (!m_bypass)
    {
        // O RNNoise trabalha na escala de 16 bits.
        for (float& value : high)
        {
            value *= 32768.0f;
        }

        for (size_t offset = 0; offset < FRAME_48K; offset += RNN_FRAME)
        {
            float clean[RNN_FRAME];

            float probability =
                rnnoise_process_frame(
                    m_state, clean, high + offset);

            vad = (std::max)(vad, probability);

            std::memcpy(high + offset, clean, sizeof(clean));
        }

        for (float& value : high)
        {
            value /= 32768.0f;
        }
    }

    Downsample(high, samples);

    return vad;
}
