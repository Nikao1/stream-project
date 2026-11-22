#include "client_screens.h"

#include "imgui.h"
#include "ui_widgets.h"


namespace
{

// Lista suspensa ocupando a largura do painel, com rotulo em
// cima.
void LabeledCombo(
    const char* label,
    const char* id,
    int* index,
    const char* const* items,
    int itemCount)
{
    UiFieldLabel(label);

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x);

    ImGui::Combo(
        id,
        index,
        items,
        itemCount
    );
}


// Botao que troca o texto por "Copiado!" durante 2 segundos
// depois do clique. "copiedAt" guarda o instante do clique.
bool CopyButton(
    const char* label,
    const char* id,
    double& copiedAt)
{
    bool recentlyCopied =
        copiedAt > 0.0 &&
        ImGui::GetTime() - copiedAt < 2.0;

    std::string text =
        std::string(recentlyCopied ? "Copiado!" : label) +
        id;

    bool clicked =
        ImGui::Button(
            text.c_str(),
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                0.0f));

    if (clicked)
    {
        copiedAt = ImGui::GetTime();
    }

    return clicked;
}

} // namespace


// ============================================================
// Tela inicial
// ============================================================

// Opcoes das listas de qualidade e FPS (mesma ordem dos indices
// de ClientSetupModel).
const char* const QUALITY_OPTIONS[] =
{
    "480p",
    "720p",
    "1080p",
    "Nativa (a resolução da tela)"
};

const char* const FPS_OPTIONS[] =
{
    "60 FPS",
    "30 FPS",
    "15 FPS"
};


void DrawClientSetupScreen(
    ClientSetupModel& model,
    ClientScreenActions& actions)
{
    const char* const* qualities = QUALITY_OPTIONS;

    const char* const* frameRates = FPS_OPTIONS;

    UiBeginCenteredPanel("##transmitir", 460.0f);

    UiTitle("Transmitir");

    UiMutedText(
        "Compartilhe uma tela ou janela, com o som do "
        "computador, com até 4 pessoas."
    );

    ImGui::Spacing();

    ImGui::BeginDisabled(model.busy);

    LabeledCombo(
        "Qualidade",
        "##qualidade",
        model.qualityIndex,
        qualities,
        4
    );

    LabeledCombo(
        "FPS",
        "##fps",
        model.fpsIndex,
        frameRates,
        3
    );

    // A lista de audio vem do sistema, entao e montada na hora.
    UiFieldLabel("Som");

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x);

    const std::vector<std::string>& devices =
        *model.audioDevices;

    int& audioIndex =
        *model.audioIndex;

    if (audioIndex < 0 ||
        audioIndex >= static_cast<int>(devices.size()))
    {
        audioIndex = 0;
    }

    if (ImGui::BeginCombo(
            "##audio",
            devices.empty()
                ? ""
                : devices[audioIndex].c_str()))
    {
        for (int i = 0;
             i < static_cast<int>(devices.size());
             ++i)
        {
            // O "##i" garante ids diferentes mesmo se dois
            // dispositivos tiverem o mesmo nome.
            std::string itemLabel =
                devices[i] + "##" + std::to_string(i);

            if (ImGui::Selectable(
                    itemLabel.c_str(),
                    i == audioIndex))
            {
                audioIndex = i;
            }
        }

        ImGui::EndCombo();
    }

    ImGui::EndDisabled();

    ImGui::Spacing();

    if (UiPrimaryButton(
            model.busy
                ? "Preparando..."
                : "Escolher o que transmitir",
            !model.busy))
    {
        actions.start = true;
    }

    if (model.busy)
    {
        UiMutedText(
            (model.status + UiBusyDots()).c_str()
        );

        UiMutedText(
            "Na primeira transmissão depois de um tempo "
            "parado, o servidor pode levar até 1 minuto para "
            "responder."
        );
    }
    else
    {
        UiMutedText(
            "Na próxima tela você escolhe uma tela inteira ou "
            "uma janela."
        );
    }

    if (model.canGoBack)
    {
        ImGui::Spacing();

        if (ImGui::Button(
                "Voltar",
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    0.0f)))
        {
            actions.goHome = true;
        }
    }

    UiEndPanel();
}


// ============================================================
// Tela de transmissao
// ============================================================

