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

    if (!busy)
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
// Tela inicial do programa
// ============================================================

void DrawHomeScreen(
    HomeScreenModel& model,
    HomeScreenActions& actions)
{
    UiBeginCenteredPanel("##inicio", 440.0f);

    UiTitle("Stream Project");

    UiMutedText(
        "Tela e som direto de um computador para outro, sem "
        "passar por servidor."
    );

    ImGui::Spacing();

    ImGui::BeginDisabled(model.busy);

    UiFieldLabel("Seu nome");

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x);

    ImGui::InputTextWithHint(
        "##nome",
        "Como os outros vão te ver na sala",
        model.name,
        model.nameSize
    );

    ImGui::Spacing();

    if (UiPrimaryButton(
            model.busy ? "Aguarde..." : "Criar uma sala",
            !model.busy))
    {
        actions.createRoom = true;
    }

    ImGui::Spacing();

    UiFieldLabel("Ou entre com um código");

    const char* enterLabel = "Entrar";

    float enterWidth =
        ImGui::CalcTextSize(enterLabel).x +
        ImGui::GetStyle().FramePadding.x * 4.0f;

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x -
        enterWidth -
        ImGui::GetStyle().ItemSpacing.x
    );

    bool submitted =
        ImGui::InputTextWithHint(
            "##codigo",
            "Código da sala",
            model.code,
            model.codeSize,
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll
        );

    ImGui::SameLine();

    if (ImGui::Button(
            enterLabel,
            ImVec2(enterWidth, 0.0f)) ||
        submitted)
    {
        actions.enter = true;
    }

    ImGui::EndDisabled();

    if (model.busy)
    {
        UiMutedText(
            (model.status + UiBusyDots()).c_str()
        );

        UiMutedText(
            "Na primeira vez depois de um tempo parado, o "
            "servidor pode levar até 1 minuto para responder."
        );
    }
    else if (!model.error.empty())
    {
        UiWrappedColored(
            UiColorError(),
            model.error.c_str()
        );
    }
    else
    {
        UiMutedText(
            "Funciona também com o código de quem transmite "
            "sem sala."
        );
    }

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::BeginDisabled(model.busy);

    if (ImGui::Button(
            "Transmitir sem sala",
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                0.0f)))
    {
        actions.shareWithoutRoom = true;
    }

    ImGui::EndDisabled();

    UiEndPanel();
}


// ============================================================
// Sala
// ============================================================

