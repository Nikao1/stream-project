#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "capture_targets.h"

// ============================================================
// MINIATURAS DO SELETOR
// ============================================================
//
// Um "print" pequeno de cada tela e janela, pra pessoa ver o
// que vai compartilhar antes de escolher. E tirado UMA vez,
// quando o seletor abre (ou "Atualizar lista"), numa thread
// propria - nada fica rodando depois.
//
// - Tela inteira: copia da imagem do monitor.
// - Janela: o Windows desenha a janela num bitmap nosso
//   (PrintWindow), mesmo que ela esteja atras de outra.
//   Janelas minimizadas nao tem imagem (ficam sem miniatura).
// ============================================================

struct Thumbnail
{
    // BGRA, sem espaco entre as linhas. Vazio = sem imagem.
    std::vector<uint8_t> pixels;

    int width = 0;
    int height = 0;
};

class ThumbnailBatch
{
public:
    // Comeca a tirar as miniaturas (thread propria). As listas
    // sao copiadas.
    static std::shared_ptr<ThumbnailBatch> Start(
        const std::vector<CaptureTarget>& monitors,
        const std::vector<CaptureTarget>& windows
    );

    bool IsDone() const
    {
        return m_done;
    }

    // So depois de IsDone().
    const std::vector<Thumbnail>& Monitors() const
    {
        return m_monitorThumbs;
    }

    const std::vector<Thumbnail>& Windows() const
    {
        return m_windowThumbs;
    }

private:
    void Run(
        std::vector<CaptureTarget> monitors,
        std::vector<CaptureTarget> windows
    );

    std::vector<Thumbnail> m_monitorThumbs;
    std::vector<Thumbnail> m_windowThumbs;

    std::atomic<bool> m_done{ false };
};