void DrawClientLiveScreen(
    ClientLiveModel& model,
    ClientScreenActions& actions)
{
    // Instantes do ultimo clique nos botoes de copiar.
    static double codeCopiedAt = 0.0;
    static double inviteCopiedAt = 0.0;

    // Painel minimizado (so nesta execucao): uma barra pequena
    // com o essencial, pra ver a previa (e o chat/pessoas da
    // sala) sem o painel na frente.
    static bool minimized = false;

    if (minimized)
    {
        UiBeginCornerPanel("##aovivomin", 300.0f);

        std::string line =
            "Transmitindo  ·  " +
            std::to_string(model.viewers) +
            " assistindo";

        UiWrappedColored(UiColorSuccess(), line.c_str());

        float half =
            (ImGui::GetContentRegionAvail().x -
             ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (ImGui::Button("Mostrar painel", ImVec2(half, 0.0f)))
        {
            minimized = false;
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(!model.canStop);

        if (ImGui::Button("Parar", ImVec2(half, 0.0f)))
        {
            actions.stopSharing = true;
        }

        ImGui::EndDisabled();

        UiEndPanel();

        return;
    }

    // O painel fica no canto, pra previa aparecer atras.
    UiBeginCornerPanel("##aovivo", 380.0f);

    UiTitle("Transmitindo");

    // Minimizar (canto direito do titulo)
    {
        const char* label = "Minimizar";

        float width =
            ImGui::CalcTextSize(label).x +
            ImGui::GetStyle().FramePadding.x * 2.0f;

        ImGui::SameLine(ImGui::GetContentRegionMax().x - width);

        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + 4.0f * UiScale());

        if (ImGui::Button(label))
        {
            minimized = true;
        }
    }

    if (!model.sharingLabel.empty())
    {
        std::string sharing =
            model.sharingLabel +
            (model.windowAudioOnly
                ? "  ·  som só desta janela"
                : "  ·  som do computador");

        UiMutedText(sharing.c_str());
    }

    if (ImGui::Button(
            "Trocar o que está sendo compartilhado",
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                0.0f)))
    {
        actions.openPicker = true;
    }

    ImGui::Separator();


    // --------------------------------------------------------
    // Como as pessoas entram: pela sala, codigo da sessao ou
    // convite
    // --------------------------------------------------------

    if (model.inRoom)
    {
        UiMutedText(
            "Quem está na sala vê sua transmissão na lista e "
            "escolhe assistir."
        );

        if (model.roomPreview)
        {
            if (ImGui::Checkbox(
                    "Mostrar prévia na lista da sala",
                    model.roomPreview))
            {
                actions.roomPreviewChanged = true;
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Uma imagem pequena da sua transmissão, "
                    "atualizada a cada 15 s,\npara a sala ver o "
                    "que você mostra antes de assistir.");
            }
        }

        if (UiPrimaryButton("Voltar para a sala"))
        {
            actions.goHome = true;
        }
    }
    else if (model.sessionPending)
    {
        UiFieldLabel("Código da sessão");

        UiMutedText(
            (model.sessionStatus + UiBusyDots()).c_str()
        );

        UiMutedText(
            "Na primeira transmissão depois de um tempo "
            "parado, o servidor pode levar até 1 minuto."
        );
    }
    else if (!model.sessionCode.empty())
    {
        UiFieldLabel("Código da sessão");

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            UiColorSuccess());

        UiTitle(model.sessionCode.c_str());

        ImGui::PopStyleColor();

        if (CopyButton(
                "Copiar código",
                "##copiarcodigo",
                codeCopiedAt))
        {
            ImGui::SetClipboardText(
                model.sessionCode.c_str());
        }

        UiMutedText(
            "Passe este código para quem vai assistir."
        );
    }
    else
    {
        UiWrappedColored(
            UiColorError(),
            "Sem servidor de sessões no momento."
        );

        UiMutedText(
            "Envie este convite para quem vai assistir:"
        );

        UiWrappedColored(
            UiColorSuccess(),
            model.invite.c_str()
        );

        if (CopyButton(
                "Copiar convite",
                "##copiarconvite",
                inviteCopiedAt))
        {
            ImGui::SetClipboardText(
                model.invite.c_str());
        }

        // (so necessaria entre redes diferentes)
        UiFieldLabel("Resposta de quem assiste");

        const char* applyLabel = "Aplicar";

        float applyWidth =
            ImGui::CalcTextSize(applyLabel).x +
            ImGui::GetStyle().FramePadding.x * 2.0f;

        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x -
            applyWidth -
            ImGui::GetStyle().ItemSpacing.x
        );

        bool submitted =
            ImGui::InputTextWithHint(
                "##resposta",
                "SPV1...",
                model.replyInput,
                model.replyInputSize,
                ImGuiInputTextFlags_EnterReturnsTrue
            );

        ImGui::SameLine();

        if (ImGui::Button(applyLabel) || submitted)
        {
            actions.applyReply = true;
        }

        if (!model.replyStatus.empty())
        {
            UiWrappedColored(
                model.replyOk
                    ? UiColorSuccess()
                    : UiColorError(),
                model.replyStatus.c_str()
            );
        }

        if (!model.localIp.empty())
        {
            UiMutedText(
                ("Na mesma rede também dá para conectar pelo "
                 "IP " + model.localIp + ".").c_str()
            );
        }
    }

    ImGui::Separator();


    // --------------------------------------------------------
    // Quem esta assistindo
    // --------------------------------------------------------

    if (model.viewers > 0)
    {
        std::string watching =
            "Assistindo agora: " +
            std::to_string(model.viewers) +
            " de " +
            std::to_string(model.maxViewers);

        UiWrappedColored(
            UiColorSuccess(),
            watching.c_str()
        );

        if (!model.uploadLabel.empty())
        {
            UiMutedText(model.uploadLabel.c_str());
        }

        if (model.uploadWarning)
        {
            UiWrappedColored(
                UiColorError(),
                "Upload alto para a maioria das internets. Se a "
                "imagem travar para quem assiste, baixe a "
                "qualidade abaixo."
            );
        }
    }
    else
    {
        UiMutedText("Ninguém assistindo ainda.");
    }


    // --------------------------------------------------------
    // Qualidade (da pra trocar com a transmissao rolando)
    // --------------------------------------------------------

    if (model.qualityIndex &&
        model.fpsIndex)
    {
        ImGui::Separator();

        // "Nativa" por extenso e longo demais pra meia largura.
        static const char* const shortQualities[] =
        {
            "480p",
            "720p",
            "1080p",
            "Nativa"
        };

        // Duas colunas: Qualidade | FPS (rotulos numa linha,
        // listas na de baixo - assim ficam alinhados).
        float spacing =
            ImGui::GetStyle().ItemSpacing.x;

        float half =
            (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

        float secondX =
            ImGui::GetCursorPosX() + half + spacing;

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            UiColorMuted());

        ImGui::TextUnformatted("Qualidade");

        ImGui::SameLine(secondX);

        ImGui::TextUnformatted("FPS");

        ImGui::PopStyleColor();

        // Cola os rotulos nas listas (como UiFieldLabel).
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() -
            ImGui::GetStyle().ItemSpacing.y * 0.6f);

        ImGui::SetNextItemWidth(half);

        if (ImGui::Combo(
                "##qualidade",
                model.qualityIndex,
                shortQualities,
                4))
        {
            actions.qualityChanged = true;
        }

        ImGui::SameLine(secondX);

        ImGui::SetNextItemWidth(half);

        if (ImGui::Combo(
                "##fps",
                model.fpsIndex,
                FPS_OPTIONS,
                3))
        {
            actions.qualityChanged = true;
        }

        if (model.viewers > 0)
        {
            UiMutedText(
                "Ao trocar, a imagem de quem assiste pisca por "
                "um instante."
            );
        }
    }


    // --------------------------------------------------------
    // Detalhes tecnicos
    // --------------------------------------------------------

    if (model.capturing)
    {
        std::string details =
            "Capturando " +
            std::to_string(model.captureWidth) +
            " x " +
            std::to_string(model.captureHeight) +
            "\nEnviando em " +
            model.qualityLabel;

        UiMutedText(details.c_str());
    }

    if (model.symmetricNat)
    {
        UiWrappedColored(
            UiColorError(),
            "Sua rede usa NAT simétrico: quem estiver fora da "
            "sua rede local pode não conseguir conectar."
        );
    }

    ImGui::Spacing();

    ImGui::BeginDisabled(!model.canStop);

    if (ImGui::Button(
            "Parar de transmitir",
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                0.0f)))
    {
        actions.stopSharing = true;
    }

    ImGui::EndDisabled();

    UiEndPanel();
}


