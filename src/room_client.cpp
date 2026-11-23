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

// Contextos da criptografia (cada campo so abre no lugar dele).
const char CONTEXT_NAME[] = "nome";
const char CONTEXT_LABEL[] = "descricao";
const char CONTEXT_TITLE[] = "titulo";

std::string ChatContext(
    const std::string& memberId)
{
    return "chat|" + memberId;
}

std::string ThumbContext(
    const std::string& memberId)
{
    return "previa|" + memberId;
}

// Corta em "maxChars" caracteres sem partir um caractere UTF-8.
std::string TruncateUtf8(
    const std::string& text,
    size_t maxChars)
{
    size_t chars = 0;

    for (size_t i = 0; i < text.size(); ++i)
    {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
        {
            if (chars == maxChars)
                return text.substr(0, i);

            chars++;
        }
    }

    return text;
}

} // namespace


void RoomClient::OpenState(
    RoomState& state,
    const RoomKey& key)
{
    if (!state.title.empty())
    {
        std::string title;

        state.title =
            RoomOpenText(key, CONTEXT_TITLE, state.title, title)
            ? title
            : std::string();
    }

    for (RoomMember& member : state.members)
    {
        std::string name;

        member.name =
            RoomOpenText(key, CONTEXT_NAME, member.name, name)
            ? name
            : std::string("?");
    }

    for (RoomPresenter& presenter : state.presenters)
    {
        std::string text;

        presenter.name =
            RoomOpenText(key, CONTEXT_NAME, presenter.name, text)
            ? text
            : std::string("?");

        presenter.label =
            RoomOpenText(key, CONTEXT_LABEL, presenter.label, text)
            ? text
            : std::string();
    }
}


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
    const std::string& title,
    std::string& outError)
{
    RoomMembership membership;

    RoomState state;

    // Chave nova: segredo (vai no codigo, nunca pro servidor)
    // + sal (guardado no servidor).
    std::string secret = RoomSecretGenerate();
    std::string salt = RoomSaltGenerate();

    RoomKey key;

    if (secret.empty() || salt.empty() ||
        !RoomKeyDerive(secret, salt, key))
    {
        outError = "falha ao gerar a chave da sala";

        return false;
    }

    std::string sealedName =
        RoomSealText(key, CONTEXT_NAME, TruncateUtf8(name, 32));

    std::string sealedTitle =
        title.empty()
        ? std::string()
        : RoomSealText(key, CONTEXT_TITLE, TruncateUtf8(title, 32));

    if (!SignalingCreateRoom(
            sealedName,
            sealedTitle,
            salt,
            membership,
            state,
            outError))
    {
        return false;
    }

    OpenState(state, key);

    std::lock_guard<std::mutex> lock(m_mutex);

    m_key = key;
    m_secret = secret;
    m_name = sealedName;
    m_membership = membership;
    m_state = state;
    m_wanted = Presenting{};
    m_published = Presenting{};

    m_chatAfter = 0;
    m_chat.clear();
    m_chatTotal = 0;
    m_outbox.clear();

    m_wantedVoice.clear();
    m_publishedVoice.clear();

    m_thumbs.clear();

    m_avatars.clear();
    m_avatarDirty = !m_myAvatar.empty();
    m_avatarRetryAt = 0;

    // A propria foto aparece na hora (mesmo antes de chegar ao
    // servidor).
    if (!m_myAvatar.empty())
    {
        m_avatars[membership.memberId] =
            Thumb{ "local" + std::to_string(++m_avatarSerial), m_myAvatar };
    }

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

    std::string roomCode;
    std::string secret;

    if (!ParseRoomInvite(code, roomCode, secret))
        return RoomJoinResult::NotFound;

    // A sala existe? Tem criptografia de ponta a ponta?
    bool encrypted = false;

    std::string salt;

    RoomJoinResult result =
        SignalingRoomInfo(roomCode, encrypted, salt, outError);

    if (result != RoomJoinResult::Joined)
        return result;

    RoomKey key;

    if (encrypted)
    {
        if (secret.empty())
            return RoomJoinResult::NeedsKey;

        if (!RoomKeyDerive(secret, salt, key))
        {
            outError = "falha ao gerar a chave da sala";

            return RoomJoinResult::Error;
        }
    }
    else
    {
        // Sala criada por versao antiga: sem segredo.
        secret.clear();
    }

    std::string sealedName =
        RoomSealText(key, CONTEXT_NAME, TruncateUtf8(name, 32));

    result =
        SignalingJoinRoom(
            roomCode,
            sealedName,
            encrypted,
            membership,
            state,
            outError);

    if (result != RoomJoinResult::Joined)
        return result;

    // Segredo errado: os nomes de quem ja esta na sala nao
    // abrem. Sai e avisa.
    if (encrypted)
    {
        bool anyOther = false;
        bool anyOpened = false;

        for (const RoomMember& member : state.members)
        {
            if (member.id == membership.memberId)
                continue;

            anyOther = true;

            std::string plain;

            if (RoomOpenText(key, CONTEXT_NAME, member.name, plain))
            {
                anyOpened = true;
            }
        }

        if (anyOther && !anyOpened)
        {
            std::string ignored;

            SignalingRoomLeave(membership, ignored);

            return RoomJoinResult::WrongKey;
        }
    }

    OpenState(state, key);

    std::lock_guard<std::mutex> lock(m_mutex);

    m_key = key;
    m_secret = secret;
    m_name = sealedName;
    m_membership = membership;
    m_state = state;
    m_wanted = Presenting{};
    m_published = Presenting{};

    m_chatAfter = 0;
    m_chat.clear();
    m_chatTotal = 0;
    m_outbox.clear();

    m_wantedVoice.clear();
    m_publishedVoice.clear();

    m_thumbs.clear();

    m_avatars.clear();
    m_avatarDirty = !m_myAvatar.empty();
    m_avatarRetryAt = 0;

    // A propria foto aparece na hora (mesmo antes de chegar ao
    // servidor).
    if (!m_myAvatar.empty())
    {
        m_avatars[membership.memberId] =
            Thumb{ "local" + std::to_string(++m_avatarSerial), m_myAvatar };
    }

    std::cout
        << "Entrou na sala "
        << roomCode
        << (encrypted ? " (criptografada)" : "")
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
    m_heartbeats = 0;

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

    // Codigo completo (com o segredo) - e o que se passa pros
    // amigos.
    return
        m_secret.empty()
        ? m_membership.code
        : m_membership.code + "-" + m_secret;
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


void RoomClient::SetVoice(
    const std::string& sessionCode,
    const std::string& sessionToken)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::string wanted =
            sessionCode.empty()
            ? std::string()
            : sessionCode + "|" + sessionToken;

        if (wanted == m_wantedVoice)
            return;

        m_wantedVoice = wanted;

        m_wake = true;
    }

    m_cv.notify_all();
}


