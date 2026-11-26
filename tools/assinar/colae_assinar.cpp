// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

// ============================================================
// colae-assinar - assina os instaladores das atualizacoes
// ============================================================
//
// Uso (no Prompt/PowerShell):
//
//   colae-assinar gerar-chave <arquivo.chave>
//       Cria o par de chaves (uma vez so). Mostra a chave
//       PUBLICA, que vai em src/update_key.h. O arquivo .chave e
//       a chave PRIVADA: guarde com cuidado, fora do repositorio,
//       e faca uma copia de seguranca (perdeu = os programas ja
//       instalados nao aceitam mais atualizacoes automaticas).
//
//   colae-assinar assinar <arquivo.chave> <instalador.exe> <versao>
//       Mostra o "hash" e a "assinatura" do instalador (as linhas
//       do signaling/versao.txt). O tools/publicar.ps1 chama isto.
//
// Sem dependencias alem do Monocypher (o mesmo do programa).
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

#ifdef _WIN32
using Path = std::wstring;

FILE* OpenFile(const Path& path, const char* mode)
{
    std::wstring wideMode(mode, mode + std::strlen(mode));

    return _wfopen(path.c_str(), wideMode.c_str());
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
#else
using Path = std::string;

FILE* OpenFile(const Path& path, const char* mode)
{
    return std::fopen(path.c_str(), mode);
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
#endif


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


bool ReadKey(const Path& path, uint8_t secret[64])
{
    std::vector<uint8_t> data;

    if (!ReadAll(path, data))
        return false;

    std::string text(data.begin(), data.end());

    // Primeira linha "privada=<128 hex>".
    size_t start = text.find("privada=");

    if (start == std::string::npos)
        return false;

    std::string hex = text.substr(start + 8, 128);

    return UpdateFromHex(hex, secret, 64);
}


int GenerateKey(const Path& path)
{
    if (FILE* existing = OpenFile(path, "rb"))
    {
        std::fclose(existing);

        std::fprintf(stderr,
            "Esse arquivo ja existe - nao vou sobrescrever uma chave.\n");

        return 1;
    }

    uint8_t seed[32];
    uint8_t secret[64];
    uint8_t publicKey[32];

    if (!RandomBytes(seed, sizeof(seed)))
    {
        std::fprintf(stderr, "Falha ao gerar numeros aleatorios.\n");

        return 1;
    }

    crypto_eddsa_key_pair(secret, publicKey, seed);

    FILE* file = OpenFile(path, "wb");

    if (!file)
    {
        std::fprintf(stderr, "Nao consegui criar o arquivo da chave.\n");

        return 1;
    }

    std::string secretHex = UpdateToHex(secret, sizeof(secret));
    std::string publicHex = UpdateToHex(publicKey, sizeof(publicKey));

    std::fprintf(file,
        "privada=%s\n"
        "publica=%s\n"
        "# Chave PRIVADA das atualizacoes do Colae. Nao compartilhe,\n"
        "# nao suba pro GitHub, e guarde uma copia de seguranca.\n",
        secretHex.c_str(), publicHex.c_str());

    std::fclose(file);

    crypto_wipe(secret, sizeof(secret));

    std::printf(
        "Chave criada.\n\n"
        "Chave PUBLICA (cole em src/update_key.h):\n\n"
        "    %s\n\n"
        "Guarde o arquivo da chave privada com cuidado e faca uma\n"
        "copia de seguranca (ex.: num pendrive).\n",
        publicHex.c_str());

    return 0;
}


int Sign(const Path& keyPath, const Path& installerPath, const std::string& version)
{
    uint8_t secret[64];

    if (!ReadKey(keyPath, secret))
    {
        std::fprintf(stderr, "Nao consegui ler a chave privada.\n");

        return 1;
    }

    std::vector<uint8_t> installer;

    if (!ReadAll(installerPath, installer) || installer.empty())
    {
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

    if (command == "assinar" && argc == 5)
        return Sign(argv[2], argv[3], Narrow(argv[4]));

    Usage();

    return 2;
}
