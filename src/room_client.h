#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

    bool Create(
        const std::string& name,
        std::string& outError
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

    // Chat: manda uma mensagem (sai na proxima volta da thread,
    // em instantes).
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

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;

    std::atomic<bool> m_active{ false };
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_lost{ false };
    std::atomic<bool> m_offline{ false };
    std::atomic<uint64_t> m_heartbeats{ 0 };

    // Protegidos por m_mutex
    std::string m_name;
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

    // Chat (protegido por m_mutex)
    int m_chatAfter = 0;
    std::deque<RoomChatMessage> m_chat;
    uint64_t m_chatTotal = 0;
    std::deque<std::string> m_outbox;
};
