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

void DrawClientSetupScreen(
    ClientSetupModel& model,
    ClientScreenActions& actions)
{
    static const char* const qualities[] =
    {
        "480p",
        "720p",
        "1080p",
        "Nativa (a resolução da tela)"
    };

    static const char* const frameRates[] =
    {
        "60 quadros por segundo",
        "30 quadros por segundo",
        "15 quadros por segundo"
    };

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
        "Fluidez",
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
                : "Escolher tela e transmitir",
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
            "Em seguida o Windows pergunta qual tela ou janela "
            "você quer compartilhar."
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

    // Com a tela ja sendo capturada, o painel vai pro canto
    // pra previa aparecer atras; antes disso fica no centro.
    if (model.capturing)
    {
        UiBeginCornerPanel("##aovivo", 380.0f);
    }
    else
    {
        UiBeginCenteredPanel("##aovivo", 480.0f);
    }

    UiTitle(
        model.capturing
            ? "Transmitindo"
            : "Escolha o que compartilhar"
    );

    if (!model.capturing)
    {
        UiMutedText(
            "Selecione a tela ou janela na caixa do Windows que "
            "acabou de abrir."
        );

        if (ImGui::Button(
                "Abrir o seletor de novo",
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    0.0f)))
        {
            actions.openPicker = true;
        }

        ImGui::Separator();
    }


    // --------------------------------------------------------
    // Como as pessoas entram: codigo da sessao ou convite
    // --------------------------------------------------------

    if (!model.sessionCode.empty())
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