void RoomClient::SetTitle(
    const std::string& title)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_titleWanted = TruncateUtf8(title, 32);
        m_titlePending = true;

        m_wake = true;
    }

    m_cv.notify_all();
}


void RoomClient::SendChat(
    const std::string& text)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_outbox.size() >= 20)
            return;

        m_outbox.push_back(text);

        m_wake = true;
    }

    m_cv.notify_all();
}


std::vector<RoomChatMessage> RoomClient::GetChat(
    uint64_t& outTotal)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    outTotal = m_chatTotal;

    return std::vector<RoomChatMessage>(
        m_chat.begin(),
        m_chat.end());
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

        RoomKey key;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            key = m_key;

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
                      RoomSealText(
                          key, CONTEXT_LABEL,
                          TruncateUtf8(wanted.label, 80)),
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
        // Nome da sala
        // ----------------------------------------------------

        {
            std::string title;
            bool pending = false;

            {
                std::lock_guard<std::mutex> lock(m_mutex);

                pending = m_titlePending;
                title = m_titleWanted;
                m_titlePending = false;
            }

            if (pending &&
                !SignalingRoomTitle(
                    membership,
                    title.empty()
                    ? std::string()
                    : RoomSealText(key, CONTEXT_TITLE, title),
                    error))
            {
                std::cout
                    << "Sala: nao deu pra trocar o nome: "
                    << error << "\n";
            }
        }


        // ----------------------------------------------------
        // Voz: publicar / retirar a sessao de voz
        // ----------------------------------------------------

        std::string wantedVoice;
        std::string publishedVoice;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            wantedVoice = m_wantedVoice;
            publishedVoice = m_publishedVoice;
        }

        if (wantedVoice != publishedVoice)
        {
            size_t bar = wantedVoice.find('|');

            std::string code =
                bar == std::string::npos
                ? std::string()
                : wantedVoice.substr(0, bar);

            std::string token =
                bar == std::string::npos
                ? std::string()
                : wantedVoice.substr(bar + 1);

            if (SignalingRoomVoice(membership, code, token, error))
            {
                std::lock_guard<std::mutex> lock(m_mutex);

                m_publishedVoice = wantedVoice;
            }
            else
            {
                std::cout
                    << "Sala: nao deu pra atualizar a voz: "
                    << error
                    << "\n";
            }
        }


        // ----------------------------------------------------
        // Chat: o que esta pessoa escreveu
        // ----------------------------------------------------

        for (;;)
        {
            std::string text;

            {
                std::lock_guard<std::mutex> lock(m_mutex);

                if (m_outbox.empty())
                    break;

                text = m_outbox.front();
            }

            if (!SignalingRoomChat(
                    membership,
                    RoomSealText(
                        key,
                        ChatContext(membership.memberId),
                        TruncateUtf8(text, 300)),
                    error))
            {
                std::cout
                    << "Sala: chat nao enviado: "
                    << error
                    << "\n";

                // Tenta de novo na proxima volta (internet
                // oscilou), mas nao fica preso nisso.
                break;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            if (!m_outbox.empty())
            {
                m_outbox.pop_front();
            }
        }


        // ----------------------------------------------------
        // Previa da propria transmissao (a cada ~15 s)
        // ----------------------------------------------------

        UpdateThumbnails(membership, RoomState{});


        // ----------------------------------------------------
        // Sinal de vida + lista atualizada + chat novo
        // ----------------------------------------------------

        RoomState state;

        std::vector<RoomChatMessage> newMessages;

        int chatAfter = 0;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            chatAfter = m_chatAfter;
        }

        bool gone = false;

        if (SignalingRoomHeartbeat(
                membership,
                chatAfter,
                state,
                newMessages,
                gone,
                error))
        {
            lastSuccess = GetTickCount64();

            m_offline = false;

            OpenState(state, key);

            std::lock_guard<std::mutex> lock(m_mutex);

            m_state = state;

            for (const RoomChatMessage& message : newMessages)
            {
                if (message.id <= m_chatAfter)
                    continue;

                m_chatAfter = message.id;

                // Abre a mensagem; a que nao abre (nao foi
                // trancada com a chave da sala) e descartada.
                RoomChatMessage opened = message;

                std::string text;

                if (!RoomOpenText(
                        key,
                        ChatContext(message.memberId),
                        message.text,
                        text) ||
                    !RoomOpenText(
                        key, CONTEXT_NAME, message.name, opened.name))
                {
                    continue;
                }

                opened.text = text;

                m_chat.push_back(opened);

                m_chatTotal++;
            }

            while (m_chat.size() > 200)
            {
                m_chat.pop_front();
            }

            m_heartbeats++;
        }

        // Previas de quem transmite e fotos (fora da trava: rede).
        if (!state.members.empty())
        {
            UpdateThumbnails(membership, state);

            UpdateAvatars(membership, state);
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
                    key.valid,
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

                OpenState(state, key);

                m_membership = again;
                m_state = state;

                // A transmissao, a voz e a foto precisam ser
                // publicadas de novo pelo membro novo.
                m_published = Presenting{};

                m_avatarDirty = !m_myAvatar.empty();

                m_publishedVoice.clear();

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


// ------------------------------------------------------------
// Previas
// ------------------------------------------------------------
//
// state vazio: so a parte de ENVIAR (a propria previa). Com
// state: so a de BAIXAR as dos outros.
// ------------------------------------------------------------

namespace
{

constexpr unsigned long long THUMB_INTERVAL_MS = 15000;

// Primeira previa: uns segundos depois de comecar (a captura
// precisa de um frame).
constexpr unsigned long long THUMB_FIRST_DELAY_MS = 3000;

// Previas baixadas por volta (cada uma ~15 KB).
constexpr int THUMB_FETCH_PER_LOOP = 3;

} // namespace


void RoomClient::UpdateThumbnails(
    const RoomMembership& membership,
    const RoomState& state)
{
    std::string error;

    if (state.members.empty())
    {
        std::string session;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            session = m_published.sessionCode;
        }

        ULONGLONG now = GetTickCount64();

        if (session.empty() || !m_thumbEnabled || !m_thumbCapture)
        {
            // Nao transmite: o servidor apaga a previa sozinho.
            m_thumbSession.clear();
            m_thumbOnServer = false;

            std::lock_guard<std::mutex> lock(m_mutex);

            m_thumbs.erase(membership.memberId);

            return;
        }

        if (session != m_thumbSession)
        {
            m_thumbSession = session;
            m_thumbOnServer = false;
            m_thumbSentAt =
                now - THUMB_INTERVAL_MS + THUMB_FIRST_DELAY_MS;
        }

        bool enabled = m_thumbEnabled();

        // Desligou a previa: tira do servidor na hora.
        if (!enabled)
        {
            if (m_thumbOnServer &&
                SignalingRoomThumb(membership, {}, error))
            {
                m_thumbOnServer = false;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            m_thumbs.erase(membership.memberId);

            return;
        }

        if (now - m_thumbSentAt < THUMB_INTERVAL_MS)
            return;

        std::vector<uint8_t> jpeg;

        if (!m_thumbCapture(jpeg) || jpeg.empty())
        {
            // Sem imagem ainda: tenta de novo em poucos segundos.
            m_thumbSentAt =
                now - THUMB_INTERVAL_MS + THUMB_FIRST_DELAY_MS;

            return;
        }

        m_thumbSentAt = now;

        RoomKey key;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            key = m_key;
        }

        if (SignalingRoomThumb(
                membership,
                RoomSealBytes(key, ThumbContext(membership.memberId), jpeg),
                error))
        {
            m_thumbOnServer = true;

            std::lock_guard<std::mutex> lock(m_mutex);

            Thumb& own = m_thumbs[membership.memberId];

            own.version = "local" + std::to_string(++m_localThumbCount);
            own.jpeg = std::move(jpeg);
        }
        else
        {
            std::cout
                << "Sala: previa nao enviada: " << error << "\n";
        }

        return;
    }


    // ---- Baixar as dos outros ----

    std::vector<std::pair<std::string, std::string>> wanted;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Esquece previas de quem parou (ou desligou a previa).
        for (auto it = m_thumbs.begin(); it != m_thumbs.end();)
        {
            bool keep = it->first == membership.memberId;

            for (const RoomPresenter& presenter : state.presenters)
            {
                if (presenter.memberId == it->first &&
                    !presenter.thumbVersion.empty())
                {
                    keep = true;
                }
            }

            it = keep ? std::next(it) : m_thumbs.erase(it);
        }

        if (!m_wantThumbs)
            return;

        for (const RoomPresenter& presenter : state.presenters)
        {
            if (presenter.memberId == membership.memberId ||
                presenter.thumbVersion.empty())
            {
                continue;
            }

            auto found = m_thumbs.find(presenter.memberId);

            if (found == m_thumbs.end() ||
                found->second.version != presenter.thumbVersion)
            {
                wanted.emplace_back(
                    presenter.memberId, presenter.thumbVersion);
            }
        }
    }

    int fetched = 0;

    for (const auto& item : wanted)
    {
        if (fetched++ >= THUMB_FETCH_PER_LOOP || !m_running)
            break;

        std::vector<uint8_t> sealed;

        std::vector<uint8_t> jpeg;

        std::string version;

        if (!SignalingRoomGetThumb(
                membership, item.first, sealed, version, error))
        {
            continue;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        if (!RoomOpenBytes(
                m_key, ThumbContext(item.first), sealed, jpeg))
        {
            // Nao abre: marca a versao pra nao baixar de novo.
            m_thumbs[item.first] = Thumb{ item.second, {} };

            continue;
        }

        Thumb& thumb = m_thumbs[item.first];

        thumb.version = version.empty() ? item.second : version;
        thumb.jpeg = std::move(jpeg);
    }
}


