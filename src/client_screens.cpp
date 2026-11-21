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

    // O painel fica no canto, pra previa aparecer atras.
    UiBeginCornerPanel("##aovivo", 380.0f);

    UiTitle("Transmitindo");

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
    // Como as pessoas entram: codigo da sessao ou convite
    // --------------------------------------------------------

    if (model.sessionPending)
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


// Item da lista: titulo + linha cinza embaixo, ocupando a
// largura toda. Devolve true no clique.
bool PickerItem(
    const PickerEntry& entry,
    int id)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    float width =
        ImGui::GetContentRegionAvail().x;

    float lineHeight =
        ImGui::GetTextLineHeight();

    float height =
        lineHeight * 2.0f +
        style.FramePadding.y * 1.4f;

    ImVec2 start =
        ImGui::GetCursorScreenPos();

    ImGui::PushID(id);

    bool clicked =
        ImGui::Selectable(
            "##item",
            false,
            ImGuiSelectableFlags_None,
            ImVec2(width, height)
        );

    ImGui::PopID();

    ImDrawList* draw =
        ImGui::GetWindowDrawList();

    float textX =
        start.x + style.FramePadding.x;

    float textWidth =
        width - style.FramePadding.x * 2.0f;

    std::string title =
        Ellipsize(entry.title, textWidth);

    std::string detail =
        Ellipsize(entry.detail, textWidth);

    float textTop =
        start.y + style.FramePadding.y * 0.7f;

    draw->AddText(
        ImVec2(textX, textTop),
        ImGui::GetColorU32(ImGuiCol_Text),
        title.c_str()
    );

    draw->AddText(
        ImVec2(
            textX,
            textTop + lineHeight),
        ImGui::GetColorU32(UiColorMuted()),
        detail.c_str()
    );

    return clicked;
}

} // namespace


void DrawClientPickerScreen(
    ClientPickerModel& model,
    ClientScreenActions& actions)
{
    UiBeginCenteredPanel("##seletor", 560.0f);

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
    // Telas
    // --------------------------------------------------------

    UiFieldLabel("Telas inteiras");

    const std::vector<PickerEntry>& monitors =
        *model.monitors;

    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(
            ImGui::GetStyle().ItemSpacing.x,
            2.0f * UiScale())
    );

    for (int i = 0;
         i < static_cast<int>(monitors.size());
         ++i)
    {
        if (PickerItem(monitors[i], i))
        {
            actions.pickMonitor = i;
        }
    }

    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0.0f, 4.0f * UiScale()));


    // --------------------------------------------------------
    // Janelas (rolavel)
    // --------------------------------------------------------

    UiFieldLabel("Janelas");

    const std::vector<PickerEntry>& windows =
        *model.windows;

    // A lista ocupa o que sobrar da janela (com limites), pra
    // o painel inteiro caber mesmo em janelas baixas.
    float reserved =
        (470.0f + (model.error.empty() ? 0.0f : 80.0f) +
         (model.offerAdmin ? (model.switching ? 90.0f : 60.0f) : 0.0f) +
         40.0f * static_cast<float>(monitors.size() > 1
             ? monitors.size() - 1
             : 0)) *
        UiScale();

    float listHeight =
        ImGui::GetMainViewport()->WorkSize.y - reserved;

    float minList = 110.0f * UiScale();
    float maxList = 320.0f * UiScale();

    if (listHeight < minList) listHeight = minList;
    if (listHeight > maxList) listHeight = maxList;

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(6.0f * UiScale(), 6.0f * UiScale())
    );

    ImGui::BeginChild(
        "##janelas",
        ImVec2(0.0f, listHeight),
        ImGuiChildFlags_Borders |
        ImGuiChildFlags_AlwaysUseWindowPadding
    );

    ImGui::PopStyleVar();

    // Itens colados um no outro (o destaque ao passar o mouse
    // ja separa visualmente).
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(
            ImGui::GetStyle().ItemSpacing.x,
            2.0f * UiScale())
    );

    if (windows.empty())
    {
        UiMutedText("Nenhuma janela aberta.");
    }

    for (int i = 0;
         i < static_cast<int>(windows.size());
         ++i)
    {
        if (PickerItem(windows[i], 1000 + i))
        {
            actions.pickWindow = i;
        }
    }

    ImGui::PopStyleVar();

    ImGui::EndChild();


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
