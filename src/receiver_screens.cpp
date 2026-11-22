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
    bool& outClose,
    bool rightSide)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    float margin = 16.0f * UiScale();

    ImGui::SetNextWindowPos(
        ImVec2(
            rightSide
            ? viewport->WorkPos.x + viewport->WorkSize.x - margin
            : viewport->WorkPos.x + margin,
            viewport->WorkPos.y + viewport->WorkSize.y - margin),
        ImGuiCond_Always,
        ImVec2(rightSide ? 1.0f : 0.0f, 1.0f));

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

// Pessoa na lista da sala / painel: bolinha (anel vermelho =
// transmitindo, verde = falando), nome, situacao na voz. Com a
// voz ligada, clicar abre o volume dessa pessoa. Devolve true
// quando o volume mudou (em outVolume).
bool MemberRow(
    const std::string& name,
    bool live,
    MemberVoice voice,
    bool isMe,
    bool voiceJoined,
    float volume,
    float& outVolume)
{
    const char* detail = "";

    switch (voice)
    {
    case MemberVoice::Connecting: detail = "conectando a voz..."; break;
    case MemberVoice::In:         detail = "na voz"; break;
    case MemberVoice::Speaking:   detail = "falando"; break;
    default: break;
    }

    std::string shown = name;

    std::string detailText = detail;

    // Silenciada por esta pessoa.
    if (!isMe && voice != MemberVoice::Out && volume <= 0.001f)
    {
        detailText = "silenciado por você";
    }

    ImGui::BeginGroup();

    PersonRow(
        shown,
        detailText,
        live,
        0.0f,
        voice == MemberVoice::Speaking);

    ImGui::EndGroup();

    bool adjustable =
        !isMe &&
        voiceJoined &&
        voice != MemberVoice::Out;

    if (!adjustable)
        return false;

    bool changed = false;

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
        ImGui::TextUnformatted(name.c_str());

        float percent = volume * 100.0f;

        ImGui::SetNextItemWidth(220.0f * UiScale());

        if (ImGui::SliderFloat(
                "##vol", &percent, 0.0f, 200.0f, "%.0f%%"))
        {
            outVolume = percent / 100.0f;
            changed = true;
        }

        bool silenced = volume <= 0.001f;

        float half =
            (220.0f * UiScale() -
             ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (ImGui::Button(
                silenced ? "Ativar som" : "Silenciar",
                ImVec2(half, 0.0f)))
        {
            outVolume = silenced ? 1.0f : 0.0f;
            changed = true;
        }

        ImGui::SameLine();

        if (ImGui::Button("Volume normal", ImVec2(half, 0.0f)))
        {
            outVolume = 1.0f;
            changed = true;
        }

        ImGui::EndPopup();
    }

    return changed;
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

        if (ImGui::SmallButton("Atalhos"))
        {
            actions.openShortcuts = true;
        }
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

        // Rotulos alinhados ("Microfone:" / "Ouvir em:" /
        // "Falar:")
        float labelWidth =
            ImGui::CalcTextSize("Microfone:").x +
            style.ItemSpacing.x;

        float fieldX = ImGui::GetCursorPosX() + labelWidth;

        auto deviceRow =
            [&](const char* label,
                const char* id,
                const std::vector<std::string>& names,
                int selected,
                int& outChoice)
            {
                ImGui::AlignTextToFramePadding();

                UiMutedText(label);

                ImGui::SameLine();

                ImGui::SetCursorPosX(fieldX);

                ImGui::SetNextItemWidth(-1.0f);

                if (selected < 0 ||
                    selected > static_cast<int>(names.size()))
                {
                    selected = 0;
                }

                const char* preview =
                    selected == 0
                    ? "Padrão do Windows"
                    : names[selected - 1].c_str();

                if (ImGui::BeginCombo(id, preview))
                {
                    // Acabou de abrir: atualiza a lista (alguem
                    // pode ter ligado um fone agora).
                    if (ImGui::IsWindowAppearing())
                    {
                        actions.voiceRefreshDevices = true;
                    }

                    if (ImGui::Selectable(
                            "Padrão do Windows", selected == 0))
                    {
                        outChoice = 0;
                    }

                    for (size_t i = 0; i < names.size(); ++i)
                    {
                        ImGui::PushID(static_cast<int>(i));

                        if (ImGui::Selectable(
                                names[i].c_str(),
                                selected == static_cast<int>(i) + 1))
                        {
                            outChoice = static_cast<int>(i) + 1;
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndCombo();
                }
            };

        deviceRow(
            "Microfone:", "##mic",
            model.microphones, model.microphone,
            actions.voiceMicrophone);

        deviceRow(
            "Ouvir em:", "##saida",
            model.speakers, model.speaker,
            actions.voiceSpeaker);

        // Atalhos de voz (configurados na tela de atalhos)
        ImGui::AlignTextToFramePadding();

        UiMutedText("Falar:");

        ImGui::SameLine();

        ImGui::SetCursorPosX(fieldX);

        std::string talk =
            model.voicePttKey.empty()
            ? std::string("livre (microfone aberto)")
            : "segurando " + model.voicePttKey;

        const char* shortcutsLabel = "Atalhos";

        float shortcutsWidth =
            ImGui::CalcTextSize(shortcutsLabel).x +
            style.FramePadding.x * 2.0f;

        ImGui::TextUnformatted(talk.c_str());

        // (dentro da tabela, SameLine(x) nao conta a coluna)
        ImGui::SameLine();

        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() +
            ImGui::GetContentRegionAvail().x - shortcutsWidth);

        if (ImGui::Button(shortcutsLabel))
        {
            actions.openShortcuts = true;
        }

        if (!model.voiceMuteKey.empty())
        {
            UiMutedText(
                (model.voiceMuteKey +
                 " liga e desliga o microfone.").c_str());
        }

        if (model.voiceMuted)
        {
            UiMutedText("Seu microfone está desligado.");
        }
        else if (!model.voicePttKey.empty() && !model.voiceTransmitting)
        {
            UiMutedText(
                ("Segure " + model.voicePttKey +
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
        ("Passe o código inteiro para quem vai entrar  ·  cabem "
         "até " + std::to_string(model.maxMembers) + " pessoas").c_str());

    if (model.code.find('-') != std::string::npos)
    {
        UiMutedText(
            "A parte depois do traço é a chave da sala: nomes, chat "
            "e prévias são criptografados com ela, e o servidor não "
            "a conhece.");
    }

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

        if (entry.thumbnail &&
            entry.thumbWidth > 0.0f &&
            entry.thumbHeight > 0.0f)
        {
            // Previa no lugar da bolinha (clicar = assistir).
            float maxW = 136.0f * UiScale();
            float maxH = 77.0f * UiScale();

            float scale =
                (std::min)(maxW / entry.thumbWidth,
                           maxH / entry.thumbHeight);

            ImVec2 size(entry.thumbWidth * scale,
                        entry.thumbHeight * scale);

            ImVec2 pos = ImGui::GetCursorScreenPos();

            // Centraliza numa caixa fixa (linhas alinhadas).
            ImGui::InvisibleButton("##previa", ImVec2(maxW, maxH));

            bool hovered = ImGui::IsItemHovered();

            if (ImGui::IsItemClicked() &&
                !entry.isMe && !entry.watching && !model.connecting)
            {
                actions.watch = static_cast<int>(i);
            }

            ImDrawList* draw = ImGui::GetWindowDrawList();

            ImVec2 min(pos.x + (maxW - size.x) * 0.5f,
                       pos.y + (maxH - size.y) * 0.5f);

            ImVec2 max(min.x + size.x, min.y + size.y);

            draw->AddRectFilled(
                pos, ImVec2(pos.x + maxW, pos.y + maxH),
                IM_COL32(0, 0, 0, 255), 6.0f * UiScale());

            draw->AddImageRounded(
                reinterpret_cast<ImTextureID>(entry.thumbnail),
                min, max, ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, 255),
                6.0f * UiScale());

            draw->AddRect(
                pos, ImVec2(pos.x + maxW, pos.y + maxH),
                hovered
                    ? ImGui::GetColorU32(UiColorAccent())
                    : IM_COL32(239, 68, 68, 200),
                6.0f * UiScale(), 0, 1.5f * UiScale());

            if (hovered)
            {
                // Maior ao passar o mouse.
                ImGui::BeginTooltip();

                float big =
                    (std::min)(360.0f * UiScale() / entry.thumbWidth,
                               203.0f * UiScale() / entry.thumbHeight);

                ImGui::Image(
                    reinterpret_cast<ImTextureID>(entry.thumbnail),
                    ImVec2(entry.thumbWidth * big,
                           entry.thumbHeight * big));

                ImGui::EndTooltip();
            }

            ImGui::SameLine();

            ImGui::BeginGroup();

            float textWidth =
                ImGui::GetContentRegionAvail().x -
                buttonWidth - style.ItemSpacing.x;

            ImGui::PushTextWrapPos(
                ImGui::GetCursorPosX() + (std::max)(40.0f, textWidth));

            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + maxH * 0.18f);

            ImGui::TextUnformatted(
                (entry.isMe ? entry.name + " (você)" : entry.name).c_str());

            if (!entry.label.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

                ImGui::TextUnformatted(entry.label.c_str());

                ImGui::PopStyleColor();
            }

            ImGui::PopTextWrapPos();

            ImGui::EndGroup();

            // A linha tem pelo menos a altura da previa.
            float bottom = rowTop + maxH + style.ItemSpacing.y;

            if (ImGui::GetCursorPosY() < bottom)
            {
                ImGui::SetCursorPosY(bottom);
            }
        }
        else
        {
            PersonRow(
                entry.isMe ? entry.name + " (você)" : entry.name,
                entry.label,
                true,
                buttonWidth + style.ItemSpacing.x);
        }

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

        ImGui::PushID(static_cast<int>(1000 + i));

        float volume =
            i < model.memberVolume.size()
            ? model.memberVolume[i]
            : 1.0f;

        float newVolume = volume;

        if (MemberRow(
                model.members[i],
                live,
                voice,
                isMe,
                model.voiceJoined,
                volume,
                newVolume))
        {
            actions.volumeMember = static_cast<int>(i);
            actions.volumeValue = newVolume;
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

        if (model.peopleOpen)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UiColorAccent());
        }

        if (ImGui::Button(
                "Pessoas##pessoasov",
                ImVec2(model.voiceJoined ? half : rowWidth, 0.0f)))
        {
            actions.togglePeople = true;
        }

        if (model.peopleOpen)
        {
            ImGui::PopStyleColor();
        }

        if (model.voiceJoined)
        {
            ImGui::SameLine();

            if (model.voiceMuted)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorError());
            }

            if (ImGui::Button(
                    model.voiceMuted
                    ? "Ligar microfone##vozov"
                    : "Silenciar mic.##vozov",
                    ImVec2(half, 0.0f)))
            {
                actions.toggleVoiceMute = true;
            }

            if (model.voiceMuted)
            {
                ImGui::PopStyleColor();
            }
        }

        if (ImGui::Button("Atalhos##atalhosov", ImVec2(rowWidth, 0.0f)))
        {
            actions.openShortcuts = true;
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
        model.keyHint.empty()
        ? "setas: volume"
        : model.keyHint.c_str());

    ImGui::PopStyleColor();

    return EndTopRightWindow();
}