std::string RoomClient::ThumbnailVersion(
    const std::string& memberId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto found = m_thumbs.find(memberId);

    return found == m_thumbs.end() ? std::string() : found->second.version;
}


bool RoomClient::GetThumbnail(
    const std::string& memberId,
    std::string& outVersion,
    std::vector<uint8_t>& outJpeg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto found = m_thumbs.find(memberId);

    if (found == m_thumbs.end())
        return false;

    outVersion = found->second.version;
    outJpeg = found->second.jpeg;

    return true;
}


// ------------------------------------------------------------
// Fotos de perfil
// ------------------------------------------------------------

namespace
{

std::string AvatarContext(
    const std::string& memberId)
{
    return "foto|" + memberId;
}

} // namespace


void RoomClient::SetAvatar(
    const std::vector<uint8_t>& jpeg)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_myAvatar = jpeg;
        m_avatarDirty = true;
        m_avatarRetryAt = 0;

        // Na hora, na propria tela.
        std::string myId = m_membership.memberId;

        if (!myId.empty())
        {
            if (jpeg.empty())
            {
                m_avatars.erase(myId);
            }
            else
            {
                m_avatars[myId] =
                    Thumb{ "local" + std::to_string(++m_avatarSerial), jpeg };
            }
        }

        m_wake = true;
    }

    m_cv.notify_all();
}


