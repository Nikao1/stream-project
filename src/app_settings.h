#pragma once

#include <windows.h>

#include <string>

// ============================================================
// CONFIGURACOES SALVAS
// ============================================================
//
// Guarda as escolhas do usuario (qualidade, FPS, volume,
// tamanho da janela...) num .ini em
//
//   %APPDATA%\Colae\<arquivo>
//
// - por usuario do Windows e fora da pasta do programa, entao
// sobrevive a trocar de versao (descompactar o .zip novo em
// outro lugar). Se a pasta nao puder ser criada, tudo vira
// no-op e o programa usa os valores padrao.
// ============================================================

class AppSettings
{
public:
    // fileName: ex. L"client.ini"
    explicit AppSettings(
        const wchar_t* fileName
    );

    int GetInt(
        const wchar_t* key,
        int defaultValue
    ) const;

    void SetInt(
        const wchar_t* key,
        int value
    );

    std::wstring GetString(
        const wchar_t* key,
        const wchar_t* defaultValue
    ) const;

    void SetString(
        const wchar_t* key,
        const std::wstring& value
    );

    // Posicao/tamanho da janela da ultima vez. So aplica se o
    // retangulo ainda cair num monitor (o notebook pode estar
    // sem o monitor externo hoje). outShowCmd = SW_SHOWNORMAL
    // ou SW_SHOWMAXIMIZED, pra passar ao ShowWindow.
    bool RestoreWindowPlacement(
        HWND window,
        int& outShowCmd
    ) const;

    // placement: o de GetWindowPlacement (ou o guardado antes
    // de entrar em tela cheia).
    void SaveWindowPlacement(
        const WINDOWPLACEMENT& placement
    );

    // Outro arquivo na mesma pasta (%APPDATA%\Colae).
    // Vazio se a pasta nao existe.
    std::wstring SiblingPath(
        const wchar_t* fileName) const
    {
        size_t slash = m_path.find_last_of(L'\\');

        if (slash == std::wstring::npos)
            return std::wstring();

        return m_path.substr(0, slash + 1) + fileName;
    }

private:
    std::wstring m_path;
};