// ============================================================
// Painel "Pessoas" (sobre o video ou a transmissao)
// ============================================================

void DrawPeoplePanel(
    const PeoplePanelModel& model,
    PeoplePanelActions& actions)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    const ImGuiStyle& style = ImGui::GetStyle();

    float margin = 16.0f * UiScale();

    float width = 290.0f * UiScale();

    ImGui::SetNextWindowPos(
        ImVec2(
            model.rightSide
            ? viewport->WorkPos.x + viewport->WorkSize.x - margin
            : viewport->WorkPos.x + margin,
            viewport->WorkPos.y + margin + model.topOffset),
        ImGuiCond_Always,
        ImVec2(model.rightSide ? 1.0f : 0.0f, 0.0f));

    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);

    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGui::Begin(
        "##pessoas",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing);

    // Cabecalho + fechar
    ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

    ImGui::AlignTextToFramePadding();

    ImGui::TextUnformatted(
        ("Na sala (" + std::to_string(model.people.size()) +
         ")").c_str());

    ImGui::PopStyleColor();

    float closeSize = ImGui::GetFrameHeight();

    ImGui::SameLine(ImGui::GetContentRegionMax().x - closeSize);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    if (ImGui::Button("×##fecharpessoas", ImVec2(closeSize, closeSize)))
    {
        actions.close = true;
    }

    ImGui::PopStyleColor();

    // Lista (rola se a sala estiver cheia)
    float rowHeight = ImGui::GetFrameHeight() * 1.15f + style.ItemSpacing.y;

    float listHeight =
        (std::min)(
            static_cast<float>(model.people.size()) * rowHeight +
                style.ItemSpacing.y,
            viewport->WorkSize.y * 0.45f);

    ImGui::BeginChild(
        "##listapessoas",
        ImVec2(0.0f, listHeight),
        false);

    for (size_t i = 0; i < model.people.size(); ++i)
    {
        const PeopleEntry& person = model.people[i];

        ImGui::PushID(static_cast<int>(i));

        float newVolume = person.volume;

        if (MemberRow(
                person.isMe ? person.name + " (você)" : person.name,
                person.live,
                person.voice,
                person.isMe,
                model.voiceJoined,
                person.volume,
                newVolume))
        {
            actions.volumeMember = static_cast<int>(i);
            actions.volumeValue = newVolume;
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    ImGui::Separator();

    // Voz
    if (model.voiceJoining)
    {
        UiSpinner(7.0f * UiScale());

        ImGui::SameLine();

        UiMutedText((std::string("Entrando na voz") + UiBusyDots()).c_str());
    }
    else if (!model.voiceJoined)
    {
        if (UiPrimaryButton("Entrar na voz"))
        {
            actions.voiceJoin = true;
        }
    }
    else
    {
        float half =
            (ImGui::GetContentRegionAvail().x -
             style.ItemSpacing.x) * 0.5f;

        ImVec4 micColor =
            model.voiceMuted
            ? UiColorError()
            : ImGui::GetStyleColorVec4(ImGuiCol_Text);

        ImGui::PushStyleColor(ImGuiCol_Text, micColor);

        if (ImGui::Button(
                model.voiceMuted
                ? "      Ligar mic.##micp"
                : "      Silenciar##micp",
                ImVec2(half, 0.0f)))
        {
            actions.toggleMute = true;
        }

        ImGui::PopStyleColor();

        ImVec2 buttonMin = ImGui::GetItemRectMin();

        UiMicIconAt(
            ImVec2(
                buttonMin.x + style.FramePadding.x * 1.5f,
                buttonMin.y + style.FramePadding.y),
            ImGui::GetTextLineHeight(),
            ImGui::GetColorU32(micColor),
            model.voiceMuted);

        ImGui::SameLine();

        if (ImGui::Button("Sair da voz", ImVec2(half, 0.0f)))
        {
            actions.voiceLeave = true;
        }

        ImGui::PushStyleColor(
            ImGuiCol_PlotHistogram,
            model.voiceTransmitting ? UiColorSuccess() : UiColorMuted());

        ImGui::ProgressBar(
            (std::min)(1.0f, model.micLevel * 4.0f),
            ImVec2(-1.0f, 4.0f * UiScale()),
            "");

        ImGui::PopStyleColor();

        std::string hint;

        if (!model.pttKey.empty())
        {
            hint = "Segure " + model.pttKey + " para falar";
        }

        if (!model.muteKey.empty())
        {
            hint +=
                (hint.empty() ? "" : "  ·  ") +
                model.muteKey + ": microfone";
        }

        if (!hint.empty())
        {
            UiMutedText(hint.c_str());
        }
    }

    if (!model.voiceError.empty())
    {
        UiWrappedColored(UiColorError(), model.voiceError.c_str());
    }

    if (ImGui::SmallButton("Atalhos"))
    {
        actions.openShortcuts = true;
    }

    ImGui::End();
}


// ============================================================
// Barra da sala enquanto transmite
// ============================================================

float DrawShareRoomToolbar(
    bool peopleOpen,
    bool chatOpen,
    int unreadChat,
    bool voiceJoined,
    bool voiceMuted,
    ShareToolbarActions& actions)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    float margin = 16.0f * UiScale();

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x - margin,
            viewport->WorkPos.y + margin),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f));

    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGui::Begin(
        "##barrasala",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing);

    auto toggle =
        [](const char* label, bool on, bool& out)
        {
            if (on)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorAccent());
            }

            if (ImGui::Button(label))
            {
                out = true;
            }

            if (on)
            {
                ImGui::PopStyleColor();
            }
        };

    toggle("Pessoas", peopleOpen, actions.togglePeople);

    ImGui::SameLine();

    std::string chat =
        unreadChat > 0
        ? "Chat (" + std::to_string(unreadChat) + ")##chatbarra"
        : std::string("Chat##chatbarra");

    toggle(chat.c_str(), chatOpen || unreadChat > 0, actions.toggleChat);

    if (voiceJoined)
    {
        ImGui::SameLine();

        if (voiceMuted)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UiColorError());
        }

        if (ImGui::Button(
                voiceMuted
                ? "Ligar microfone##micbarra"
                : "Silenciar mic.##micbarra"))
        {
            actions.toggleMute = true;
        }

        if (voiceMuted)
        {
            ImGui::PopStyleColor();
        }
    }

    float height = ImGui::GetWindowHeight();

    ImGui::End();

    return height + 8.0f * UiScale();
}


