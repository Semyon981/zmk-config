/* Тестовые кадры. Геометрия параметром: стенд перебирает разные панели. */

#ifndef PATTERNS_H
#define PATTERNS_H

#include <stdint.h>

/* Кадр n в buf. Возвращает имя кадра. phase крутится для подвижных. */
const char *pattern(uint8_t *buf, int w, int h, int n, int phase);

/* Счётчик шага: n квадратов 6x6 по верхнему краю. */
void mark(uint8_t *buf, int w, int h, int n);

#define PATTERN_COUNT 8

#endif /* PATTERNS_H */
