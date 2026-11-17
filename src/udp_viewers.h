#pragma once

#include <winsock2.h>

#include <cstddef>
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

constexpr size_t MAX_VIEWERS = 4;

constexpr ULONGLONG VIEWER_TIMEOUT_MS = 15000;


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


    // Registra sinal de vida de um endereco. Com allowAdd, um
    // endereco novo entra na lista (se houver vaga).
    TouchResult Touch(
        const sockaddr_in& address,
        bool allowAdd,
        ULONGLONG now)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (Viewer& viewer : m_viewers)
        {
            if (Same(viewer.address, address))
            {
                viewer.lastSeen = now;

                return TouchResult::Refreshed;
            }
        }

        if (!allowAdd)
            return TouchResult::Unknown;

        if (m_viewers.size() >= MAX_VIEWERS)
            return TouchResult::Full;

        m_viewers.push_back({ address, now });

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
                    m_viewers[i].address);

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


    // Copia os enderecos atuais pra um array fixo (sem alocar
    // memoria - e chamado a cada frame/pacote enviado).
    // Devolve quantos copiou.
    size_t Snapshot(
        sockaddr_in outAddresses[MAX_VIEWERS])
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        size_t count = m_viewers.size();

        for (size_t i = 0; i < count; ++i)
        {
            outAddresses[i] = m_viewers[i].address;
        }

        return count;
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

    struct Viewer
    {
        sockaddr_in address;

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

    std::vector<Viewer> m_viewers;
};
