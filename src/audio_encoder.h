#pragma once

#include <cstdint>
#include <vector>

struct OpusEncoder;

// ============================================================
// AudioEncoder
//
// Wrapper fino sobre libopus. Recebe um bloco de PCM float32
// estéreo (exatamente AUDIO_FRAME_SAMPLES amostras por canal)
// e devolve um pacote Opus codificado.
// ============================================================

class AudioEncoder
{
public:
    AudioEncoder();
    ~AudioEncoder();

    bool Start();

    void Stop();

    // pcm: AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS floats
    // intercalados (estéreo). outPacket recebe o Opus
    // codificado.
    bool Encode(
        const float* pcm,
        std::vector<unsigned char>& outPacket
    );

private:
    OpusEncoder* m_encoder;
};
