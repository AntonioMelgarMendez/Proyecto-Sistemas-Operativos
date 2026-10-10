#include "ui.h"

#include <inttypes.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define RESET "\033[0m"
#define CYAN "\033[1;36m"
#define MAGENTA "\033[1;35m"
#define GREEN "\033[1;32m"
#define YELLOW "\033[1;33m"
#define DIM "\033[90m"
#define WHITE "\033[1;37m"

static const char *paint(const Ui *ui, const char *color)
{
    return ui->color ? color : "";
}

static void line(Ui *ui, const char *color, const char *format, ...)
{
    if (ui->line >= ui->rows - 1) return;
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    printf("\r\033[2K%s%.*s%s\n", paint(ui, color), ui->columns - 1, text,
           paint(ui, RESET));
    ++ui->line;
}

static void rule(Ui *ui)
{
    char bar[160];
    int width = ui->columns - 2;
    if (width > (int)sizeof(bar) - 1) width = (int)sizeof(bar) - 1;
    memset(bar, '-', (size_t)width);
    bar[width] = '\0';
    line(ui, DIM, "%s", bar);
}

bool ui_open(Ui *ui, const Options *options)
{
    *ui = (Ui){0};
    const char *term = getenv("TERM");
    if (options->plain || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) ||
        term == NULL || strcmp(term, "dumb") == 0) return false;
    if (tcgetattr(STDIN_FILENO, &ui->saved) != 0) return false;
    struct termios raw = ui->saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    /* Avoid leaving a hidden cursor/raw terminal behind on accidental Ctrl-Z. */
    raw.c_cc[VSUSP] = _POSIX_VDISABLE;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return false;
    ui->active = true;
    ui->color = !options->no_color && getenv("NO_COLOR") == NULL;
    fputs("\033[?1049h\033[?25l\033[2J\033[H", stdout);
    fflush(stdout);
    return true;
}

void ui_close(Ui *ui)
{
    if (!ui->active) return;
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &ui->saved);
    fputs("\033[0m\033[?25h\033[?1049l", stdout);
    fflush(stdout);
    ui->active = false;
}

int ui_key(Ui *ui, int timeout_ms)
{
    if (!ui->active) { sleep_ms(timeout_ms); return 0; }
    struct pollfd fd = { .fd = STDIN_FILENO, .events = POLLIN };
    int result = poll(&fd, 1, timeout_ms);
    if (result <= 0) return 0;
    if (fd.revents & (POLLHUP | POLLERR | POLLNVAL)) return 'q';
    unsigned char key;
    if ((fd.revents & POLLIN) && read(STDIN_FILENO, &key, 1) == 1) return key;
    return 0;
}

static void collect_proof(Ui *ui, Scenario scenario, const MonitorSnapshot *s)
{
    if (scenario == SCENARIO_NORMAL) return;
    Actor first = scenario == SCENARIO_PRODUCER ? PRODUCER : CONSUMER;
    EventKind change = first == PRODUCER ? EV_GET : EV_PUT;
    EventKind resume = first == PRODUCER ? EV_PUT : EV_GET;
    for (size_t i = 0; i < s->event_count; ++i) {
        const MonitorEvent *e = &s->events[i];
        if (!ui->proof[0] && e->kind == EV_WAIT && e->actor == first)
            ui->proof[0] = e->sequence;
        if (ui->proof[0] && !ui->proof[1] && e->sequence > ui->proof[0] &&
            e->kind == change && e->actor != first) ui->proof[1] = e->sequence;
        if (ui->proof[1] && !ui->proof[2] && e->sequence > ui->proof[1] &&
            e->kind == EV_SIGNAL && e->actor != first) ui->proof[2] = e->sequence;
        if (ui->proof[2] && !ui->proof[3] && e->sequence > ui->proof[2] &&
            e->kind == EV_WAKE && e->actor == first) ui->proof[3] = e->sequence;
        if (ui->proof[3] && !ui->proof[4] && e->sequence > ui->proof[3] &&
            e->kind == resume && e->actor == first) ui->proof[4] = e->sequence;
    }
}

