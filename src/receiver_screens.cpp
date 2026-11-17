#include "receiver_screens.h"

#include "imgui.h"
#include "ui_widgets.h"


// ============================================================
// ffmpeg ausente ou quebrado
// ============================================================

void DrawFfmpegProblemScreen(
    const std::string& problem,
    ReceiverScreenActions& actions)
{
    UiFfmpegProblemPanel(
        problem.c_str(),
        "O FFmpeg é o programa que decodifica o vídeo. Sem ele "
        "não há imagem.",
        actions.openDownloadPage,
        actions.recheckFfmpeg
    );
}


// ============================================================
// Tela de conexao
// ============================================================

void DrawConnectScreen(
    char* input,
    size_t inputSize,
    bool busy,
    const std::string& status,
    const std::string& error,
    ReceiverScreenActions& actions)
{
    UiBeginCenteredPanel("##conectar", 440.0f);

    UiTitle("Assistir");

    UiMutedText(
        "Digite o código da sessão que a pessoa que está "
        "transmitindo te passou."
    );

    ImGui::Spacing();

    ImGui::BeginDisabled(busy);

    const char* pasteLabel = "Colar";

    float pasteWidth =
        ImGui::CalcTextSize(pasteLabel).x +
        ImGui::GetStyle().FramePadding.x * 2.0f;

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x -
        pasteWidth -
        ImGui::GetStyle().ItemSpacing.x
    );

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    bool submitted =
        ImGui::InputTextWithHint(
            "##alvo",
            "Código da sessão",
            input,
            inputSize,
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll
        );

    ImGui::SameLine();

    if (ImGui::Button(pasteLabel))
    {
        // Cola tirando espacos e quebras de linha (um
        // convite longo as vezes vem quebrado em duas).
        const char* clipboard =
            ImGui::GetClipboardText();

        size_t length = 0;

        for (const char* c = clipboard;
             c && *c && length + 1 < inputSize;
             ++c)
        {
            if (*c != ' ' && *c != '\t' &&
                *c != '\r' && *c != '\n')
            {
                input[length++] = *c;
            }
        }

        input[length] = '\0';
    }

    ImGui::EndDisabled();

    if (UiPrimaryButton(
            busy ? "Conectando..." : "Conectar",
            !busy) ||
        (submitted && !busy))
    {
        actions.connect = true;
    }

    if (busy)
    {
        UiMutedText(
            (status + UiBusyDots()).c_str()
        );

        UiMutedText(
            "Na primeira conexão depois de um tempo parado, "
            "o servidor pode levar até 1 minuto para "
            "responder."
        );
    }
    else if (!error.empty())
    {
        UiWrappedColored(
            UiColorError(),
            error.c_str()
        );
    }
    else
    {
        UiMutedText(
            "Também aceita um convite (texto que começa com "
            "SPH1.) ou o endereço IP de quem transmite."
        );
    }

    UiEndPanel();
}


// ============================================================
// Conectado, esperando o primeiro frame
// ============================================================

void DrawWaitingScreen(
    const std::string& reply,
    const std::string& versionWarning)
{
    UiBeginCenteredPanel("##aguardando", 440.0f);

    UiTitle("Conectando");

    UiMutedText(
        (std::string("Aguardando a transmissão começar") +
         UiBusyDots()).c_str()
    );

    UiMutedText(
        "Se demorar, confira se quem transmite já escolheu "
        "a tela que vai compartilhar."
    );

    if (!versionWarning.empty())
    {
        UiWrappedColored(
            UiColorError(),
            versionWarning.c_str()
        );
    }

    // Convite manual: a resposta que precisa voltar pra
    // quem transmite (so entre redes diferentes).
    if (!reply.empty())
    {
        ImGui::Separator();

        ImGui::PushTextWrapPos(0.0f);

        ImGui::TextUnformatted(
            "Se vocês estão em redes diferentes, envie esta "
            "resposta para quem transmite:"
        );

        ImGui::PopTextWrapPos();

        UiWrappedColored(
            UiColorSuccess(),
            reply.c_str()
        );

        if (ImGui::Button(
                "Copiar resposta",
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    0.0f)))
        {
            ImGui::SetClipboardText(reply.c_str());
        }
    }

    UiEndPanel();
}


// ============================================================
// Painel sobre o video
// ============================================================

namespace
{

// Comeca uma janela sem moldura encostada no canto superior
// direito, com a transparencia dada.
void BeginTopRightWindow(
    const char* id,
    float alpha)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    float margin = 16.0f * UiScale();

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x - margin,
            viewport->WorkPos.y + margin),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f)
    );

    ImGui::SetNextWindowBgAlpha(0.93f);

    ImGui::PushStyleVar(
        ImGuiStyleVar_Alpha,
        alpha
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(14.0f * UiScale(), 10.0f * UiScale())
    );

    ImGui::Begin(
        id,
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing
    );
}


