#include "receiver_screens.h"

#include <algorithm>

#include "imgui.h"
#include "ui_widgets.h"


// ============================================================
// ffmpeg ausente ou quebrado
// ============================================================


void DrawVoiceSpeakers(
    const std::vector<std::string>& names,
    bool myMicMuted)
{
    if (names.empty() && !myMicMuted)
        return;

    ImGui::SetNextWindowPos(
        ImVec2(12.0f * UiScale(), 12.0f * UiScale()));

    ImGui::SetNextWindowBgAlpha(0.55f);

    ImGui::Begin(
        "##falando",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs);

    float icon = ImGui::GetTextLineHeight();

    if (myMicMuted)
    {
        float top = ImGui::GetCursorPosY();

        ImGui::SetCursorPosY(top + icon * 0.1f);

        UiMicIcon(icon, IM_COL32(239, 68, 68, 255), true);

        if (!names.empty())
        {
            ImGui::SameLine();
        }
    }

    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine();
        }

        float avatar = icon * 1.2f;

        float top = ImGui::GetCursorPosY();

        UiAvatar(names[i].c_str(), avatar, false, true);

        ImGui::SameLine();

        ImGui::SetCursorPosY(
            top + (avatar - ImGui::GetTextLineHeight()) * 0.5f);

        ImGui::TextUnformatted(names[i].c_str());
    }

    ImGui::End();
}

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
        UiSpinner(8.0f * UiScale());

        ImGui::SameLine();

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

    UiLogo(ImGui::GetFrameHeight() * 1.7f);

    ImGui::SameLine();

    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + 6.0f * UiScale());

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
        UiSpinner(8.0f * UiScale());

        ImGui::SameLine();

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
// Chat
// ============================================================

bool DrawChatBox(
    const char* id,
    const std::vector<ChatLine>& lines,
    char* input,
    size_t inputSize,
    float height,
    bool focusInput)
{
    ImGui::PushID(id);

    const ImGuiStyle& style = ImGui::GetStyle();

    float inputHeight = ImGui::GetFrameHeight();

    ImGui::BeginChild(
        "##mensagens",
        ImVec2(0.0f, height - inputHeight - style.ItemSpacing.y),
        ImGuiChildFlags_Borders);

    if (lines.empty())
    {
        UiMutedText("Nenhuma mensagem ainda.");
    }

    for (const ChatLine& line : lines)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            line.mine ? UiColorAccent() : UiColorMuted());

        ImGui::TextUnformatted(line.name.c_str());

        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushTextWrapPos(0.0f);

        ImGui::TextUnformatted(line.text.c_str());

        ImGui::PopTextWrapPos();
    }

    // Mensagem nova: rola pro fim (se a pessoa nao estiver
    // lendo algo mais antigo la em cima).
    // (contagem guardada por caixa - a da sala e a do video sao
    // separadas)
    ImGuiStorage* storage = ImGui::GetStateStorage();

    ImGuiID countKey = ImGui::GetID("##quantas");

    int lastCount = storage->GetInt(countKey, -1);

    if (static_cast<int>(lines.size()) != lastCount &&
        (lastCount < 0 ||
         ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f))
    {
        ImGui::SetScrollHereY(1.0f);
    }

    storage->SetInt(countKey, static_cast<int>(lines.size()));

    ImGui::EndChild();

    const char* sendLabel = "Enviar";

    float sendWidth =
        ImGui::CalcTextSize(sendLabel).x +
        style.FramePadding.x * 2.0f;

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x -
        sendWidth -
        style.ItemSpacing.x);

    if (focusInput)
    {
        ImGui::SetKeyboardFocusHere();
    }

    bool submitted =
        ImGui::InputTextWithHint(
            "##escrever",
            "Escreva uma mensagem",
            input,
            inputSize,
            ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();

    bool clicked = ImGui::Button(sendLabel);

    bool send =
        (submitted || clicked) &&
        input[0] != '\0';

    // Depois de enviar com Enter, o cursor continua no campo.
    if (submitted)
    {
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::PopID();

    return send;
}


// ============================================================
// Avisos
// ============================================================

void DrawToasts(
    const std::vector<Toast>& toasts)
{
    const double lifetime = 5.0;

    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    float margin = 16.0f * UiScale();

    float y =
        viewport->WorkPos.y + viewport->WorkSize.y - margin;

    double now = ImGui::GetTime();

    int index = 0;

    // Os mais novos embaixo.
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it)
    {
        double age = now - it->shownAt;

        if (age > lifetime || age < 0.0)
            continue;

        float alpha =
            age < 4.3
            ? 1.0f
            : static_cast<float>((lifetime - age) / 0.7);

        std::string id =
            "##aviso" + std::to_string(index++);

        ImGui::SetNextWindowPos(
            ImVec2(
                viewport->WorkPos.x + viewport->WorkSize.x - margin,
                y),
            ImGuiCond_Always,
            ImVec2(1.0f, 1.0f));

        ImGui::SetNextWindowBgAlpha(0.95f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

        ImGui::Begin(
            id.c_str(),
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs);

        ImGui::PushTextWrapPos(320.0f * UiScale());

        ImGui::TextUnformatted(it->text.c_str());

        ImGui::PopTextWrapPos();

        y -= ImGui::GetWindowHeight() + 8.0f * UiScale();

        ImGui::End();

        ImGui::PopStyleVar();

        // No maximo 4 de uma vez.
        if (index >= 4)
            break;
    }
}


bool DrawVideoChat(
    const std::vector<ChatLine>& lines,
    char* input,
    size_t inputSize,
    bool focusInput,
    bool& outClose)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    float margin = 16.0f * UiScale();

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + margin,
            viewport->WorkPos.y + viewport->WorkSize.y - margin),
        ImGuiCond_Always,
        ImVec2(0.0f, 1.0f));

    ImGui::SetNextWindowSize(
        ImVec2(340.0f * UiScale(), 0.0f),
        ImGuiCond_Always);

    ImGui::SetNextWindowBgAlpha(0.90f);

    ImGui::Begin(
        "##chatvideo",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

    ImGui::AlignTextToFramePadding();

    ImGui::TextUnformatted("Chat da sala");

    ImGui::PopStyleColor();

    float closeSize = ImGui::GetFrameHeight();

    ImGui::SameLine(
        ImGui::GetContentRegionMax().x - closeSize);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    if (ImGui::Button("×##fecharchat", ImVec2(closeSize, closeSize)))
    {
        outClose = true;
    }

    ImGui::PopStyleColor();

    bool send =
        DrawChatBox(
            "videochat",
            lines,
            input,
            inputSize,
            220.0f * UiScale(),
            focusInput);

    ImGui::End();

    return send;
}


