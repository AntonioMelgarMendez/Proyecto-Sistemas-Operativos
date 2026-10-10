#ifndef UI_H
#define UI_H

#include "app.h"
#include <stdint.h>
#include <termios.h>

typedef struct {
    bool active, color, unicode, details;
    struct termios saved;
    int rows, columns;
    uint64_t transfer_sequence, proof[5];
    double flash_until;
} Ui;

/* Falls back to plain output for pipes, TERM=dumb, or unavailable termios. */
bool ui_open(Ui *ui, const Options *options);
void ui_close(Ui *ui);
int ui_key(Ui *ui, int timeout_ms);
void ui_render(Ui *ui, const Options *options, const MonitorSnapshot *snapshot,
               bool released, bool complete, int delay_ms);

#endif
