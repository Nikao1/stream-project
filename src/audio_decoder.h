#pragma once

#include <cstdint>
#include <vector>

struct OpusDecoder;

// ============================================================
// AudioDecoder
//
// Wrapper fino sobre libopus. Recebe um pacote Opus e devolve
// PCM float32 estéreo (AUDIO_FRAME_SAMPLES amostras/canal).
// ============================================================

class AudioDecoder
{
public:
    AudioDecoder();
    ~AudioDecoder();

    bool Start();

    void Stop();

    // outPcm recebe AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS
    // floats intercalados (estéreo).
    bool Decode(
        const unsigned char* opusData,
        uint32_t opusSize,
        std::vector<float>& outPcm
    );

    // Chamar quando um pacote é perdido (sequence pulou), pra
    // o Opus fazer "packet loss concealment" (PLC) - gera um
    // frame de substituição plausível em vez de silêncio seco
    // ou nada.
    bool DecodeLost(
        std::vector<float>& outPcm
    );

private:
    OpusDecoder* m_decoder;
};
