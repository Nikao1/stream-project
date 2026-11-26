# Publicar uma versão nova

Guia para o autor publicar uma versão do Colaê com **atualização automática**.

## Como funciona

1. O Colaê pergunta ao servidor (`GET /versao`) qual é a última versão. O
   servidor responde o conteúdo de `signaling/versao.txt`.
2. Se houver versão nova, aparece o botão **Atualizar**. O instalador é baixado
   do **GitHub Releases**.
3. Antes de instalar, o Colaê confere a **assinatura** do arquivo com a chave
   pública embutida no programa (`src/update_key.h`). Só quem tem a chave
   privada consegue gerar uma assinatura válida, então nem quem invadir o
   servidor ou a conta do GitHub consegue fazer o Colaê instalar outra coisa.
4. O instalador roda em silêncio (`/VERYSILENT /ATUALIZAR`): espera o Colaê
   fechar, instala por cima e abre a versão nova.

A atualização automática só acontece na cópia instalada pelo instalador. Quem
usa o `.zip` recebe o aviso e o botão abre a página de downloads.

## Uma vez só: criar a chave

1. Compile o projeto (o `colae-assinar.exe` é compilado junto, em
   `build\Release\`).
2. Gere o par de chaves **fora da pasta do repositório**. Ele pede uma
   **senha** (mínimo 8 caracteres), que protege a chave:

   ```
   build\Release\colae-assinar.exe gerar-chave D:\chaves\colae.chave
   ```

   A chave privada fica gravada **criptografada** (Argon2id +
   XChaCha20-Poly1305). Sem a senha, o arquivo não serve para nada.
   Se você já tinha uma chave criada **sem senha** (versão antiga da
   ferramenta), proteja-a sem trocar de chave:

   ```
   build\Release\colae-assinar.exe proteger D:\chaves\colae.chave
   ```

3. Cole a **chave pública** que ele mostrar em `src/update_key.h`:

   ```cpp
   constexpr const char* COLAE_UPDATE_PUBLIC_KEY = "cole-aqui-os-64-caracteres";
   ```

4. **Guarde o arquivo `.chave` e a senha com cuidado.** Faça uma cópia de
   segurança do arquivo (num pendrive, por exemplo) e guarde a senha num
   gerenciador de senhas. Esquecer a senha é o mesmo que perder a chave:
   - **Se vazar:** qualquer um poderia assinar atualizações. Gere outra chave e
     publique uma versão nova com a chave pública nova (instalada manualmente
     uma vez).
   - **Se perder:** os Colaê já instalados não aceitam mais atualizações
     automáticas. Os usuários vão precisar instalar a próxima versão
     manualmente uma vez.

   O `.gitignore` já ignora arquivos `*.chave`, mas o mais seguro é deixar
   a chave fora da pasta do projeto.

A primeira versão com a chave embutida precisa ser instalada **manualmente**.
A partir dela, as próximas chegam pelo botão.

## A cada versão

1. Suba o número em `CMakeLists.txt` (`project(... VERSION x.y.z)`).
2. Rode:

   ```
   powershell -ExecutionPolicy Bypass -File tools\publicar.ps1 -Chave D:\chaves\colae.chave -Notas "O que mudou, em uma linha"
   ```

   Ele compila, gera o `.zip` e o instalador, pede a **senha da chave**,
   assina o instalador e escreve `signaling/versao.txt`.
3. Crie o **release** `vx.y.z` no GitHub
   (https://github.com/Nikao1/stream-project/releases/new) e anexe:
   - `dist\Instalar-Colae-x.y.z.exe` (é o que a atualização baixa, então
     **não renomeie**);
   - `dist\Colae-x.y.z-windows.zip`.
4. **Só depois** de o release estar publicado, suba o `versao.txt`:

   ```
   git add signaling/versao.txt
   git commit -m "Release x.y.z"
   git push
   ```

   O Render publica o servidor de novo sozinho. Os Colaê abertos veem a versão
   nova em até 6 horas, ou na hora pelo botão **Procurar atualização**.

A ordem importa: se o `versao.txt` subir antes do release, o botão aparece mas
o download dá erro.
