// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

// ============================================================
// colae-assinar - assina os instaladores das atualizacoes
// ============================================================
//
// Uso (no Prompt/PowerShell):
//
//   colae-assinar gerar-chave <arquivo.chave>
//       Cria o par de chaves (uma vez so), protegido por uma senha
//       que ele pede. Mostra a chave PUBLICA, que vai em
//       src/update_key.h. O arquivo .chave e a chave PRIVADA
//       (criptografada): guarde com cuidado, fora do repositorio,
//       e faca uma copia de seguranca (perdeu = os programas ja
//       instalados nao aceitam mais atualizacoes automaticas).
//
//   colae-assinar proteger <arquivo.chave>
//       Poe senha numa chave criada sem senha (versao antiga desta
//       ferramenta). A chave continua a mesma.
//
//   colae-assinar assinar <arquivo.chave> <instalador.exe> <versao>
//       Pede a senha e mostra o "hash" e a "assinatura" do
//       instalador (as linhas do signaling/versao.txt). O
//       tools/publicar.ps1 chama isto.
//
// A chave privada fica cifrada (XChaCha20-Poly1305) com uma chave
// tirada da senha pelo Argon2id (64 MB, 3 passadas): sem a senha,
// o arquivo nao serve pra nada, e cada tentativa de adivinhar
// custa caro. Sem dependencias alem do Monocypher (o mesmo do
// programa).
// ============================================================

#include "update_signature.h"

