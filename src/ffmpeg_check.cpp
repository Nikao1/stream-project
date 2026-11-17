#include "ffmpeg_check.h"

#include <windows.h>

#include <iostream>


bool CheckFfmpegAvailable(
    std::string& outProblem)
{
    // A saida do "ffmpeg -version" nao interessa, so se ele
    // roda e termina com sucesso - entao vai tudo pro NUL.
    SECURITY_ATTRIBUTES sa{};

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE nul =
        CreateFileW(
            L"NUL",
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

    if (nul == INVALID_HANDLE_VALUE)
    {
        // Sem como testar - nao bloqueia o programa por isso.
        return true;
    }

    STARTUPINFOA startupInfo{};

    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = nul;
    startupInfo.hStdOutput = nul;
    startupInfo.hStdError = nul;

    PROCESS_INFORMATION processInfo{};

    char commandLine[] = "ffmpeg -version";

    // Sem janela de erro do Windows se faltar alguma DLL do
    // ffmpeg: queremos o codigo de saida, nao um popup.
    UINT previousMode =
        SetErrorMode(
            SEM_FAILCRITICALERRORS |
            SEM_NOGPFAULTERRORBOX
        );

    BOOL created =
        CreateProcessA(
            nullptr,
            commandLine,
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo
        );

    DWORD createError = GetLastError();

    SetErrorMode(previousMode);

    CloseHandle(nul);

    if (!created)
    {
        if (createError == ERROR_FILE_NOT_FOUND ||
            createError == ERROR_PATH_NOT_FOUND)
        {
            outProblem =
                "o ffmpeg.exe nao foi encontrado";
        }
        else
        {
            outProblem =
                "o Windows nao conseguiu executar o "
                "ffmpeg.exe (erro " +
                std::to_string(createError) +
                ") - arquivo corrompido ou incompleto";
        }

        return false;
    }

    DWORD waitResult =
        WaitForSingleObject(
            processInfo.hProcess,
            8000
        );

    DWORD exitCode = 1;

    bool ok = false;

    if (waitResult == WAIT_OBJECT_0 &&
        GetExitCodeProcess(
            processInfo.hProcess,
            &exitCode) &&
        exitCode == 0)
    {
        ok = true;
    }
    else if (waitResult != WAIT_OBJECT_0)
    {
        TerminateProcess(
            processInfo.hProcess,
            1
        );

        outProblem =
            "o ffmpeg.exe nao respondeu";
    }
    else
    {
        outProblem =
            "o ffmpeg.exe encontrado nao funciona nesta "
            "maquina (codigo de saida " +
            std::to_string(exitCode) +
            ")";
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return ok;
}


void PrintFfmpegHelp(
    const std::string& problem)
{
    std::cout
        << "\n========================================\n"
        << "PROBLEMA COM O FFMPEG\n"
        << "========================================\n"
        << "Motivo: " << problem << ".\n\n"
        << "Sem o ffmpeg nao ha video. Como resolver:\n"
        << " 1. Baixe \"ffmpeg-release-essentials.zip\" em\n"
        << "    https://www.gyan.dev/ffmpeg/builds/\n"
        << " 2. Extraia e copie o ffmpeg.exe da pasta \"bin\"\n"
        << "    para a MESMA pasta deste programa.\n\n"
        << "O arquivo certo tem dezenas de MB. Se o seu tem\n"
        << "poucos KB, e so um atalho (comum quando o ffmpeg\n"
        << "foi instalado por Chocolatey, Scoop ou winget) e\n"
        << "nao funciona fora do computador de origem.\n"
        << "========================================\n\n";
}
