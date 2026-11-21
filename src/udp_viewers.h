#pragma once

#include <winsock2.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

// ============================================================
// LISTA DE ESPECTADORES
// ============================================================
//
// Uma sessao pode ter varios receivers ao mesmo tempo. Cada
// servidor UDP do client (video e audio) guarda uma lista
// destas e envia cada pacote pra todos.
//
// - Entra na lista: quem manda "HELLO".
// - Fica na lista: quem continua dando sinal de vida ("HELLO"
//   antes do stream, "KEEP" a cada 5s durante).
// - Sai da lista: quem fica VIEWER_TIMEOUT_MS sem dar sinal
//   (fechou o receiver, caiu a rede...).
//
// O limite de espectadores existe por causa do UPLOAD de quem
// transmite: cada um recebe uma copia inteira do fluxo.
// ============================================================

// 8: com mais, o upload de quem transmite (uma copia do video
// por pessoa) passa do que a maioria dos planos aguenta - ver a
// estimativa no painel do modo Transmitir.
constexpr size_t MAX_VIEWERS = 8;

constexpr ULONGLONG VIEWER_TIMEOUT_MS = 15000;


// Um espectador: pra onde enviar e com qual chave cifrar. Cada
// espectador tem a sua chave de sessao (ver stream_crypto.h).
struct UdpViewer
{
    sockaddr_in address;

    uint8_t sessionKey[32];
};


class UdpViewerList
{
public:

    enum class TouchResult
    {
        Refreshed,  // ja estava na lista - renovou o prazo
        Added,      // entrou agora
        Full,       // lista cheia - nao entrou
        Unknown     // nao esta na lista (e nao era pra entrar)
    };


    // Registra sinal de vida de um endereco.
    //
    // Com sessionKey (vem de um HELLO valido), um endereco novo
    // entra na lista se houver vaga, e um ja conhecido tem a
    // chave atualizada (o receiver pode ter sido reaberto na
    // mesma porta, com chaves novas).
    //
    // Sem sessionKey (KEEP), so renova quem ja esta na lista.
    TouchResult Touch(
        const sockaddr_in& address,
        ULONGLONG now,
        const uint8_t* sessionKey = nullptr)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (Entry& viewer : m_viewers)
        {
            if (Same(viewer.viewer.address, address))
            {
                viewer.lastSeen = now;

                if (sessionKey)
                {
                    std::memcpy(
                        viewer.viewer.sessionKey,
                        sessionKey,
                        32);
                }

                return TouchResult::Refreshed;
            }
        }

        if (!sessionKey)
            return TouchResult::Unknown;

        if (m_viewers.size() >= MAX_VIEWERS)
            return TouchResult::Full;

        Entry entry{};

        entry.viewer.address = address;

        std::memcpy(
            entry.viewer.sessionKey,
            sessionKey,
            32);

        entry.lastSeen = now;

        m_viewers.push_back(entry);

        return TouchResult::Added;
    }


    // Remove quem passou do prazo; os removidos vao pra
    // outRemoved (pra quem chama poder avisar no console).
    void Prune(
        ULONGLONG now,
        std::vector<sockaddr_in>& outRemoved)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (size_t i = 0; i < m_viewers.size();)
        {
            if (now - m_viewers[i].lastSeen >
                VIEWER_TIMEOUT_MS)
            {
                outRemoved.push_back(
                    m_viewers[i].viewer.address);

                m_viewers.erase(
                    m_viewers.begin() +
                    static_cast<std::ptrdiff_t>(i));
            }
            else
            {
                ++i;
            }
        }
    }


    // Copia os espectadores atuais pra um array fixo (sem
    // alocar memoria - e chamado a cada frame/pacote enviado).
    // Devolve quantos copiou.
    size_t Snapshot(
        UdpViewer outViewers[MAX_VIEWERS])
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        size_t count = m_viewers.size();

        for (size_t i = 0; i < count; ++i)
        {
            outViewers[i] = m_viewers[i].viewer;
        }

        return count;
    }


    // Tira um espectador da lista (ele avisou que saiu).
    // Devolve true se ele estava na lista.
    bool Remove(
        const sockaddr_in& address)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (size_t i = 0; i < m_viewers.size(); ++i)
        {
            if (Same(m_viewers[i].viewer.address, address))
            {
                m_viewers.erase(m_viewers.begin() + i);

                return true;
            }
        }

        return false;
    }


    // Procura um espectador pelo endereco. Devolve false se
    // ele nao esta (mais) na lista.
    bool Find(
        const sockaddr_in& address,
        UdpViewer& outViewer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (const Entry& entry : m_viewers)
        {
            if (Same(entry.viewer.address, address))
            {
                outViewer = entry.viewer;

                return true;
            }
        }

        return false;
    }


    size_t Count()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        return m_viewers.size();
    }


    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_viewers.clear();
    }

private:

    struct Entry
    {
        UdpViewer viewer;

        ULONGLONG lastSeen;
    };


    static bool Same(
        const sockaddr_in& a,
        const sockaddr_in& b)
    {
        return
            a.sin_addr.s_addr == b.sin_addr.s_addr &&
            a.sin_port == b.sin_port;
    }


    std::mutex m_mutex;

    std::vector<Entry> m_viewers;
};
