#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "room_crypto.h"
#include "signaling_client.h"

// ============================================================
// SALA - o lado do programa
// ============================================================
//
// Mantem a pessoa na sala (avisa o servidor a cada 2 s), traz a
// lista de quem esta nela e de quem esta transmitindo, e
// publica/retira a transmissao desta pessoa.
//
// Create()/Join() bloqueiam (o servidor gratuito pode levar ate
// ~1 minuto pra acordar): chamar de uma thread de trabalho. O
// resto e rapido e pode ser chamado da thread da janela.
// ============================================================

class RoomClient
{
public:
    ~RoomClient();

    // title: nome da sala (opcional, vazio = sem nome).
    bool Create(
        const std::string& name,
        const std::string& title,
        std::string& outError
    );

    // Troca o nome da sala (vale pra todos; sai na proxima volta
    // da thread). Vazio = sem nome.
    void SetTitle(
        const std::string& title
    );

    RoomJoinResult Join(
        const std::string& code,
        const std::string& name,
        std::string& outError
    );

    // Comeca a avisar o servidor (thread propria). Depois de
    // Create()/Join() darem certo.
    void Start();

    // Sai da sala (para a thread e avisa o servidor).
    void Leave();

    // O que esta pessoa esta transmitindo: codigo e prova
    // (token) da sessao dela, e o que aparece na lista. Codigo
    // vazio = nao esta transmitindo.
    void SetPresenting(
        const std::string& sessionCode,
        const std::string& sessionToken,
        const std::string& label
    );

    bool IsActive() const
    {
        return m_active;
    }

    // A sala acabou (ou o servidor esqueceu a gente e nao deu
    // pra voltar).
    bool IsLost() const
    {
        return m_lost;
    }

    // Sem resposta do servidor ha um tempo (internet caiu?).
    bool IsOffline() const
    {
        return m_offline;
    }

    // Quantas respostas do servidor ja chegaram desde que
    // entrou (a primeira traz o historico do chat).
    uint64_t HeartbeatCount() const
    {
        return m_heartbeats;
    }

    std::string GetCode();

    std::string GetMemberId();

    RoomState GetState();

    // Canal de voz: codigo e prova da sessao de voz desta
    // pessoa. Codigo vazio = fora da voz.
    void SetVoice(
        const std::string& sessionCode,
        const std::string& sessionToken
    );

    // Chat: manda uma mensagem (sai na proxima volta da thread,
    // em instantes).
    // Previa da transmissao desta pessoa: enabled() diz se a
    // pessoa quer mostrar; capture() tira a imagem (JPEG) - true
    // = conseguiu. Chamados na thread da sala, a cada ~15 s
    // enquanto transmite. Chamar antes de Start().
    void SetThumbnailSource(
        std::function<bool()> enabled,
        std::function<bool(std::vector<uint8_t>&)> capture)
    {
        m_thumbEnabled = std::move(enabled);
        m_thumbCapture = std::move(capture);
    }

    // Baixar as previas de quem transmite (so quando a lista
    // esta na tela - economiza internet).
    void SetWantThumbnails(
        bool want)
    {
        m_wantThumbs = want;
    }

    // Foto de perfil desta pessoa (JPEG pequeno; vazio = sem
    // foto). Enviada criptografada; reenviada ao reconectar.
    void SetAvatar(
        const std::vector<uint8_t>& jpeg
    );

    // Fotos das pessoas da sala (inclusive a propria): mesmo
    // esquema das previas.
    std::string AvatarVersion(
        const std::string& memberId
    );

    bool GetAvatar(
        const std::string& memberId,
        std::string& outVersion,
        std::vector<uint8_t>& outJpeg
    );

    // Versao da previa guardada (vazio = nenhuma). Barato: pra
    // conferir a cada frame se mudou.
    std::string ThumbnailVersion(
        const std::string& memberId
    );

    bool GetThumbnail(
        const std::string& memberId,
        std::string& outVersion,
        std::vector<uint8_t>& outJpeg
    );

    void SendChat(
        const std::string& text
    );

    // Mensagens recebidas (as ultimas, em ordem). "total" conta
    // todas desde que entrou - pra saber se chegou algo novo.
    std::vector<RoomChatMessage> GetChat(
        uint64_t& outTotal
    );

private:
    void Loop();

    // Abre (descriptografa) nomes e descricoes do estado da sala.
    void OpenState(
        RoomState& state,
        const RoomKey& key
    );

    void UpdateThumbnails(
        const RoomMembership& membership,
        const RoomState& state
    );

    void UpdateAvatars(
        const RoomMembership& membership,
        const RoomState& state
    );

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;

    std::atomic<bool> m_active{ false };
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_lost{ false };
    std::atomic<bool> m_offline{ false };
    std::atomic<uint64_t> m_heartbeats{ 0 };

    // Protegidos por m_mutex
    // Nome desta pessoa como vai pro servidor (ja trancado).
    std::string m_name;

    // Criptografia de ponta a ponta: chave da sala e o segredo
    // (parte do codigo depois do traco). Sem chave = sala de
    // versao antiga (texto puro).
    RoomKey m_key;
    std::string m_secret;
    RoomMembership m_membership;
    RoomState m_state;

    struct Presenting
    {
        std::string sessionCode;
        std::string sessionToken;
        std::string label;

        bool operator==(const Presenting& other) const
        {
            return sessionCode == other.sessionCode &&
                   sessionToken == other.sessionToken &&
                   label == other.label;
        }
    };

    Presenting m_wanted;
    Presenting m_published;
    bool m_wake = false;

    // Voz: desejado x publicado (codigo|token)
    std::string m_wantedVoice;
    std::string m_publishedVoice;

    // Chat (protegido por m_mutex)
    int m_chatAfter = 0;
    std::deque<RoomChatMessage> m_chat;
    uint64_t m_chatTotal = 0;
    std::deque<std::string> m_outbox;

    // Nome da sala a publicar (m_titlePending = falta mandar).
    std::string m_titleWanted;
    bool m_titlePending = false;

    // Previas
    std::function<bool()> m_thumbEnabled;
    std::function<bool(std::vector<uint8_t>&)> m_thumbCapture;

    std::atomic<bool> m_wantThumbs{ false };

    // So a thread da sala mexe nestes tres.
    std::string m_thumbSession;
    unsigned long long m_thumbSentAt = 0;
    bool m_thumbOnServer = false;

    struct Thumb
    {
        std::string version;
        std::vector<uint8_t> jpeg;
    };

    // memberId -> previa (inclusive a propria).
    std::map<std::string, Thumb> m_thumbs;

    uint64_t m_localThumbCount = 0;

    // Fotos: a propria (pra enviar) e as baixadas.
    std::vector<uint8_t> m_myAvatar;
    bool m_avatarDirty = false;
    unsigned long long m_avatarRetryAt = 0;
    uint64_t m_avatarSerial = 0;

    std::map<std::string, Thumb> m_avatars;
};
