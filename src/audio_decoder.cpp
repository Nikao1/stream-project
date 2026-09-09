#include "audio_decoder.h"
#include "audio_protocol.h"

#include "opus.h"

#include <iostream>


// ============================================================
// Constructor / Destructor
// ============================================================

AudioDecoder::AudioDecoder()
    : m_decoder(nullptr)
{
}


AudioDecoder::~AudioDecoder()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool AudioDecoder::Start()
{
    int error =
        0;


    m_decoder =
        opus_decoder_create(
            AUDIO_SAMPLE_RATE,
            AUDIO_CHANNELS,
            &error
        );


    if (error != OPUS_OK ||
        !m_decoder)
    {
        std::cout
            << "AudioDecoder: opus_decoder_create falhou. "
            << "Erro: "
            << opus_strerror(error)
            << "\n";

        m_decoder =
            nullptr;

        return false;
    }


    std::cout
        << "AudioDecoder: Opus iniciado ("
        << AUDIO_SAMPLE_RATE
        << " Hz, "
        << AUDIO_CHANNELS
        << " canais).\n";


    return true;
}


// ============================================================
// Decode
// ============================================================

bool AudioDecoder::Decode(
    const unsigned char* opusData,
    uint32_t opusSize,
    std::vector<float>& outPcm)
{
    if (!m_decoder ||
        !opusData ||
        opusSize == 0)
    {
        return false;
    }


    outPcm.resize(
        AUDIO_FRAME_SAMPLES *
        AUDIO_CHANNELS
    );


    int samplesDecoded =
        opus_decode_float(
            m_decoder,

            opusData,

            static_cast<opus_int32>(
                opusSize
            ),

            outPcm.data(),

            static_cast<int>(
                AUDIO_FRAME_SAMPLES
            ),

            0 // sem FEC embutido
        );


    if (samplesDecoded < 0)
    {
        std::cout
            << "AudioDecoder: opus_decode_float falhou. "
            << "Erro: "
            << opus_strerror(samplesDecoded)
            << "\n";

        outPcm.clear();

        return false;
    }


    outPcm.resize(
        static_cast<size_t>(samplesDecoded) *
        AUDIO_CHANNELS
    );


    return true;
}


// ============================================================
// DecodeLost
// ============================================================
//
// Pacote perdido: passa nullptr/0 pro Opus, que usa PLC
// (packet loss concealment) pra gerar um frame plausível em
// vez de silêncio seco ou deixar um buraco no áudio.
// ============================================================

bool AudioDecoder::DecodeLost(
    std::vector<float>& outPcm)
{
    if (!m_decoder)
    {
        return false;
    }


    outPcm.resize(
        AUDIO_FRAME_SAMPLES *
        AUDIO_CHANNELS
    );


    int samplesDecoded =
        opus_decode_float(
            m_decoder,

            nullptr,

            0,

            outPcm.data(),

            static_cast<int>(
                AUDIO_FRAME_SAMPLES
            ),

            0
        );


    if (samplesDecoded < 0)
    {
        outPcm.clear();

        return false;
    }


    outPcm.resize(
        static_cast<size_t>(samplesDecoded) *
        AUDIO_CHANNELS
    );


    return true;
}


// ============================================================
// Stop
// ============================================================

void AudioDecoder::Stop()
{
    if (m_decoder)
    {
        opus_decoder_destroy(
            m_decoder
        );

        m_decoder =
            nullptr;
    }
}