// ============================================================
// Seletor: o que compartilhar
// ============================================================

namespace
{

// Corta o texto com "..." pra caber na largura.
std::string Ellipsize(
    const std::string& text,
    float maxWidth)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
        return text;

    std::string cut = text;

    while (!cut.empty())
    {
        // Volta um caractere inteiro (UTF-8 pode ter varios
        // bytes por caractere).
        do
        {
            cut.pop_back();
        }
        while (!cut.empty() &&
               (static_cast<unsigned char>(cut.back()) & 0xC0) ==
                   0x80);

        if (!cut.empty())
        {
            cut.pop_back();
        }

        std::string candidate = cut + "...";

        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
            return candidate;
    }

    return "...";
}


// Cartao do seletor: miniatura (16:9, a imagem inteira dentro,
// com borda se a proporcao for outra) + titulo + linha cinza.
// Devolve true no clique.
bool PickerCard(
    const PickerEntry& entry,
    int id,
    float width)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    const float scale = UiScale();

    const float pad = 6.0f * scale;

    const float thumbHeight =
        (width - pad * 2.0f) * 9.0f / 16.0f;

    const float lineHeight =
        ImGui::GetTextLineHeight();

    const float height =
        pad + thumbHeight + pad * 0.8f +
        lineHeight * 2.0f + pad;

    ImVec2 start =
        ImGui::GetCursorScreenPos();

    ImGui::PushID(id);

    bool clicked =
        ImGui::InvisibleButton(
            "##cartao",
            ImVec2(width, height));

    bool hovered =
        ImGui::IsItemHovered();

    ImGui::PopID();

    ImDrawList* draw =
        ImGui::GetWindowDrawList();

    ImVec2 end(start.x + width, start.y + height);

    float rounding = style.FrameRounding;

    draw->AddRectFilled(
        start,
        end,
        ImGui::GetColorU32(
            hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
        rounding);

    if (hovered)
    {
        draw->AddRect(
            start,
            end,
            ImGui::GetColorU32(UiColorAccent()),
            rounding,
            0,
            2.0f * scale);
    }


    // --------------------------------------------------------
    // Miniatura
    // --------------------------------------------------------

    ImVec2 thumbMin(start.x + pad, start.y + pad);

    ImVec2 thumbMax(
        start.x + width - pad,
        start.y + pad + thumbHeight);

    draw->AddRectFilled(
        thumbMin,
        thumbMax,
        IM_COL32(8, 9, 12, 255),
        rounding * 0.6f);

    float boxWidth = thumbMax.x - thumbMin.x;
    float boxHeight = thumbMax.y - thumbMin.y;

    if (entry.thumbnail &&
        entry.thumbnailWidth > 0.0f &&
        entry.thumbnailHeight > 0.0f)
    {
        float fit =
            boxWidth / entry.thumbnailWidth;

        if (entry.thumbnailHeight * fit > boxHeight)
        {
            fit = boxHeight / entry.thumbnailHeight;
        }

        float w = entry.thumbnailWidth * fit;
        float h = entry.thumbnailHeight * fit;

        ImVec2 imageMin(
            thumbMin.x + (boxWidth - w) * 0.5f,
            thumbMin.y + (boxHeight - h) * 0.5f);

        draw->AddImage(
            reinterpret_cast<ImTextureID>(entry.thumbnail),
            imageMin,
            ImVec2(imageMin.x + w, imageMin.y + h));
    }
    else
    {
        const char* text =
            entry.thumbnailPending ? "Carregando..." :
            entry.minimized ? "Minimizada" :
            "Sem prévia";

        ImVec2 textSize =
            ImGui::CalcTextSize(text);

        draw->AddText(
            ImVec2(
                thumbMin.x + (boxWidth - textSize.x) * 0.5f,
                thumbMin.y + (boxHeight - textSize.y) * 0.5f),
            ImGui::GetColorU32(UiColorMuted()),
            text);
    }


    // --------------------------------------------------------
    // Textos
    // --------------------------------------------------------

    float textX = start.x + pad;

    float textWidth = width - pad * 2.0f;

    float textTop = thumbMax.y + pad * 0.8f;

    std::string title =
        Ellipsize(entry.title, textWidth);

    std::string detail =
        Ellipsize(
            entry.minimized
                ? entry.detail + "  ·  minimizada"
                : entry.detail,
            textWidth);

    draw->AddText(
        ImVec2(textX, textTop),
        ImGui::GetColorU32(ImGuiCol_Text),
        title.c_str());

    draw->AddText(
        ImVec2(textX, textTop + lineHeight),
        ImGui::GetColorU32(UiColorMuted()),
        detail.c_str());

    if (hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // Titulo cortado: o tooltip mostra inteiro.
        if (title != entry.title)
        {
            ImGui::SetTooltip("%s", entry.title.c_str());
        }
    }

    return clicked;
}