void DrawRoomScreen(
    RoomScreenModel& model,
    RoomScreenActions& actions)
{
    static double codeCopiedAt = 0.0;

    UiBeginCenteredPanel("##sala", 480.0f);

    UiTitle(("Sala " + model.code).c_str());

    {
        bool recentlyCopied =
            codeCopiedAt > 0.0 &&
            ImGui::GetTime() - codeCopiedAt < 2.0;

        if (ImGui::Button(
                recentlyCopied
                    ? "Copiado!##copiarsala"
                    : "Copiar código da sala##copiarsala",
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    0.0f)))
        {
            ImGui::SetClipboardText(model.code.c_str());

            codeCopiedAt = ImGui::GetTime();
        }
    }

    UiMutedText(
        ("Passe este código para quem vai entrar. Cabem até " +
         std::to_string(model.maxMembers) + " pessoas.").c_str()
    );

    if (model.offline)
    {
        UiWrappedColored(
            UiColorError(),
            "Sem resposta do servidor da sala. Verifique sua "
            "internet - quem você já está assistindo continua."
        );
    }

    ImGui::Separator();


    // --------------------------------------------------------
    // Quem esta transmitindo
    // --------------------------------------------------------

    UiFieldLabel("Transmitindo agora");

    ImGui::Spacing();

    if (model.presenters.empty())
    {
        UiMutedText("Ninguém está transmitindo ainda.");
    }

    const float buttonWidth =
        ImGui::CalcTextSize("Assistindo").x +
        ImGui::GetStyle().FramePadding.x * 4.0f;

    for (size_t i = 0; i < model.presenters.size(); ++i)
    {
        const RoomPresenterEntry& entry =
            model.presenters[i];

        ImGui::PushID(static_cast<int>(i));

        float textWidth =
            ImGui::GetContentRegionAvail().x -
            buttonWidth -
            ImGui::GetStyle().ItemSpacing.x;

        ImGui::BeginGroup();

        ImGui::PushTextWrapPos(
            ImGui::GetCursorPosX() + textWidth);

        ImGui::TextUnformatted(
            entry.isMe
                ? (entry.name + " (você)").c_str()
                : entry.name.c_str());

        if (!entry.label.empty())
        {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                UiColorMuted());

            ImGui::TextUnformatted(entry.label.c_str());

            ImGui::PopStyleColor();
        }

        ImGui::PopTextWrapPos();

        ImGui::EndGroup();

        ImGui::SameLine(
            ImGui::GetContentRegionMax().x - buttonWidth);

        if (entry.isMe)
        {
            ImGui::BeginDisabled();

            ImGui::Button(
                "Você",
                ImVec2(buttonWidth, 0.0f));

            ImGui::EndDisabled();
        }
        else if (entry.watching)
        {
            ImGui::BeginDisabled();

            ImGui::Button(
                model.connecting ? "Conectando" : "Assistindo",
                ImVec2(buttonWidth, 0.0f));

            ImGui::EndDisabled();
        }
        else
        {
            ImGui::BeginDisabled(model.connecting);

            if (ImGui::Button(
                    "Assistir",
                    ImVec2(buttonWidth, 0.0f)))
            {
                actions.watch = static_cast<int>(i);
            }

            ImGui::EndDisabled();
        }

        ImGui::PopID();

        ImGui::Spacing();
    }

    if (model.connecting)
    {
        UiMutedText(
            ("Conectando a " + model.connectingTo +
             UiBusyDots()).c_str()
        );
    }

    if (!model.notice.empty())
    {
        UiMutedText(model.notice.c_str());
    }

    if (!model.error.empty())
    {
        UiWrappedColored(
            UiColorError(),
            model.error.c_str()
        );
    }

    ImGui::Separator();


    // --------------------------------------------------------
    // A transmissao desta pessoa
    // --------------------------------------------------------

    if (model.sharing)
    {
        std::string text =
            "Você está transmitindo: " +
            model.sharingLabel +
            "  ·  " +
            std::to_string(model.sharingViewers) +
            " assistindo";

        UiWrappedColored(
            UiColorSuccess(),
            text.c_str()
        );

        const ImGuiStyle& style = ImGui::GetStyle();

        float half =
            (ImGui::GetContentRegionAvail().x -
             style.ItemSpacing.x) * 0.5f;

        if (ImGui::Button(
                "Opções da transmissão",
                ImVec2(half, 0.0f)))
        {
            actions.openShare = true;
        }

        ImGui::SameLine();

        if (ImGui::Button(
                "Parar de transmitir",
                ImVec2(half, 0.0f)))
        {
            actions.stopShare = true;
        }
    }
    else
    {
        if (UiPrimaryButton("Transmitir minha tela"))
        {
            actions.openShare = true;
        }
    }


    // --------------------------------------------------------
    // Quem esta na sala
    // --------------------------------------------------------

    ImGui::Spacing();

    std::string people =
        "Na sala (" +
        std::to_string(model.members.size()) +
        "): ";

    for (size_t i = 0; i < model.members.size(); ++i)
    {
        if (i > 0)
            people += ", ";

        people += model.members[i];
    }

    UiMutedText(people.c_str());

    ImGui::Spacing();

    if (ImGui::Button(
            "Sair da sala",
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                0.0f)))
    {
        actions.leave = true;
    }

    UiEndPanel();
}


void DrawRoomWaitingScreen(
    const std::string& presenterName,
    const std::string& versionWarning,
    bool& outBackToRoom)
{
    UiBeginCenteredPanel("##salaaguardando", 440.0f);

    UiTitle(presenterName.c_str());

    UiMutedText(
        (std::string("Aguardando a imagem chegar") +
         UiBusyDots()).c_str()
    );

    if (!versionWarning.empty())
    {
        UiWrappedColored(
            UiColorError(),
            versionWarning.c_str()
        );
    }

    ImGui::Spacing();

    if (ImGui::Button(
            "Voltar para a sala",
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                0.0f)))
    {
        outBackToRoom = true;
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

    ImGui::TextUnformatted(
        model.inRoom && !model.presenterName.empty()
            ? model.presenterName.c_str()
            : "Opções");

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

    // Na sala: voltar pra lista (trocar de tela, transmitir...)
    if (model.inRoom)
    {
        if (ImGui::Button(
                "Voltar para a sala",
                ImVec2(rowWidth, 0.0f)))
        {
            actions.backToRoom = true;
        }
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

    if (model.recovered > 0)
    {
        stats +=
            "  ·  recuperados: " +
            std::to_string(model.recovered);
    }

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
