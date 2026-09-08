#include "audio_encoder.h"
#include "audio_protocol.h"

#include "opus.h"

#include <iostream>


// ============================================================
// Constructor / Destructor
// ============================================================

AudioEncoder::AudioEncoder()
    : m_encoder(nullptr)
{
}


AudioEncoder::~AudioEncoder()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool AudioEncoder::Start()
{
    int error =
        0;


    m_encoder =
        opus_encoder_create(
            AUDIO_SAMPLE_RATE,
            AUDIO_CHANNELS,
            OPUS_APPLICATION_AUDIO,
            &error
        );


    if (error != OPUS_OK ||
        !m_encoder)
    {
        std::cout
            << "AudioEncoder: opus_encoder_create falhou. "
            << "Erro: "
            << opus_strerror(error)
            << "\n";

        m_encoder =
            nullptr;

        return false;
    }


    // --------------------------------------------------------
    // Bitrate alvo
    // --------------------------------------------------------

    opus_encoder_ctl(
        m_encoder,
        OPUS_SET_BITRATE(
            AUDIO_BITRATE
        )
    );


    // --------------------------------------------------------
    // Prioriza baixa latencia sobre qualidade máxima
    // --------------------------------------------------------

    opus_encoder_ctl(
        m_encoder,
        OPUS_SET_COMPLEXITY(5)
    );


    opus_encoder_ctl(
        m_encoder,
        OPUS_SET_SIGNAL(
            OPUS_SIGNAL_MUSIC
        )
    );


    std::cout
        << "AudioEncoder: Opus iniciado ("
        << AUDIO_SAMPLE_RATE
        << " Hz, "
        << AUDIO_CHANNELS
        << " canais, "
        << AUDIO_BITRATE / 1000
        << " kbps).\n";


    return true;
}


// ============================================================
// Encode
// ============================================================

bool AudioEncoder::Encode(
    const float* pcm,
    std::vector<unsigned char>& outPacket)
{
    if (!m_encoder ||
        !pcm)
    {
        return false;
    }


    // --------------------------------------------------------
    // Opus recomenda um buffer de saida de ate 4000 bytes por
    // frame como limite seguro.
    // --------------------------------------------------------

    outPacket.resize(
        4000
    );


    int result =
        opus_encode_float(
            m_encoder,

            pcm,

            static_cast<int>(
                AUDIO_FRAME_SAMPLES
            ),

            outPacket.data(),

            static_cast<opus_int32>(
                outPacket.size()
            )
        );


    if (result < 0)
    {
        std::cout
            << "AudioEncoder: opus_encode_float falhou. "
            << "Erro: "
            << opus_strerror(result)
            << "\n";

        outPacket.clear();

        return false;
    }


    outPacket.resize(
        static_cast<size_t>(
            result
        )
    );


    return true;
}


// ============================================================
// Stop
// ============================================================

void AudioEncoder::Stop()
{
    if (m_encoder)
    {
        opus_encoder_destroy(
            m_encoder
        );

        m_encoder =
            nullptr;
    }
}