// Grade de cartoes. Devolve o indice clicado (-1 = nenhum).
int PickerGrid(
    const std::vector<PickerEntry>& entries,
    int idBase,
    int columns)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    float available =
        ImGui::GetContentRegionAvail().x;

    float cardWidth =
        (available - style.ItemSpacing.x * (columns - 1)) /
        static_cast<float>(columns);

    int clicked = -1;

    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        if (i % columns != 0)
        {
            ImGui::SameLine();
        }

        if (PickerCard(entries[i], idBase + i, cardWidth))
        {
            clicked = i;
        }
    }

    return clicked;
}

} // namespace


void DrawClientPickerScreen(
    ClientPickerModel& model,
    ClientScreenActions& actions)
{
    UiBeginCenteredPanel("##seletor", 820.0f);

    UiTitle("O que você quer compartilhar?");

    if (!model.error.empty())
    {
        UiWrappedColored(
            UiColorError(),
            model.error.c_str()
        );
    }

    if (model.offerAdmin)
    {
        if (UiPrimaryButton("Abrir como administrador"))
        {
            actions.relaunchAsAdmin = true;
        }

        if (model.switching)
        {
            UiMutedText(
                "A transmissão atual será encerrada e um código "
                "novo será gerado."
            );
        }

        ImGui::Spacing();
    }


    // --------------------------------------------------------
    // Telas e janelas, em cartoes com miniatura (rolavel)
    // --------------------------------------------------------

    const std::vector<PickerEntry>& monitors =
        *model.monitors;

    const std::vector<PickerEntry>& windows =
        *model.windows;

    // Tres cartoes por linha; em janela estreita, dois.
    int columns =
        ImGui::GetContentRegionAvail().x >= 600.0f * UiScale()
        ? 3
        : 2;

    // A area ocupa o que sobrar da janela (com limites), pra o
    // painel inteiro caber mesmo em janelas baixas.
    float reserved =
        (300.0f + (model.error.empty() ? 0.0f : 80.0f) +
         (model.offerAdmin ? (model.switching ? 90.0f : 60.0f) : 0.0f)) *
        UiScale();

    float listHeight =
        ImGui::GetMainViewport()->WorkSize.y - reserved;

    float minList = 220.0f * UiScale();
    float maxList = 620.0f * UiScale();

    if (listHeight < minList) listHeight = minList;
    if (listHeight > maxList) listHeight = maxList;

    ImGui::BeginChild(
        "##opcoes",
        ImVec2(0.0f, listHeight),
        ImGuiChildFlags_None
    );

    UiFieldLabel("Telas inteiras");

    ImGui::Spacing();

    int monitorClicked =
        PickerGrid(monitors, 0, columns);

    if (monitorClicked >= 0)
    {
        actions.pickMonitor = monitorClicked;
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f * UiScale()));

    UiFieldLabel("Janelas");

    ImGui::Spacing();

    if (windows.empty())
    {
        UiMutedText("Nenhuma janela aberta.");
    }

    int windowClicked =
        PickerGrid(windows, 1000, columns);

    if (windowClicked >= 0)
    {
        actions.pickWindow = windowClicked;
    }

    ImGui::EndChild();

    ImGui::Spacing();


    // --------------------------------------------------------
    // Som
    // --------------------------------------------------------

    ImGui::BeginDisabled(!model.windowAudioSupported);

    ImGui::Checkbox(
        "Ao compartilhar uma janela, transmitir só o som dela",
        model.windowAudioOnly
    );

    ImGui::EndDisabled();

    if (!model.windowAudioSupported)
    {
        UiMutedText(
            "Disponível a partir do Windows 11. Aqui vai o som "
            "do computador."
        );
    }
    else
    {
        UiMutedText(
            "Em navegadores, vai o som do navegador inteiro "
            "(todas as abas)."
        );
    }


    // --------------------------------------------------------
    // Botoes
    // --------------------------------------------------------

    const ImGuiStyle& style = ImGui::GetStyle();

    float half =
        (ImGui::GetContentRegionAvail().x -
         style.ItemSpacing.x) * 0.5f;

    if (ImGui::Button(
            "Atualizar lista",
            ImVec2(half, 0.0f)))
    {
        actions.refreshPicker = true;
    }

    ImGui::SameLine();

    if (ImGui::Button(
            model.switching ? "Cancelar" : "Voltar",
            ImVec2(half, 0.0f)))
    {
        actions.cancelPicker = true;
    }

    UiEndPanel();
}


// ============================================================
// ffmpeg ausente ou quebrado
// ============================================================

void DrawClientFfmpegProblemScreen(
    const std::string& problem,
    ClientScreenActions& actions)
{
    UiFfmpegProblemPanel(
        problem.c_str(),
        "O FFmpeg é o programa que codifica o vídeo. Sem ele "
        "não há transmissão.",
        actions.openDownloadPage,
        actions.recheckFfmpeg
    );
}