void RoomClient::UpdateAvatars(
    const RoomMembership& membership,
    const RoomState& state)
{
    std::string error;

    // ---- Enviar a propria (quando mudou / reconectou) ----
    std::vector<uint8_t> mine;
    bool dirty = false;
    RoomKey key;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        dirty = m_avatarDirty;
        mine = m_myAvatar;
        key = m_key;
    }

    // Falhou ha pouco (ex.: servidor sem a rota de fotos): espera
    // 30 s antes de tentar de novo, em vez de a cada 2 s.
    if (dirty && GetTickCount64() < m_avatarRetryAt)
    {
        dirty = false;
    }

    if (dirty)
    {
        std::vector<uint8_t> sealed =
            mine.empty()
            ? std::vector<uint8_t>()
            : RoomSealBytes(key, AvatarContext(membership.memberId), mine);

        if (SignalingRoomAvatar(membership, sealed, error))
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            // (se mudou de novo no meio, envia na proxima volta)
            if (m_myAvatar == mine)
            {
                m_avatarDirty = false;
            }

            if (!mine.empty() &&
                m_avatars.find(membership.memberId) == m_avatars.end())
            {
                m_avatars[membership.memberId] =
                    Thumb{ "local" + std::to_string(++m_avatarSerial), mine };
            }
        }
        else
        {
            std::cout << "Sala: foto nao enviada: " << error << "\n";

            m_avatarRetryAt = GetTickCount64() + 30000;
        }
    }

    // ---- Baixar as dos outros ----
    std::vector<std::pair<std::string, std::string>> wanted;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto it = m_avatars.begin(); it != m_avatars.end();)
        {
            bool keep = it->first == membership.memberId;

            for (const RoomMember& member : state.members)
            {
                if (member.id == it->first && !member.avatarVersion.empty())
                {
                    keep = true;
                }
            }

            it = keep ? std::next(it) : m_avatars.erase(it);
        }

        for (const RoomMember& member : state.members)
        {
            if (member.id == membership.memberId ||
                member.avatarVersion.empty())
            {
                continue;
            }

            auto found = m_avatars.find(member.id);

            if (found == m_avatars.end() ||
                found->second.version != member.avatarVersion)
            {
                wanted.emplace_back(member.id, member.avatarVersion);
            }
        }
    }

    int fetched = 0;

    for (const auto& item : wanted)
    {
        if (fetched++ >= 3 || !m_running)
            break;

        std::vector<uint8_t> sealed;
        std::vector<uint8_t> jpeg;
        std::string version;

        if (!SignalingRoomGetAvatar(
                membership, item.first, sealed, version, error))
        {
            continue;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        if (!RoomOpenBytes(m_key, AvatarContext(item.first), sealed, jpeg))
        {
            m_avatars[item.first] = Thumb{ item.second, {} };

            continue;
        }

        m_avatars[item.first] =
            Thumb{ version.empty() ? item.second : version, std::move(jpeg) };
    }
}


std::string RoomClient::AvatarVersion(
    const std::string& memberId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto found = m_avatars.find(memberId);

    return found == m_avatars.end() ? std::string() : found->second.version;
}


bool RoomClient::GetAvatar(
    const std::string& memberId,
    std::string& outVersion,
    std::vector<uint8_t>& outJpeg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto found = m_avatars.find(memberId);

    if (found == m_avatars.end())
        return false;

    outVersion = found->second.version;
    outJpeg = found->second.jpeg;

    return true;
}