static const MonitorEvent *last_transfer(const MonitorSnapshot *s)
{
    for (size_t i = s->event_count; i > 0; --i) {
        const MonitorEvent *e = &s->events[i - 1];
        if (e->kind == EV_PUT || e->kind == EV_GET) return e;
    }
    return NULL;
}

void ui_render(Ui *ui, const Options *o, const MonitorSnapshot *s,
               bool released, bool complete, int delay_ms)
{
    struct winsize size;
    ui->rows = 32;
    ui->columns = 100;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_row != 0) ui->rows = size.ws_row;
        if (size.ws_col != 0) ui->columns = size.ws_col;
    }
    if (ui->columns < 2) ui->columns = 2;
    ui->line = 0;
    fputs("\033[H", stdout);
    collect_proof(ui, o->scenario, s);
    if (ui->rows < 24 || ui->columns < 80) {
        line(ui, YELLOW, "MONITORES: amplia la terminal a 80x24 o mas (ideal 100x34).");
        line(ui, WHITE, "Actual: %dx%d | buffer %zu/%zu | P:%s C:%s",
             ui->columns, ui->rows, s->count, s->capacity,
             monitor_state_name(s->state[0]), monitor_state_name(s->state[1]));
        line(ui, CYAN, "Enter: continuar demo | Q: salir | +/-: velocidad");
        line(ui, GREEN, "%s", complete ? "COMPLETADO: Enter/Q para cerrar" : "Simulacion activa");
        fputs("\033[J", stdout);
        fflush(stdout);
        return;
    }
    const MonitorEvent *transfer = last_transfer(s);
    if (transfer != NULL && transfer->sequence != ui->transfer_sequence) {
        ui->transfer_sequence = transfer->sequence;
        ui->flash_until = monotonic_ms() + 350;
    }
    bool flash = transfer != NULL && monotonic_ms() < ui->flash_until;
    line(ui, CYAN, "  MONITORES / LABORATORIO DE SINCRONIZACION                         C17 + POSIX");
    line(ui, DIM, "  Un mutex | Dos condiciones | Sin semaforos | Snapshot consistente");
    rule(ui);
    line(ui, WHITE, "  %s   |   Ritmo: %d ms   |   Meta: %d elementos",
         scenario_name(o->scenario), delay_ms, o->items);
    line(ui, s->state[PRODUCER] == WAITING ? YELLOW : CYAN,
         "  [P] PRODUCTOR   %-12s   insertados: %-7" PRIu64 "  esperas: %" PRIu64,
         monitor_state_name(s->state[PRODUCER]), s->produced, s->waits[PRODUCER]);
    line(ui, s->state[CONSUMER] == WAITING ? YELLOW : MAGENTA,
         "  [C] CONSUMIDOR  %-12s   extraidos:  %-7" PRIu64 "  esperas: %" PRIu64,
         monitor_state_name(s->state[CONSUMER]), s->consumed, s->waits[CONSUMER]);
    rule(ui);
    line(ui, flash ? (transfer->kind == EV_PUT ? CYAN : MAGENTA) : DIM,
         "  PRODUCTOR  ===== insertar =====>  BUFFER  ===== extraer =====>  CONSUMIDOR");
    int per_row = ui->columns >= 92 ? 8 : 7;
    for (size_t base = 0; base < s->capacity; base += (size_t)per_row) {
        char cells[200] = "  ", indices[200] = "  ";
        size_t used = 2, index_used = 2;
        for (size_t i = base; i < s->capacity && i < base + (size_t)per_row; ++i) {
            bool occupied = ((i + s->capacity - s->head) % s->capacity) < s->count;
            size_t recent_slot = transfer != NULL && transfer->kind == EV_PUT
                ? (s->tail + s->capacity - 1) % s->capacity
                : (s->head + s->capacity - 1) % s->capacity;
            bool marked = flash && i == recent_slot;
            if (occupied) used += (size_t)snprintf(cells + used, sizeof(cells) - used,
                "%c%7d%c ", marked ? '{' : '[', s->cells[i], marked ? '}' : ']');
            else used += (size_t)snprintf(cells + used, sizeof(cells) - used,
                "%c   .   %c ", marked ? '{' : '[', marked ? '}' : ']');
            index_used += (size_t)snprintf(indices + index_used, sizeof(indices) - index_used,
                " %2zu:%c%c    ", i, i == s->head ? 'H' : '-', i == s->tail ? 'T' : '-');
        }
        line(ui, s->count == s->capacity ? YELLOW : GREEN, "%s", cells);
        line(ui, DIM, "%s", indices);
    }
    char bar[25];
    size_t fill = s->count * 24 / s->capacity;
    for (size_t i = 0; i < 24; ++i) bar[i] = i < fill ? '#' : '.';
    bar[24] = '\0';
    line(ui, s->count == 0 || s->count == s->capacity ? YELLOW : GREEN,
         "  [%s] %zu/%zu | H: sacar  T: insertar  { }: ultima transferencia",
         bar, s->count, s->capacity);
    if (transfer != NULL) {
        line(ui, transfer->kind == EV_PUT ? CYAN : MAGENTA,
             "  Ultimo movimiento real: %s elemento %d (#%" PRIu64 ")",
             transfer->kind == EV_PUT ? "P ---> BUFFER" : "BUFFER ---> C",
             transfer->value, transfer->sequence);
    } else line(ui, DIM, "  Esperando la primera transferencia real...");
    rule(ui);
    Actor first = o->scenario == SCENARIO_CONSUMER ? CONSUMER : PRODUCER;
    if (complete) line(ui, GREEN, "  COMPLETADO | Todos los elementos consumidos. Enter/Q para cerrar.");
    else if (!released && s->state[first] == WAITING) {
        line(ui, YELLOW, "  BLOQUEO REAL OBSERVADO: %s espera en %s.",
             first == PRODUCER ? "productor" : "consumidor",
             first == PRODUCER ? "not_full" : "not_empty");
    } else line(ui, WHITE, "  %s", !released ? "Preparando el bloqueo: el segundo hilo aun no existe."
                                            : "Ejecucion concurrente: la UI no retiene el mutex al dibujar.");
    if (o->scenario != SCENARIO_NORMAL) {
        line(ui, ui->proof[4] ? GREEN : YELLOW,
             "  Evidencia: WAIT #%" PRIu64 " > %s #%" PRIu64 " > SIGNAL #%" PRIu64
             " > WAKE #%" PRIu64 " > %s #%" PRIu64,
             ui->proof[0], first == PRODUCER ? "GET" : "PUT", ui->proof[1],
             ui->proof[2], ui->proof[3], first == PRODUCER ? "PUT" : "GET", ui->proof[4]);
    } else line(ui, DIM, "  WAIT libera mutex; WAKE lo recupera; while reevalua el predicado.");
    line(ui, DIM, "  signal no transfiere el mutex. #0 = evento aun no observado.");
    int available = ui->rows - ui->line - 5;
    if (available > 7) available = 7;
    if (available > 0) {
        line(ui, WHITE, "  CRONOLOGIA RECIENTE (historial acotado, no animacion simulada)");
        size_t count = s->event_count < (size_t)available ? s->event_count : (size_t)available;
        for (size_t i = s->event_count - count; i < s->event_count; ++i) {
            const MonitorEvent *e = &s->events[i];
            char text[160];
            event_format(e, text, sizeof(text));
            line(ui, e->kind == EV_WAIT ? YELLOW : e->actor == PRODUCER ? CYAN : MAGENTA,
                 "  #%04" PRIu64 " %7.0fms %-6s %s", e->sequence,
                 e->milliseconds, monitor_event_name(e->kind), text);
        }
    }
    rule(ui);
    line(ui, WHITE, "  [Enter] Iniciar otro hilo cuando espere | [+/-] Rapido/lento | [Q] Salir");
    line(ui, DIM, "  Cian: productor | Magenta: consumidor | Amarillo: espera | Verde: resultado");
    fputs("\033[J", stdout);
    fflush(stdout);
}
