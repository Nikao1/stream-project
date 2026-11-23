#pragma once

#include <cstddef>

// ============================================================
// REDUCAO DE RUIDO DO MICROFONE (RNNoise)
// ============================================================
//
// RNNoise (Xiph.Org, licenca BSD): uma rede neural pequena que
// tira do microfone o que nao e voz - teclado, mouse,
// ventoinha, ar-condicionado, barulho de rua - e diz a
// probabilidade de ter voz em cada trecho (usada no indicador
// de "falando"). Roda inteira no computador da pessoa.
//
// Ele trabalha em 48 kHz (blocos de 10 ms); a voz do programa
// e 16 kHz. Entao: 16 kHz -> 48 kHz -> RNNoise -> 16 kHz, com
// filtros passa-baixa nas conversoes (~1 ms de atraso, mais os
// ~10 ms do proprio RNNoise).
// ============================================================

struct DenoiseState;

class NoiseSuppressor
{
public:
    // Amostras por chamada: 20 ms a 16 kHz.
    static constexpr size_t FRAME_SAMPLES = 320;

    NoiseSuppressor();
    ~NoiseSuppressor();

    NoiseSuppressor(const NoiseSuppressor&) = delete;
    NoiseSuppressor& operator=(const NoiseSuppressor&) = delete;

    bool IsValid() const
    {
        return m_state != nullptr;
    }

    // Limpa FRAME_SAMPLES amostras (float -1..1, 16 kHz mono)
    // no lugar. Devolve a probabilidade de voz (0..1). Depois da
    // rede, uma "porta" suave abaixa ~20 dB o ruido que sobra
    // entre as palavras (segura 200 ms depois da fala, pra nao
    // cortar o fim delas).
    float Process(
        float* samples
    );

    // Sem a rede (so as conversoes 16k->48k->16k). Pra teste.
    void SetBypassNetwork(
        bool bypass)
    {
        m_bypass = bypass;
    }

private:
    void Upsample(
        const float* in,
        float* out
    );

    void Downsample(
        const float* in,
        float* out
    );

    DenoiseState* m_state = nullptr;

    bool m_bypass = false;

    // Porta pos-rede: entre as palavras abaixa o que sobrou.
    float m_gateGain = 1.0f;
    int m_gateHold = 0;

    // Historicos dos filtros (fim do bloco anterior).
    static constexpr size_t TAPS = 63;

    float m_upHistory[TAPS]{};
    float m_downHistory[TAPS]{};
};