// ============================================================
// Tela de atalhos
// ============================================================

void DrawShortcutsWindow(
    const ShortcutsModel& model,
    ShortcutsActions& actions)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    const ImGuiStyle& style = ImGui::GetStyle();

    float width =
        (std::min)(680.0f * UiScale(), viewport->WorkSize.x - 32.0f);

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));

    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);

    // Janela pequena: rola em vez de passar da tela.
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0f, 0.0f),
        ImVec2(width, viewport->WorkSize.y - 24.0f * UiScale()));

    ImGui::SetNextWindowFocus();

    ImGui::Begin(
        "##atalhos",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);

    UiTitle("Atalhos");

    {
        const char* label = "Fechar";

        float width =
            ImGui::CalcTextSize(label).x + style.FramePadding.x * 4.0f;

        ImGui::SameLine(ImGui::GetContentRegionMax().x - width);

        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + 4.0f * UiScale());

        if (UiPrimaryButtonSized(label, ImVec2(width, 0.0f)))
        {
            actions.close = true;
        }
    }

    UiMutedText(
        "Clique na tecla de um atalho e aperte a nova (vale "
        "botão lateral do mouse). Esc cancela, Backspace remove.");

    ImGui::Spacing();

    float keyWidth = 200.0f * UiScale();

    float clearWidth = ImGui::GetFrameHeight();

    for (size_t i = 0; i < model.rows.size(); ++i)
    {
        const ShortcutRow& row = model.rows[i];

        ImGui::PushID(static_cast<int>(i));

        ImGui::Separator();

        float rowTop = ImGui::GetCursorPosY();

        float textWidth =
            ImGui::GetContentRegionAvail().x -
            keyWidth - clearWidth - style.ItemSpacing.x * 3.0f;

        ImGui::BeginGroup();

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);

        ImGui::TextUnformatted(row.label.c_str());

        ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

        ImGui::TextWrapped("%s", row.help.c_str());

        ImGui::PopStyleColor();

        if (row.global)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UiColorAccent());

            ImGui::TextWrapped("Funciona com o jogo aberto.");

            ImGui::PopStyleColor();
        }

        ImGui::PopTextWrapPos();

        ImGui::EndGroup();

        float rowBottom = ImGui::GetCursorPosY();

        // Botao da tecla, centralizado na altura da linha
        float buttonY =
            rowTop +
            ((rowBottom - rowTop) - style.ItemSpacing.y -
             ImGui::GetFrameHeight()) * 0.5f;

        ImGui::SameLine(
            ImGui::GetContentRegionMax().x - keyWidth -
            clearWidth - style.ItemSpacing.x);

        ImGui::SetCursorPosY(buttonY);

        bool capturing = model.capturing == static_cast<int>(i);

        if (capturing)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UiColorAccent());

            if (ImGui::Button(
                    "Aperte uma tecla...##tecla",
                    ImVec2(keyWidth, 0.0f)))
            {
                actions.cancelCapture = true;
            }

            ImGui::PopStyleColor();
        }
        else
        {
            if (!row.hasKey)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());
            }

            if (ImGui::Button(
                    (row.keyName + "##tecla").c_str(),
                    ImVec2(keyWidth, 0.0f)))
            {
                actions.capture = static_cast<int>(i);
            }

            if (!row.hasKey)
            {
                ImGui::PopStyleColor();
            }
        }

        ImGui::SameLine();

        ImGui::SetCursorPosY(buttonY);

        ImGui::BeginDisabled(!row.hasKey || capturing);

        if (ImGui::Button("×##limpar", ImVec2(clearWidth, 0.0f)))
        {
            actions.clear = static_cast<int>(i);
        }

        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
            row.hasKey)
        {
            ImGui::SetTooltip("Remover este atalho");
        }

        ImGui::SetCursorPosY(rowBottom);

        ImGui::PopID();
    }

    ImGui::Separator();

    UiMutedText(
        "Fixos: setas para cima/baixo mudam o volume da "
        "transmissão; duplo clique no vídeo é tela cheia.");

    if (ImGui::Button("Restaurar padrões"))
    {
        actions.restoreDefaults = true;
    }

    ImGui::End();
}