#include "monocypher.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace
{

// Custo do Argon2id (gravado no arquivo, pra poder mudar depois).
constexpr uint32_t KDF_BLOCKS = 65536; // x 1 KB = 64 MB
constexpr uint32_t KDF_PASSES = 3;

constexpr size_t MIN_PASSWORD = 8;


// ------------------------------------------------------------
// Sistema (arquivos, aleatorio, senha)
// ------------------------------------------------------------

#ifdef _WIN32
using Path = std::wstring;

FILE* OpenFile(const Path& path, const char* mode)
{
    std::wstring wideMode(mode, mode + std::strlen(mode));

    return _wfopen(path.c_str(), wideMode.c_str());
}

bool ReplaceFile(const Path& from, const Path& to)
{
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::string Narrow(const Path& text)
{
    // Versoes e comandos sao ASCII.
    std::string out;

    for (wchar_t c : text)
        out.push_back(static_cast<char>(c));

    return out;
}

bool RandomBytes(uint8_t* out, size_t size)
{
    return BCRYPT_SUCCESS(BCryptGenRandom(
        nullptr, out, static_cast<ULONG>(size),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

// Le a senha direto do console, sem mostrar o que e digitado
// (funciona mesmo com a saida capturada pelo PowerShell).
std::string ReadPassword(const char* prompt)
{
    std::fprintf(stderr, "%s", prompt);
    std::fflush(stderr);

    HANDLE input = CreateFileW(
        L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

    if (input == INVALID_HANDLE_VALUE)
        return std::string();

    DWORD mode = 0;

    GetConsoleMode(input, &mode);

    SetConsoleMode(input, (mode & ~ENABLE_ECHO_INPUT) | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);

    wchar_t buffer[512]{};
    DWORD read = 0;

    ReadConsoleW(input, buffer, 511, &read, nullptr);

    SetConsoleMode(input, mode);

    CloseHandle(input);

    std::fprintf(stderr, "\n");

    std::wstring wide(buffer, read);

    SecureZeroMemory(buffer, sizeof(buffer));

    while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n'))
        wide.pop_back();

    if (wide.empty())
        return std::string();

    int size = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);

    std::string out(static_cast<size_t>(size), '\0');

    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &out[0], size, nullptr, nullptr);

    SecureZeroMemory(&wide[0], wide.size() * sizeof(wchar_t));

    return out;
}
#else
using Path = std::string;

FILE* OpenFile(const Path& path, const char* mode)
{
    return std::fopen(path.c_str(), mode);
}

bool ReplaceFile(const Path& from, const Path& to)
{
    return std::rename(from.c_str(), to.c_str()) == 0;
}

std::string Narrow(const Path& text)
{
    return text;
}

bool RandomBytes(uint8_t* out, size_t size)
{
    FILE* file = std::fopen("/dev/urandom", "rb");

    if (!file)
        return false;

    bool ok = std::fread(out, 1, size, file) == size;

    std::fclose(file);

    return ok;
}

// (Fora do Windows, so pra testes: le uma linha da entrada.)
std::string ReadPassword(const char* prompt)
{
    std::fprintf(stderr, "%s", prompt);

    char buffer[512]{};

    if (!std::fgets(buffer, sizeof(buffer), stdin))
        return std::string();

    std::string out(buffer);

    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();

    return out;
}
#endif


void Wipe(std::string& text)
{
    if (!text.empty())
        crypto_wipe(&text[0], text.size());

    text.clear();
}


bool ReadAll(const Path& path, std::vector<uint8_t>& out)
{
    FILE* file = OpenFile(path, "rb");

    if (!file)
        return false;

    uint8_t buffer[65536];

    size_t read = 0;

    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        out.insert(out.end(), buffer, buffer + read);

    std::fclose(file);

    return true;
}


bool Exists(const Path& path)
{
    FILE* file = OpenFile(path, "rb");

    if (!file)
        return false;

    std::fclose(file);

    return true;
}


// Valor de "nome=valor" no texto do arquivo da chave.
std::string Field(const std::string& text, const std::string& name)
{
    size_t start = 0;

    while (start < text.size())
    {
        size_t end = text.find('\n', start);

        if (end == std::string::npos)
            end = text.size();

        std::string line = text.substr(start, end - start);

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.compare(0, name.size() + 1, name + "=") == 0)
            return line.substr(name.size() + 1);

        start = end + 1;
    }

    return std::string();
}


// ------------------------------------------------------------
// Senha -> chave, e a chave privada cifrada
// ------------------------------------------------------------

bool DeriveKey(
    const std::string& password,
    const uint8_t salt[16],
    uint32_t blocks,
    uint32_t passes,
    uint8_t out[32])
{
    if (blocks < 8 || blocks > (1u << 20) || passes < 1 || passes > 16)
        return false;

    std::vector<uint8_t> work(static_cast<size_t>(blocks) * 1024);

    crypto_argon2_config config{};

    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = blocks;
    config.nb_passes = passes;
    config.nb_lanes = 1;

    crypto_argon2_inputs inputs{};

    inputs.pass = reinterpret_cast<const uint8_t*>(password.data());
    inputs.pass_size = static_cast<uint32_t>(password.size());
    inputs.salt = salt;
    inputs.salt_size = 16;

    crypto_argon2(out, 32, work.data(), config, inputs, crypto_argon2_no_extras);

    crypto_wipe(work.data(), work.size());

    return true;
}


// A chave publica entra como "dado associado": nao da pra trocar
// uma pela outra no arquivo sem a senha perceber.
std::string AssociatedData(const std::string& publicHex)
{
    return "colae-chave-2\n" + publicHex;
}


// Pede a senha duas vezes (chave nova / proteger).
std::string AskNewPassword()
{
    for (;;)
    {
        std::string first = ReadPassword("Crie uma senha para a chave (minimo 8 caracteres): ");

        if (first.size() < MIN_PASSWORD)
        {
            Wipe(first);

            std::fprintf(stderr, "Senha curta demais.\n");

            continue;
        }

        std::string second = ReadPassword("Repita a senha: ");

        bool same = first == second;

        Wipe(second);

        if (same)
            return first;

        Wipe(first);

        std::fprintf(stderr, "As senhas nao conferem. De novo.\n");
    }
}


// Grava a chave privada cifrada com a senha (formato 2). Escreve
// num arquivo temporario e troca no fim: se algo falhar no meio, o
// arquivo antigo continua inteiro.
bool WriteProtectedKey(
    const Path& path,
    const uint8_t secret[64],
    const std::string& password)
{
    uint8_t salt[16];
    uint8_t nonce[24];

    if (!RandomBytes(salt, sizeof(salt)) || !RandomBytes(nonce, sizeof(nonce)))
        return false;

    uint8_t key[32];

    if (!DeriveKey(password, salt, KDF_BLOCKS, KDF_PASSES, key))
        return false;

    std::string publicHex = UpdateToHex(secret + 32, 32);
    std::string ad = AssociatedData(publicHex);

    uint8_t cipher[64];
    uint8_t mac[16];

    crypto_aead_lock(
        cipher, mac, key, nonce,
        reinterpret_cast<const uint8_t*>(ad.data()), ad.size(),
        secret, 64);

    crypto_wipe(key, sizeof(key));

    Path temp = path;

#ifdef _WIN32
    temp += L".novo";
#else
    temp += ".novo";
#endif

    FILE* file = OpenFile(temp, "wb");

    if (!file)
        return false;

    std::fprintf(file,
        "formato=2\n"
        "kdf=argon2id,%u,%u\n"
        "sal=%s\n"
        "nonce=%s\n"
        "privada-cifrada=%s\n"
        "mac=%s\n"
        "publica=%s\n"
        "# Chave PRIVADA das atualizacoes do Colae, protegida por senha.\n"
        "# Nao compartilhe, nao suba pro GitHub e guarde uma copia de\n"
        "# seguranca. Sem a senha ela nao serve pra nada.\n",
        static_cast<unsigned>(KDF_BLOCKS), static_cast<unsigned>(KDF_PASSES),
        UpdateToHex(salt, sizeof(salt)).c_str(),
        UpdateToHex(nonce, sizeof(nonce)).c_str(),
        UpdateToHex(cipher, sizeof(cipher)).c_str(),
        UpdateToHex(mac, sizeof(mac)).c_str(),
        publicHex.c_str());

    bool ok = std::fflush(file) == 0;

    ok = std::fclose(file) == 0 && ok;

    return ok && ReplaceFile(temp, path);
}


enum class KeyResult
{
    Ok,
    Unreadable,
    WrongPassword
};

// Le a chave privada (pede a senha se estiver protegida).
// outPlain = estava sem senha (formato antigo).
KeyResult ReadKey(
    const Path& path,
    uint8_t secret[64],
    bool& outPlain)
{
    outPlain = false;

    std::vector<uint8_t> data;

    if (!ReadAll(path, data))
        return KeyResult::Unreadable;

    std::string text(data.begin(), data.end());

    if (Field(text, "formato") == "2")
    {
        std::string kdf = Field(text, "kdf");
        std::string publicHex = Field(text, "publica");

        unsigned blocks = 0;
        unsigned passes = 0;

        uint8_t salt[16];
        uint8_t nonce[24];
        uint8_t cipher[64];
        uint8_t mac[16];
        uint8_t publicKey[32];

        if (std::sscanf(kdf.c_str(), "argon2id,%u,%u", &blocks, &passes) != 2 ||
            !UpdateFromHex(Field(text, "sal"), salt, sizeof(salt)) ||
            !UpdateFromHex(Field(text, "nonce"), nonce, sizeof(nonce)) ||
            !UpdateFromHex(Field(text, "privada-cifrada"), cipher, sizeof(cipher)) ||
            !UpdateFromHex(Field(text, "mac"), mac, sizeof(mac)) ||
            !UpdateFromHex(publicHex, publicKey, sizeof(publicKey)))
        {
            return KeyResult::Unreadable;
        }

        std::string password = ReadPassword("Senha da chave: ");

        uint8_t key[32];

        bool derived = DeriveKey(password, salt, blocks, passes, key);

        Wipe(password);

        if (!derived)
            return KeyResult::Unreadable;

        std::string ad = AssociatedData(publicHex);

        int failed = crypto_aead_unlock(
            secret, mac, key, nonce,
            reinterpret_cast<const uint8_t*>(ad.data()), ad.size(),
            cipher, sizeof(cipher));

        crypto_wipe(key, sizeof(key));

        if (failed != 0)
            return KeyResult::WrongPassword;

        // A parte publica da chave decifrada tem que bater.
        if (std::memcmp(secret + 32, publicKey, 32) != 0)
        {
            crypto_wipe(secret, 64);

            return KeyResult::Unreadable;
        }

        return KeyResult::Ok;
    }

    // Formato antigo, sem senha: "privada=<128 hex>".
    if (UpdateFromHex(Field(text, "privada"), secret, 64))
    {
        outPlain = true;

        return KeyResult::Ok;
    }

    return KeyResult::Unreadable;
}


bool LoadKeyOrComplain(
    const Path& path,
    uint8_t secret[64],
    bool& outPlain)
{
    switch (ReadKey(path, secret, outPlain))
    {
    case KeyResult::Ok:
        return true;

    case KeyResult::WrongPassword:
        std::fprintf(stderr, "Senha errada.\n");
        return false;

    default:
        std::fprintf(stderr, "Nao consegui ler a chave privada (arquivo ausente ou danificado).\n");
        return false;
    }
}


// ------------------------------------------------------------
// Comandos
// ------------------------------------------------------------

int GenerateKey(const Path& path)
{
    if (Exists(path))
    {
        std::fprintf(stderr,
            "Esse arquivo ja existe - nao vou sobrescrever uma chave.\n");

        return 1;
    }

    std::string password = AskNewPassword();

    uint8_t seed[32];
    uint8_t secret[64];
    uint8_t publicKey[32];

    if (!RandomBytes(seed, sizeof(seed)))
    {
        Wipe(password);

        std::fprintf(stderr, "Falha ao gerar numeros aleatorios.\n");

        return 1;
    }

    crypto_eddsa_key_pair(secret, publicKey, seed);

    std::fprintf(stderr, "Protegendo a chave (leva alguns segundos)...\n");

    bool ok = WriteProtectedKey(path, secret, password);

    Wipe(password);
    crypto_wipe(secret, sizeof(secret));

    if (!ok)
    {
        std::fprintf(stderr, "Nao consegui criar o arquivo da chave.\n");

        return 1;
    }

    std::printf(
        "Chave criada (protegida pela sua senha).\n\n"
        "Chave PUBLICA (cole em src/update_key.h):\n\n"
        "    %s\n\n"
        "Guarde o arquivo e a senha com cuidado, e faca uma copia de\n"
        "seguranca do arquivo (ex.: num pendrive).\n",
        UpdateToHex(publicKey, sizeof(publicKey)).c_str());

    return 0;
}


int Protect(const Path& path)
{
    uint8_t secret[64];
    bool plain = false;

    if (!LoadKeyOrComplain(path, secret, plain))
        return 1;

    if (!plain)
    {
        crypto_wipe(secret, sizeof(secret));

        std::fprintf(stderr, "Essa chave ja tem senha: nada a fazer.\n");

        return 0;
    }

    std::string password = AskNewPassword();

    std::fprintf(stderr, "Protegendo a chave (leva alguns segundos)...\n");

    bool ok = WriteProtectedKey(path, secret, password);

    std::string publicHex = UpdateToHex(secret + 32, 32);

    Wipe(password);
    crypto_wipe(secret, sizeof(secret));

    if (!ok)
    {
        std::fprintf(stderr, "Nao consegui gravar a chave protegida (o arquivo antigo nao mudou).\n");

        return 1;
    }

    std::printf(
        "Pronto: a chave agora pede senha. A chave publica continua a\n"
        "mesma:\n\n    %s\n\n"
        "Substitua tambem as copias de seguranca antigas (elas estao\n"
        "SEM senha) por este arquivo novo.\n",
        publicHex.c_str());

    return 0;
}


int Sign(const Path& keyPath, const Path& installerPath, const std::string& version)
{
    uint8_t secret[64];
    bool plain = false;

    if (!LoadKeyOrComplain(keyPath, secret, plain))
        return 1;

    if (plain)
    {
        std::fprintf(stderr,
            "Aviso: esta chave esta SEM senha. Proteja com:\n"
            "  colae-assinar proteger <arquivo.chave>\n");
    }

    std::vector<uint8_t> installer;

    if (!ReadAll(installerPath, installer) || installer.empty())
    {
        crypto_wipe(secret, sizeof(secret));

        std::fprintf(stderr, "Nao consegui ler o instalador.\n");

        return 1;
    }

    std::string hashHex = UpdateHashHex(installer.data(), installer.size());

    std::string message = UpdateSignedMessage(version, hashHex);

    uint8_t signature[64];

    crypto_eddsa_sign(
        signature,
        secret,
        reinterpret_cast<const uint8_t*>(message.data()),
        message.size());

    // A chave publica fica nos ultimos 32 bytes da privada: confere
    // a propria assinatura antes de entregar.
    std::string publicHex = UpdateToHex(secret + 32, 32);
    std::string signatureHex = UpdateToHex(signature, sizeof(signature));

    crypto_wipe(secret, sizeof(secret));

    if (!UpdateVerify(publicHex, version, hashHex, signatureHex))
    {
        std::fprintf(stderr, "A assinatura gerada nao conferiu.\n");

        return 1;
    }

    std::printf(
        "versao=%s\n"
        "hash=%s\n"
        "assinatura=%s\n"
        "publica=%s\n",
        version.c_str(), hashHex.c_str(), signatureHex.c_str(), publicHex.c_str());

    return 0;
}


void Usage()
{
    std::fprintf(stderr,
        "Uso:\n"
        "  colae-assinar gerar-chave <arquivo.chave>\n"
        "  colae-assinar proteger <arquivo.chave>\n"
        "  colae-assinar assinar <arquivo.chave> <instalador.exe> <versao>\n");
}

} // namespace


#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (argc < 2)
    {
        Usage();

        return 2;
    }

    std::string command = Narrow(argv[1]);

    if (command == "gerar-chave" && argc == 3)
        return GenerateKey(argv[2]);

    if (command == "proteger" && argc == 3)
        return Protect(argv[2]);

    if (command == "assinar" && argc == 5)
        return Sign(argv[2], argv[3], Narrow(argv[4]));

    Usage();

    return 2;
}