// ============================================================
// Sala
// ============================================================

namespace
{

// Pessoa: bolinha + nome (+ linha cinza).
void PersonRow(
    const std::string& name,
    const std::string& detail,
    bool live,
    float rightReserve,
    bool speaking = false)
{
    float avatar = ImGui::GetFrameHeight() * 1.15f;

    UiAvatar(name.c_str(), avatar, live, speaking);

    ImGui::SameLine();

    ImGui::BeginGroup();

    float textWidth =
        ImGui::GetContentRegionAvail().x - rightReserve;

    ImGui::PushTextWrapPos(
        ImGui::GetCursorPosX() + (std::max)(40.0f, textWidth));

    if (detail.empty())
    {
        // Centraliza o nome na altura da bolinha.
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() +
            (avatar - ImGui::GetTextLineHeight()) * 0.5f);
    }

    ImGui::TextUnformatted(name.c_str());

    if (!detail.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

        ImGui::TextUnformatted(detail.c_str());

        ImGui::PopStyleColor();
    }

    ImGui::PopTextWrapPos();

    ImGui::EndGroup();
}

// Secao "Voz" da sala.
void DrawVoiceSection(
    const RoomScreenModel& model,
    RoomScreenActions& actions)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    UiFieldLabel("Voz");

    ImGui::Spacing();

    if (model.voiceJoining)
    {
        UiSpinner(8.0f * UiScale());

        ImGui::SameLine();

        UiMutedText((std::string("Entrando na voz") + UiBusyDots()).c_str());
    }
    else if (!model.voiceJoined)
    {
        if (UiPrimaryButton("Entrar na voz"))
        {
            actions.voiceJoin = true;
        }

        UiMutedText(
            "Converse com a sala por aqui mesmo. A voz vai direto "
            "entre vocês, criptografada.");
    }
    else
    {
        // [microfone]  [sair da voz]
        float half =
            (ImGui::GetContentRegionAvail().x -
             style.ItemSpacing.x) * 0.5f;

        float icon = ImGui::GetTextLineHeight();

        const char* micLabel =
            model.voiceMuted
            ? "      Ligar microfone##mic"
            : "      Silenciar##mic";

        ImVec4 micColor =
            model.voiceMuted
            ? UiColorError()
            : ImGui::GetStyleColorVec4(ImGuiCol_Text);

        ImGui::PushStyleColor(ImGuiCol_Text, micColor);

        if (ImGui::Button(micLabel, ImVec2(half, 0.0f)))
        {
            actions.voiceToggleMute = true;
        }

        ImGui::PopStyleColor();

        // Icone por cima do botao, a esquerda.
        ImVec2 buttonMin = ImGui::GetItemRectMin();

        UiMicIconAt(
            ImVec2(
                buttonMin.x + style.FramePadding.x * 1.5f,
                buttonMin.y + style.FramePadding.y),
            icon,
            ImGui::GetColorU32(micColor),
            model.voiceMuted);

        ImGui::SameLine();

        if (ImGui::Button("Sair da voz", ImVec2(half, 0.0f)))
        {
            actions.voiceLeave = true;
        }

        // Nivel do microfone (verde quando a voz esta saindo).
        ImGui::PushStyleColor(
            ImGuiCol_PlotHistogram,
            model.voiceTransmitting
            ? UiColorSuccess()
            : UiColorMuted());

        ImGui::ProgressBar(
            (std::min)(1.0f, model.voiceMicLevel * 4.0f),
            ImVec2(-1.0f, 4.0f * UiScale()),
            "");

        ImGui::PopStyleColor();

        // Modo de falar
        ImGui::AlignTextToFramePadding();

        UiMutedText("Falar:");

        ImGui::SameLine();

        ImGui::SetNextItemWidth(-1.0f);

        int ptt = model.voicePushToTalk;

        if (ptt < 0 || ptt >= VOICE_PTT_OPTION_COUNT)
        {
            ptt = 0;
        }

        if (ImGui::Combo(
                "##ptt",
                &ptt,
                VOICE_PTT_OPTION_NAMES,
                VOICE_PTT_OPTION_COUNT))
        {
            actions.voicePushToTalk = ptt;
        }

        if (model.voiceMuted)
        {
            UiMutedText("Seu microfone está desligado.");
        }
        else if (ptt != 0 && !model.voiceTransmitting)
        {
            // "Segurar Mouse 4" -> "Mouse 4"
            std::string key = VOICE_PTT_OPTION_NAMES[ptt];

            if (key.rfind("Segurar ", 0) == 0)
            {
                key = key.substr(8);
            }

            UiMutedText(
                ("Segure " + key +
                 " para falar (funciona com o jogo aberto).").c_str());
        }

        if (!model.voiceEchoCancel)
        {
            UiWrappedColored(
                UiColorMuted(),
                "Este Windows não ofereceu cancelamento de eco: "
                "use fone de ouvido para os outros não ouvirem "
                "a própria voz de volta.");
        }
    }

    if (!model.voiceError.empty())
    {
        UiWrappedColored(UiColorError(), model.voiceError.c_str());
    }
}

} // namespace


