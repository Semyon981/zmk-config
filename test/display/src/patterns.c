/*
 * Кадры для стенда. Единицы — белое, нули — чёрное, левый пиксель строки
 * лежит в младшем бите байта (панель ждёт LSB первым).
 */

#include <string.h>

#include "patterns.h"

static void px(uint8_t *buf, int w, int x, int y, int on)
{
	uint8_t *byte = &buf[y * (w / 8) + x / 8];
	uint8_t m = 1 << (x % 8);

	if (on) {
		*byte |= m;
	} else {
		*byte &= ~m;
	}
}

/* Вертикальная метка высотой len в столбце x. */
static void vtick(uint8_t *buf, int w, int h, int x, int len)
{
	if (x >= w) {
		return;
	}
	for (int y = 0; y < len && y < h; y++) {
		px(buf, w, x, y, 1);
	}
}

/* Горизонтальная метка длиной len в строке y. */
static void htick(uint8_t *buf, int w, int h, int y, int len)
{
	if (y >= h) {
		return;
	}
	for (int x = 0; x < len && x < w; x++) {
		px(buf, w, x, y, 1);
	}
}

void mark(uint8_t *buf, int w, int h, int n)
{
	for (int i = 0; i < n && (4 + i * 8 + 6) < w; i++) {
		for (int y = 4; y < 10 && y < h; y++) {
			for (int x = 4 + i * 8; x < 10 + i * 8; x++) {
				px(buf, w, x, y, 1);
			}
		}
	}
}

const char *pattern(uint8_t *buf, int w, int h, int n, int phase)
{
	int stride = w / 8;

	switch (n) {
	case 0:
		memset(buf, 0x00, stride * h);
		return "всё нулями (0x00)";
	case 1:
		memset(buf, 0xff, stride * h);
		return "всё единицами (0xff)";
	case 2:
		memset(buf, 0xaa, stride * h);
		return "вертикальные полосы через пиксель";
	case 3:
		for (int y = 0; y < h; y++) {
			memset(&buf[y * stride], y < h / 2 ? 0x00 : 0xff, stride);
		}
		return "верх нулями, низ единицами";
	case 4:
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				px(buf, w, x, y, ((x / 8) + (y / 8)) % 2);
			}
		}
		return "шахматка 8x8";
	case 5:
		memset(buf, 0x00, stride * h);
		for (int x = 0; x < w; x++) {
			px(buf, w, x, 0, 1);
			px(buf, w, x, h - 1, 1);
		}
		for (int y = 0; y < h; y++) {
			px(buf, w, 0, y, 1);
			px(buf, w, w - 1, y, 1);
			px(buf, w, y * w / h, y, 1);
		}
		return "рамка и диагональ";
	case 6:
		/*
		 * Линейка: по ней читается фактическая геометрия панели.
		 * Метки в столбцах 8/16/32/64/128 разной высоты и в строках
		 * 8/16/32/64 разной длины. Если панель уже кадра, дальние
		 * метки не появятся, а строки поедут — сразу видно, где обрез.
		 */
		memset(buf, 0x00, stride * h);
		vtick(buf, w, h, 8, 8);
		vtick(buf, w, h, 16, 16);
		vtick(buf, w, h, 32, 24);
		vtick(buf, w, h, 64, 32);
		vtick(buf, w, h, 128, 40);
		htick(buf, w, h, 8, 8);
		htick(buf, w, h, 16, 16);
		htick(buf, w, h, 32, 24);
		htick(buf, w, h, 64, 32);
		return "линейка (метки 8/16/32/64/128)";
	default:
		memset(buf, 0x00, stride * h);
		for (int y = 0; y < h; y++) {
			for (int x = phase % w; x < (phase % w) + 16 && x < w; x++) {
				px(buf, w, x, y, 1);
			}
		}
		return "бегущая полоса";
	}
}