bool EndTopRightWindow()
{
    bool hovered =
        ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows) ||
        ImGui::IsAnyItemActive();

    ImGui::End();

    ImGui::PopStyleVar(2);

    return hovered;
}


// Dois botoes lado a lado que funcionam como uma escolha
// (o escolhido fica destacado).
bool ToggleButton(
    const char* label,
    bool selected,
    float width)
{
    if (selected)
    {
        const ImVec4 accent = UiColorAccent();

        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
    }

    bool clicked =
        ImGui::Button(
            label,
            ImVec2(width, 0.0f));

    if (selected)
    {
        ImGui::PopStyleColor(3);
    }

    return clicked;
}

} // namespace


bool DrawStreamOverlay(
    StreamOverlayModel& model,
    StreamOverlayActions& actions)
{
    if (model.alpha <= 0.0f)
        return false;

    bool& collapsed = *model.collapsed;


    // --------------------------------------------------------
    // Painel fechado: so um botao pequeno pra reabrir
    // --------------------------------------------------------

    if (collapsed)
    {
        BeginTopRightWindow("##opcoesfechado", model.alpha);

        if (ImGui::Button("Opções"))
        {
            collapsed = false;
        }

        return EndTopRightWindow();
    }


    BeginTopRightWindow("##sobrevideo", model.alpha);

    const ImGuiStyle& style = ImGui::GetStyle();

    float rowWidth = 300.0f * UiScale();


    // --------------------------------------------------------
    // Cabecalho com o botao de fechar
    // --------------------------------------------------------

    ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

    // Alinha o texto com o centro do botao de fechar.
    ImGui::AlignTextToFramePadding();

    ImGui::TextUnformatted("Opções");

    ImGui::PopStyleColor();

    float closeSize =
        ImGui::GetFrameHeight();

    ImGui::SameLine(
        rowWidth - closeSize + style.WindowPadding.x
    );

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    if (ImGui::Button(
            "×##fechar",
            ImVec2(closeSize, closeSize)))
    {
        collapsed = true;
    }

    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Fechar (as teclas continuam funcionando)");
    }


    // --------------------------------------------------------
    // Volume
    // --------------------------------------------------------

    bool& muted = *model.muted;

    float& volume = *model.volume;

    float muteWidth =
        ImGui::CalcTextSize("Som desligado").x +
        style.FramePadding.x * 2.0f;

    if (muted)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            UiColorError());
    }

    if (ImGui::Button(
            muted ? "Som desligado" : "Som ligado",
            ImVec2(muteWidth, 0.0f)))
    {
        muted = !muted;
    }

    if (muted)
    {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    ImGui::SetNextItemWidth(
        rowWidth - muteWidth - style.ItemSpacing.x);

    int percent =
        static_cast<int>(volume * 100.0f + 0.5f);

    ImGui::BeginDisabled(muted);

    if (ImGui::SliderInt(
            "##volume",
            &percent,
            0,
            100,
            "%d%%"))
    {
        volume = percent / 100.0f;
    }

    ImGui::EndDisabled();


    // --------------------------------------------------------
    // Exibicao
    // --------------------------------------------------------

    bool& fill = *model.fillWindow;

    float half =
        (rowWidth - style.ItemSpacing.x) * 0.5f;

    if (ToggleButton("Imagem inteira", !fill, half))
    {
        fill = false;
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Mostra tudo; pode sobrar borda.");
    }

    ImGui::SameLine();

    if (ToggleButton("Preencher", fill, half))
    {
        fill = true;
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Sem bordas; corta um pouco das beiradas.");
    }

    if (ImGui::Button(
            model.fullscreen
                ? "Sair da tela cheia"
                : "Tela cheia",
            ImVec2(rowWidth, 0.0f)))
    {
        actions.toggleFullscreen = true;
    }


    // --------------------------------------------------------
    // Estatisticas e atalhos
    // --------------------------------------------------------

    std::string stats =
        std::to_string(model.width) +
        " x " +
        std::to_string(model.height) +
        "  ·  " +
        std::to_string(model.fps) +
        " FPS  ·  perdidos: " +
        std::to_string(model.lostNetwork);

    if (model.late > 0)
    {
        stats +=
            "  ·  atrasados: " +
            std::to_string(model.late);
    }

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        UiColorMuted());

    ImGui::TextUnformatted(stats.c_str());

    ImGui::TextUnformatted(
        "M: som  ·  setas: volume  ·  F11: tela cheia");

    ImGui::PopStyleColor();

    return EndTopRightWindow();
}
