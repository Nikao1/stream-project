#include "crash_report.h"

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstdlib>
#include <exception>


namespace
{

wchar_t g_dumpFileName[MAX_PATH] = L"crash.dmp";


using MiniDumpWriteDumpFn =
    BOOL (WINAPI*)(
        HANDLE,
        DWORD,
        HANDLE,
        MINIDUMP_TYPE,
        PMINIDUMP_EXCEPTION_INFORMATION,
        PMINIDUMP_USER_STREAM_INFORMATION,
        PMINIDUMP_CALLBACK_INFORMATION);


// Escreve o .dmp. Carrega o dbghelp so na hora (ele vem em
// todo Windows), pra nao virar dependencia do programa.
void WriteDump(
    EXCEPTION_POINTERS* exception)
{
    HMODULE dbghelp =
        LoadLibraryW(L"dbghelp.dll");

    if (!dbghelp)
        return;

    MiniDumpWriteDumpFn writeDump =
        reinterpret_cast<MiniDumpWriteDumpFn>(
            reinterpret_cast<void*>(
                GetProcAddress(dbghelp, "MiniDumpWriteDump")));

    if (!writeDump)
        return;

    HANDLE file =
        CreateFileW(
            g_dumpFileName,
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

    if (file == INVALID_HANDLE_VALUE)
        return;

    MINIDUMP_EXCEPTION_INFORMATION info{};

    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;

    // Pilhas e dados das threads, sem a memoria inteira do
    // programa (que teria centenas de MB por causa dos frames
    // de video).
    writeDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        file,
        static_cast<MINIDUMP_TYPE>(
            MiniDumpWithThreadInfo |
            MiniDumpWithIndirectlyReferencedMemory),
        exception ? &info : nullptr,
        nullptr,
        nullptr
    );

    CloseHandle(file);
}


// Linha no log: codigo do erro e "arquivo.exe+deslocamento" do
// ponto onde aconteceu. Usa so funcoes simples (printf), que
// continuam funcionando com o programa em estado ruim.
void LogCrash(
    EXCEPTION_POINTERS* exception)
{
    if (!exception || !exception->ExceptionRecord)
    {
        std::fprintf(
            stdout,
            "\n*** CRASH: erro fatal (sem detalhes). Arquivo "
            "de diagnostico: %ls\n",
            g_dumpFileName);

        std::fflush(stdout);

        return;
    }

    const EXCEPTION_RECORD* record =
        exception->ExceptionRecord;

    void* address = record->ExceptionAddress;

    HMODULE module = nullptr;

    wchar_t moduleName[MAX_PATH] = L"?";

    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),
            &module))
    {
        wchar_t path[MAX_PATH]{};

        if (GetModuleFileNameW(module, path, MAX_PATH))
        {
            const wchar_t* name = path;

            for (const wchar_t* c = path; *c; ++c)
            {
                if (*c == L'\\' || *c == L'/')
                {
                    name = c + 1;
                }
            }

            lstrcpynW(moduleName, name, MAX_PATH);
        }
    }

    unsigned long long offset =
        module
        ? static_cast<unsigned long long>(
              reinterpret_cast<char*>(address) -
              reinterpret_cast<char*>(module))
        : reinterpret_cast<unsigned long long>(address);

    std::fprintf(
        stdout,
        "\n*** CRASH: excecao 0x%08lX em %ls+0x%llX. Arquivo "
        "de diagnostico: %ls\n",
        static_cast<unsigned long>(record->ExceptionCode),
        moduleName,
        offset,
        g_dumpFileName);

    // Acesso invalido a memoria: diz se foi leitura/escrita e
    // em qual endereco.
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2)
    {
        std::fprintf(
            stdout,
            "    (%s no endereco 0x%llX)\n",
            record->ExceptionInformation[0] == 0
                ? "leitura"
                : "escrita",
            static_cast<unsigned long long>(
                record->ExceptionInformation[1]));
    }

    std::fflush(stdout);
}


LONG WINAPI OnUnhandledException(
    EXCEPTION_POINTERS* exception)
{
    // Evita rodar duas vezes se o proprio relatorio falhar.
    static volatile LONG reporting = 0;

    if (InterlockedExchange(&reporting, 1) == 0)
    {
        LogCrash(exception);

        WriteDump(exception);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}


// Excecao de C++ que ninguem capturou (std::terminate).
void OnTerminate()
{
    std::fprintf(
        stdout,
        "\n*** CRASH: excecao de C++ nao tratada.\n");

    std::fflush(stdout);

    WriteDump(nullptr);

    std::abort();
}

} // namespace


void InstallCrashReporter(
    const wchar_t* dumpFileName)
{
    if (dumpFileName && *dumpFileName)
    {
        lstrcpynW(g_dumpFileName, dumpFileName, MAX_PATH);
    }

    SetUnhandledExceptionFilter(OnUnhandledException);

    std::set_terminate(OnTerminate);
}
