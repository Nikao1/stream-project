// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

#include <cstdint>
#include <vector>

// ============================================================
// IMAGENS PEQUENAS (previa da transmissao na sala)
// ============================================================
//
// JPEG pelo WIC (Windows Imaging Component, ja vem no Windows):
// nada de biblioteca extra. Pixels sempre em BGRA 8 bits, sem
// espaco entre as linhas.
// ============================================================

// Reduz (media de blocos) pra caber em maxWidth x maxHeight,
// mantendo a proporcao. Imagem menor que isso: so copia.
void ImageDownscaleBgra(
    const uint8_t* pixels,
    int width,
    int height,
    int strideBytes,
    int maxWidth,
    int maxHeight,
    std::vector<uint8_t>& outPixels,
    int& outWidth,
    int& outHeight
);

// quality: 0..1. Chamar de qualquer thread (cuida do COM).
bool ImageEncodeJpeg(
    const uint8_t* bgraPixels,
    int width,
    int height,
    float quality,
    std::vector<uint8_t>& outJpeg
);

// Abre uma imagem do disco (JPG, PNG, BMP, GIF...), corta o
// quadrado do meio e reduz pra side x side. Pra foto de perfil.
bool ImageLoadSquareFile(
    const wchar_t* path,
    int side,
    std::vector<uint8_t>& outPixels
);

bool ImageDecodeToBgra(
    const uint8_t* data,
    size_t size,
    std::vector<uint8_t>& outPixels,
    int& outWidth,
    int& outHeight
);