const char* const VOICE_PTT_OPTION_NAMES[VOICE_PTT_OPTION_COUNT] =
{
    "Falar livre",
    "Segurar Mouse 4",
    "Segurar Mouse 5",
    "Segurar Alt direito",
    "Segurar Ctrl direito"
};


void DrawRoomScreen(
    RoomScreenModel& model,
    RoomScreenActions& actions)
{
    static double codeCopiedAt = 0.0;

    const ImGuiStyle& style = ImGui::GetStyle();

    UiBeginCenteredPanel("##sala", 860.0f);

    // Duas colunas em janelas largas; uma so nas estreitas.
    bool twoColumns =
        ImGui::GetContentRegionAvail().x >= 640.0f * UiScale();


    // --------------------------------------------------------
    // Cabecalho: nome da sala + copiar codigo
    // --------------------------------------------------------

    UiTitle(("Sala " + model.code).c_str());

    {
        bool recentlyCopied =
            codeCopiedAt > 0.0 &&
            ImGui::GetTime() - codeCopiedAt < 2.0;

        const char* copyLabel =
            recentlyCopied
            ? "Copiado!##copiarsala"
            : "Copiar código##copiarsala";

        float copyWidth =
            ImGui::CalcTextSize("Copiar código").x +
            style.FramePadding.x * 4.0f;

        ImGui::SameLine(
            ImGui::GetContentRegionMax().x - copyWidth);

        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + 4.0f * UiScale());

        if (ImGui::Button(copyLabel, ImVec2(copyWidth, 0.0f)))
        {
            ImGui::SetClipboardText(model.code.c_str());

            codeCopiedAt = ImGui::GetTime();
        }
    }

    UiMutedText(
        ("Passe o código para quem vai entrar  ·  cabem até " +
         std::to_string(model.maxMembers) + " pessoas").c_str());

    if (model.offline)
    {
        UiWrappedColored(
            UiColorError(),
            "Sem resposta do servidor da sala. Verifique sua "
            "internet - quem você já está assistindo continua."
        );
    }

    ImGui::Spacing();

    bool table =
        twoColumns &&
        ImGui::BeginTable(
            "##salacolunas",
            2,
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_BordersInnerV);

    if (table)
    {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextColumn();
    }


    // --------------------------------------------------------
    // Quem esta transmitindo
    // --------------------------------------------------------

    UiFieldLabel("Transmitindo agora");

    ImGui::Spacing();

    if (model.presenters.empty())
    {
        UiMutedText(
            "Ninguém está transmitindo ainda. Quando alguém "
            "começar, aparece aqui.");
    }

    const float buttonWidth =
        ImGui::CalcTextSize("Assistindo").x +
        style.FramePadding.x * 4.0f;

    for (size_t i = 0; i < model.presenters.size(); ++i)
    {
        const RoomPresenterEntry& entry =
            model.presenters[i];

        ImGui::PushID(static_cast<int>(i));

        float rowTop = ImGui::GetCursorPosY();

        PersonRow(
            entry.isMe ? entry.name + " (você)" : entry.name,
            entry.label,
            true,
            buttonWidth + style.ItemSpacing.x);

        float rowBottom = ImGui::GetCursorPosY();

        // Botao alinhado a direita, no meio da linha.
        ImGui::SameLine(
            ImGui::GetContentRegionMax().x - buttonWidth);

        ImGui::SetCursorPosY(
            rowTop +
            ((rowBottom - rowTop) - style.ItemSpacing.y -
             ImGui::GetFrameHeight()) * 0.5f);

        if (entry.isMe)
        {
            ImGui::BeginDisabled();

            ImGui::Button("Você", ImVec2(buttonWidth, 0.0f));

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

            if (UiPrimaryButtonSized(
                    "Assistir",
                    ImVec2(buttonWidth, 0.0f)))
            {
                actions.watch = static_cast<int>(i);
            }

            ImGui::EndDisabled();
        }

        ImGui::SetCursorPosY(rowBottom);

        ImGui::PopID();

        ImGui::Spacing();
    }

    if (model.connecting)
    {
        UiSpinner(8.0f * UiScale());

        ImGui::SameLine();

        UiMutedText(
            ("Conectando a " + model.connectingTo +
             UiBusyDots()).c_str());
    }

    if (!model.notice.empty())
    {
        UiMutedText(model.notice.c_str());
    }

    if (!model.error.empty())
    {
        UiWrappedColored(
            UiColorError(),
            model.error.c_str());
    }

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::Spacing();


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

        UiWrappedColored(UiColorSuccess(), text.c_str());

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
    else if (UiPrimaryButton("Transmitir minha tela"))
    {
        actions.openShare = true;
    }

    ImGui::Spacing();

    if (ImGui::Button(
            "Sair da sala",
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
    {
        actions.leave = true;
    }


    // --------------------------------------------------------
    // Pessoas + chat (segunda coluna, ou embaixo)
    // --------------------------------------------------------

    if (table)
    {
        ImGui::TableNextColumn();
    }
    else
    {
        ImGui::Spacing();
        ImGui::Separator();
    }

    UiFieldLabel(
        ("Na sala (" + std::to_string(model.members.size()) +
         ")").c_str());

    ImGui::Spacing();

    for (size_t i = 0; i < model.members.size(); ++i)
    {
        bool live =
            i < model.memberLive.size() &&
            model.memberLive[i];

        MemberVoice voice =
            i < model.memberVoice.size()
            ? model.memberVoice[i]
            : MemberVoice::Out;

        bool isMe =
            i < model.memberIsMe.size() &&
            model.memberIsMe[i];

        const char* detail = "";

        switch (voice)
        {
        case MemberVoice::Connecting: detail = "conectando a voz..."; break;
        case MemberVoice::In:         detail = "na voz"; break;
        case MemberVoice::Speaking:   detail = "falando"; break;
        default: break;
        }

        ImGui::PushID(static_cast<int>(1000 + i));

        ImGui::BeginGroup();

        PersonRow(
            model.members[i],
            detail,
            live,
            0.0f,
            voice == MemberVoice::Speaking);

        ImGui::EndGroup();

        // Volume de cada pessoa: clique na linha.
        bool adjustable =
            !isMe &&
            model.voiceJoined &&
            voice != MemberVoice::Out &&
            i < model.memberVolume.size();

        if (adjustable)
        {
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Clique para ajustar o volume");
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
                ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                ImGui::OpenPopup("##volume");
            }

            if (ImGui::BeginPopup("##volume"))
            {
                ImGui::TextUnformatted(model.members[i].c_str());

                float percent = model.memberVolume[i] * 100.0f;

                ImGui::SetNextItemWidth(200.0f * UiScale());

                if (ImGui::SliderFloat(
                        "##vol", &percent, 0.0f, 200.0f, "%.0f%%"))
                {
                    actions.volumeMember = static_cast<int>(i);
                    actions.volumeValue = percent / 100.0f;
                }

                if (ImGui::Button("Volume normal"))
                {
                    actions.volumeMember = static_cast<int>(i);
                    actions.volumeValue = 1.0f;
                }

                ImGui::EndPopup();
            }
        }

        ImGui::PopID();
    }

    ImGui::Spacing();

    DrawVoiceSection(model, actions);

    ImGui::Spacing();

    UiFieldLabel("Chat");

    if (model.chatInput &&
        DrawChatBox(
            "salachat",
            model.chat,
            model.chatInput,
            model.chatInputSize,
            (twoColumns ? 230.0f : 180.0f) * UiScale(),
            false))
    {
        actions.sendChat = true;
    }

    if (table)
    {
        ImGui::EndTable();
    }

    UiEndPanel();
}


