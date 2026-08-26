/* Уход в UF2-бутлоадер по команде с хоста — чтобы не жать ресет руками. */

#ifndef DFU_H
#define DFU_H

/* Поднимает фоновый поток-сторож. Вызывать после wait_for_console(). */
void dfu_watch_start(void);

#endif /* DFU_H */
