# Componentes de terceiros

O Colaê usa os projetos abaixo. Obrigado a quem os mantém.

| Componente | Para que serve | Licença | Origem |
|---|---|---|---|
| Dear ImGui | Interface | MIT | https://github.com/ocornut/imgui |
| Opus | Codificação do som e da voz | BSD 3-Clause | https://github.com/xiph/opus |
| RNNoise | Redução de ruído da voz | BSD 3-Clause | https://github.com/xiph/rnnoise |
| Monocypher | Criptografia (XChaCha20-Poly1305, Argon2id, X25519) | BSD 2-Clause ou CC0 (à escolha) | https://github.com/LoupVaillant/Monocypher |
| FFmpeg (`ffmpeg.exe`) | Codificação e decodificação de vídeo | GPL v3 (builds "full"/"essentials" do gyan.dev) | https://ffmpeg.org |

As bibliotecas (ImGui, Opus, RNNoise e Monocypher) são baixadas pelo CMake na
hora de compilar e ficam dentro do `Colae.exe`. Os textos completos das
licenças estão nos repositórios de origem, nos arquivos `LICENSE` / `COPYING`.

## Sobre o FFmpeg

O `ffmpeg.exe` vai **junto** do programa, como um executável separado e sem
modificações. O Colaê conversa com ele por entrada e saída padrão (não liga
o código dele dentro do próprio executável).

- Licença: GNU General Public License v3 — https://www.gnu.org/licenses/gpl-3.0.html
- Código-fonte do FFmpeg: https://ffmpeg.org/download.html#get-sources
- Builds para Windows usados: https://www.gyan.dev/ffmpeg/builds/ (a página de
  cada build informa a versão e as bibliotecas incluídas).

Quem distribuir o pacote com o `ffmpeg.exe` deve manter este aviso junto.

FFmpeg é uma marca de Fabrice Bellard, criador do projeto FFmpeg.
