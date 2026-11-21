#include "room_client.h"

#include <windows.h>

#include <chrono>
#include <iostream>

namespace
{

constexpr int HEARTBEAT_SECONDS = 2;

// Sem resposta por mais que isso = "sem conexao" na tela. (O
// servidor tira da sala depois de 20 s sem sinal.)
constexpr ULONGLONG OFFLINE_AFTER_MS = 8000;

} // namespace


RoomClient::~RoomClient()
{
    m_running = false;

    m_cv.notify_all();

    if (m_thread.joinable())
    {
        m_thread.join();
    }
}


bool RoomClient::Create(
    const std::string& name,
    std::string& outError)
{
    RoomMembership membership;

    RoomState state;

    if (!SignalingCreateRoom(
            name,
            membership,
            state,
            outError))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    m_name = name;
    m_membership = membership;
    m_state = state;
    m_wanted = Presenting{};
    m_published = Presenting{};

    std::cout
        << "Sala criada: "
        << membership.code
        << "\n";

    return true;
}


RoomJoinResult RoomClient::Join(
    const std::string& code,
    const std::string& name,
    std::string& outError)
{
    RoomMembership membership;

    RoomState state;

    RoomJoinResult result =
        SignalingJoinRoom(
            code,
            name,
            membership,
            state,
            outError);

    if (result != RoomJoinResult::Joined)
        return result;

    std::lock_guard<std::mutex> lock(m_mutex);

    m_name = name;
    m_membership = membership;
    m_state = state;
    m_wanted = Presenting{};
    m_published = Presenting{};

    std::cout
        << "Entrou na sala "
        << code
        << " ("
        << state.members.size()
        << " pessoas)\n";

    return result;
}


void RoomClient::Start()
{
    if (m_running.exchange(true))
        return;

    // A thread anterior pode ter saido sozinha (sala perdida).
    if (m_thread.joinable())
    {
        m_thread.join();
    }

    m_active = true;
    m_lost = false;
    m_offline = false;

    m_thread =
        std::thread(&RoomClient::Loop, this);
}


void RoomClient::Leave()
{
    m_running = false;

    m_cv.notify_all();

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    if (!m_active.exchange(false))
        return;

    RoomMembership membership;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        membership = m_membership;

        m_state = RoomState{};
    }

    if (!m_lost)
    {
        std::string error;

        SignalingRoomLeave(membership, error);

        std::cout
            << "Saiu da sala "
            << membership.code
            << "\n";
    }
}


void RoomClient::SetPresenting(
    const std::string& sessionCode,
    const std::string& sessionToken,
    const std::string& label)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Presenting wanted{ sessionCode, sessionToken, label };

        if (wanted == m_wanted)
            return;

        m_wanted = wanted;

        m_wake = true;
    }

    // Publica ja (sem esperar os 2 s): quem esta na sala ve
    // quem comecou a transmitir na hora.
    m_cv.notify_all();
}


std::string RoomClient::GetCode()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_membership.code;
}


std::string RoomClient::GetMemberId()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_membership.memberId;
}


RoomState RoomClient::GetState()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_state;
}


// ============================================================
// Loop
// ============================================================

void RoomClient::Loop()
{
    ULONGLONG lastSuccess =
        GetTickCount64();

    while (m_running)
    {
        RoomMembership membership;

        Presenting wanted;

        Presenting published;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            membership = m_membership;
            wanted = m_wanted;
            published = m_published;
        }

        std::string error;


        // ----------------------------------------------------
        // Publicar / retirar a transmissao desta pessoa
        // ----------------------------------------------------

        if (!(wanted == published))
        {
            bool ok =
                wanted.sessionCode.empty()
                ? SignalingRoomStop(membership, error)
                : SignalingRoomPresent(
                      membership,
                      wanted.sessionCode,
                      wanted.sessionToken,
                      wanted.label,
                      error);

            if (ok)
            {
                std::lock_guard<std::mutex> lock(m_mutex);

                m_published = wanted;
            }
            else
            {
                std::cout
                    << "Sala: nao deu pra atualizar a "
                    << "transmissao: "
                    << error
                    << "\n";
            }
        }


        // ----------------------------------------------------
        // Sinal de vida + lista atualizada
        // ----------------------------------------------------

        RoomState state;

        bool gone = false;

        if (SignalingRoomHeartbeat(
                membership,
                state,
                gone,
                error))
        {
            lastSuccess = GetTickCount64();

            m_offline = false;

            std::lock_guard<std::mutex> lock(m_mutex);

            m_state = state;
        }
        else if (gone)
        {
            // O servidor esqueceu a gente (ex.: o PC dormiu, ou
            // o servidor reiniciou e a sala sumiu). Tenta voltar
            // pra mesma sala uma vez.
            std::string name;

            {
                std::lock_guard<std::mutex> lock(m_mutex);

                name = m_name;
            }

            RoomMembership again;

            RoomJoinResult result =
                SignalingJoinRoom(
                    membership.code,
                    name,
                    again,
                    state,
                    error);

            if (result == RoomJoinResult::Joined)
            {
                std::cout
                    << "Sala: reconectado.\n";

                lastSuccess = GetTickCount64();

                m_offline = false;

                std::lock_guard<std::mutex> lock(m_mutex);

                m_membership = again;
                m_state = state;

                // A transmissao precisa ser publicada de novo
                // pelo membro novo.
                m_published = Presenting{};

                continue;
            }

            if (result == RoomJoinResult::NotFound)
            {
                std::cout
                    << "Sala: a sala nao existe mais.\n";

                m_lost = true;

                m_running = false;

                std::lock_guard<std::mutex> lock(m_mutex);

                m_state = RoomState{};

                break;
            }
        }
        else if (GetTickCount64() - lastSuccess >
                 OFFLINE_AFTER_MS)
        {
            m_offline = true;
        }


        // ----------------------------------------------------
        // Espera 2 s (ou ate a transmissao mudar)
        // ----------------------------------------------------

        std::unique_lock<std::mutex> lock(m_mutex);

        m_cv.wait_for(
            lock,
            std::chrono::seconds(HEARTBEAT_SECONDS),
            [this] { return !m_running || m_wake; });

        m_wake = false;
    }
}
