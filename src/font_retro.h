#ifndef FONT_RETRO_H
#define FONT_RETRO_H

#include <Adafruit_GFX.h>

// Data font 7-segmen kustom untuk jam digital
// Setiap angka direpresentasikan sebagai bitmap 5x7
const unsigned char font_retro[10][5] = {
  {0x7E, 0x81, 0x81, 0x81, 0x7E}, // 0
  {0x00, 0x42, 0xFF, 0x02, 0x00}, // 1
  {0x42, 0x81, 0x89, 0x89, 0x72}, // 2
  {0x42, 0x81, 0x89, 0x81, 0x7E}, // 3
  {0x1E, 0x18, 0x14, 0xFF, 0x10}, // 4
  {0xF2, 0x89, 0x89, 0x89, 0x71}, // 5
  {0x7A, 0x89, 0x89, 0x89, 0x71}, // 6
  {0x80, 0x8F, 0x80, 0x80, 0x80}, // 7
  {0x76, 0x89, 0x89, 0x89, 0x76}, // 8
  {0x8E, 0x89, 0x89, 0x89, 0x7E}  // 9
};

// Fungsi untuk menggambar karakter font retro yang diperbesar
void drawRetroChar(GFXcanvas16 &canvas, int x, int y, char c, int scale, uint16_t color) {
  if (c < '0' || c > '9') return;

  const unsigned char* glyph = font_retro[c - '0'];

  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 5; j++) {
      if ((glyph[j] >> i) & 1) {
        canvas.fillRect(x + j * scale, y + i * scale, scale, scale, color);
      }
    }
  }
}

#endif // FONT_RETRO_H
