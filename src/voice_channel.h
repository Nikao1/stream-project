#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio_playback.h"
#include "signaling_client.h"
#include "stream_crypto.h"
#include "udp_audio_server.h"
#include "voice_capture.h"
#include "voice_mixer.h"

struct OpusEncoder;

// ============================================================
// CANAL DE VOZ DA SALA
// ============================================================
//
// Cada pessoa na voz:
//
//   - abre um canal proprio (uma "sessao" so de audio, como uma
//     transmissao) e manda a voz dela pra quem se conectar;
//   - se conecta ao canal de cada outra pessoa da sala que
//     esteja na voz (VoicePeer) e ouve.
//
// Com 9 pessoas na sala, cada uma recebe 8 conexoes - o mesmo
// limite de uma transmissao. A voz vai direto de um computador
// pro outro (cifrada), nunca pelo servidor.
//
// Qualidade de chamada: Opus 16 kHz mono, ~24 kbps, pacotes de
// 20 ms. Cerca de 0,2 Mbps de upload com a sala cheia.
// ============================================================

class VoicePeer;

class VoiceChannel
{
public:
    VoiceChannel();
    ~VoiceChannel();

    // Entra na voz (numa thread: abre microfone, portas, sessao
    // no servidor). Acompanhar com IsJoining()/IsJoined().
    void Join();

    // Sai da voz (fecha tudo).
    void Leave();

    bool IsJoining() const { return m_joining; }

    bool IsJoined() const { return m_joined; }

    // Nao deu pra entrar (ou o microfone falhou): explicacao.
    std::string ErrorText();

    // A cada frame, com o estado da sala: conecta em quem entrou
    // na voz e desconecta de quem saiu.
    void Sync(
        const RoomState& state,
        const std::string& myMemberId
    );

    // Sessao da voz desta pessoa, pra publicar na sala. false =
    // ainda nao (ou fora da voz).
    bool GetPublishInfo(
        std::string& outSessionCode,
        std::string& outSessionToken
    );


    // --------------------------------------------------------
    // Microfone
    // --------------------------------------------------------

    void SetMuted(
        bool muted
    );

    bool IsMuted() const { return m_muted; }

    // Tecla de "segurar para falar" (codigo de tecla virtual do
    // Windows). 0 = microfone aberto.
    void SetPushToTalkKey(
        int virtualKey
    );

    // Microfone (id do Windows, ver VoiceCapture::
    // ListMicrophones; vazio = padrao). Na voz: troca na hora.
    void SetMicrophone(
        const std::wstring& deviceId
    );

    // A voz desta pessoa esta saindo agora (nao mudo e, com
    // segurar-para-falar, tecla apertada).
    bool IsTransmitting() const { return m_transmitting; }

    float MicLevel() const;

    bool EchoCancellation() const;


    // --------------------------------------------------------
    // Outras pessoas
    // --------------------------------------------------------

    // 0.0 a 2.0 (1.0 = normal), por id de membro da sala.
    void SetPeerVolume(
        const std::string& memberId,
        float volume
    );

    float PeerVolume(
        const std::string& memberId
    );

    // Falando agora (nivel recente da voz).
    bool IsSpeaking(
        const std::string& memberId
    );

    // Conectado e recebendo a voz dessa pessoa.
    bool IsConnected(
        const std::string& memberId
    );

private:
    void JoinWorker();

    void OnMicFrame(
        const float* samples,
        size_t count
    );

    void StopPeers();

    void ReapPeers();

    std::mutex m_mutex;

    std::atomic<bool> m_joining{ false };
    std::atomic<bool> m_joined{ false };
    std::atomic<bool> m_muted{ false };
    std::atomic<bool> m_transmitting{ false };
    std::atomic<int> m_pushToTalkKey{ 0 };

    std::thread m_joinThread;
    std::thread m_serviceThread;

    std::atomic<bool> m_serviceRunning{ false };

    // Protegidos por m_mutex
    std::string m_error;
    std::wstring m_microphone;

    std::map<std::string, std::unique_ptr<VoicePeer>> m_peers;

    // Conexoes parando (destruidas quando a thread termina).
    std::vector<std::unique_ptr<VoicePeer>> m_dying;
    std::map<std::string, float> m_volumes;

    CryptoKeyPair m_keys{};

    UdpAudioServer m_server;

    SignalingHost m_signaling;

    VoiceCapture m_capture;

    VoiceMixer m_mixer;

    AudioPlayback m_playback;

    OpusEncoder* m_encoder = nullptr;

    ULONGLONG m_startMs = 0;
};