void DrawRoomWaitingScreen(
    const std::string& presenterName,
    const std::string& status,
    const std::string& versionWarning,
    bool& outBackToRoom)
{
    UiBeginCenteredPanel("##salaaguardando", 440.0f);

    UiAvatar(
        presenterName.c_str(),
        ImGui::GetFrameHeight() * 1.6f,
        true);

    ImGui::SameLine();

    ImGui::BeginGroup();

    UiTitle(presenterName.c_str());

    UiMutedText("está transmitindo");

    ImGui::EndGroup();

    ImGui::Spacing();

    UiSpinner(10.0f * UiScale());

    ImGui::SameLine();

    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + 2.0f * UiScale());

    UiMutedText(
        (status.empty()
            ? std::string("Aguardando a imagem chegar")
            : status).c_str());

    if (!versionWarning.empty())
    {
        UiWrappedColored(
            UiColorError(),
            versionWarning.c_str());
    }

    ImGui::Spacing();

    if (ImGui::Button(
            "Voltar para a sala",
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
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


// Corta o texto com "..." pra caber na largura (sem quebrar um
// caractere UTF-8 no meio).
std::string FitText(
    const std::string& text,
    float maxWidth)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
        return text;

    std::string cut = text;

    while (!cut.empty())
    {
        do
        {
            cut.pop_back();
        }
        while (!cut.empty() &&
               (static_cast<unsigned char>(cut.back()) & 0xC0) ==
                   0x80);

        std::string candidate = cut + "...";

        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
            return candidate;
    }

    return "...";
}


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

    // Na sala: chat, trocar de tela e voltar pra lista
    if (model.inRoom)
    {
        std::string chatLabel =
            model.unreadChat > 0
            ? "Chat (" + std::to_string(model.unreadChat) + ")"
            : std::string("Chat");

        if (model.unreadChat > 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UiColorAccent());
        }

        if (ImGui::Button(
                (chatLabel + "##chat").c_str(),
                ImVec2(half, 0.0f)))
        {
            actions.toggleChat = true;
        }

        if (model.unreadChat > 0)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();

        if (ImGui::Button(
                "Voltar à sala",
                ImVec2(half, 0.0f)))
        {
            actions.backToRoom = true;
        }

        if (model.voiceJoined)
        {
            if (model.voiceMuted)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorError());
            }

            if (ImGui::Button(
                    model.voiceMuted
                    ? "Microfone mudo  ·  ligar##vozov"
                    : "Silenciar microfone##vozov",
                    ImVec2(rowWidth, 0.0f)))
            {
                actions.toggleVoiceMute = true;
            }

            if (model.voiceMuted)
            {
                ImGui::PopStyleColor();
            }
        }

        if (!model.otherPresenters.empty())
        {
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

            ImGui::TextUnformatted("Trocar de tela");

            ImGui::PopStyleColor();

            for (size_t i = 0;
                 i < model.otherPresenters.size();
                 ++i)
            {
                ImGui::PushID(static_cast<int>(i));

                const std::string& full =
                    model.otherPresenters[i];

                std::string label =
                    FitText(
                        full,
                        rowWidth - style.FramePadding.x * 2.0f);

                if (ImGui::Button(
                        (label + "##troca").c_str(),
                        ImVec2(rowWidth, 0.0f)))
                {
                    actions.switchTo = static_cast<int>(i);
                }

                if (label != full &&
                    ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", full.c_str());
                }

                ImGui::PopID();
            }
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
