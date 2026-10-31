#include "ui.h"

#include <inttypes.h>
#include <langinfo.h>
#include <locale.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* A bounded character canvas clips by terminal cells, not UTF-8 bytes.
 * All glyphs used here (ASCII, box drawing and simple symbols) have width 1. */
#define CANVAS_ROWS 34
#define CANVAS_COLUMNS 110

typedef enum { NORMAL, MUTED, BRIGHT, PRODUCER_TONE, CONSUMER_TONE, WAIT_TONE, OK_TONE, ERROR_TONE } Tone;
typedef struct { char glyph[5]; Tone tone; } Cell;
typedef struct {
    Cell cells[CANVAS_ROWS][CANVAS_COLUMNS];
    int rows, columns;
    bool unicode;
} Canvas;

static const char *const tones[] = {
    "\033[0m", "\033[90m", "\033[1;37m", "\033[1;36m",
    "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;31m"
};

static int smaller(int a, int b) { return a < b ? a : b; }

static size_t glyph_bytes(unsigned char first)
{
    if (first < 0x80) return 1;
    if (first < 0xe0) return 2;
    if (first < 0xf0) return 3;
    return 4;
}

static int text_width(const char *text)
{
    int width = 0;
    while (*text != '\0') {
        text += glyph_bytes((unsigned char)*text);
        ++width;
    }
    return width;
}

static void put(Canvas *c, int x, int y, Tone tone, const char *text)
{
    if (y < 0 || y >= c->rows) return;
    while (*text != '\0' && x < c->columns) {
        size_t bytes = glyph_bytes((unsigned char)*text);
        if (x >= 0) {
            Cell *cell = &c->cells[y][x];
            memcpy(cell->glyph, text, bytes);
            cell->glyph[bytes] = '\0';
            cell->tone = tone;
        }
        text += bytes;
        ++x;
    }
}

static void label(Canvas *c, int x, int y, Tone tone, const char *format, ...)
{
    /* Format inputs are ASCII; Unicode labels go through put() directly. */
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    put(c, x, y, tone, text);
}

static void centered(Canvas *c, int y, Tone tone, const char *text)
{
    put(c, (c->columns - text_width(text)) / 2, y, tone, text);
}

static void horizontal(Canvas *c, int x, int y, int length, Tone tone)
{
    for (int i = 0; i < length; ++i) put(c, x + i, y, tone, c->unicode ? "─" : "-");
}

static void box(Canvas *c, int x, int y, int width, int height, Tone tone)
{
    horizontal(c, x + 1, y, width - 2, tone);
    horizontal(c, x + 1, y + height - 1, width - 2, tone);
    for (int row = 1; row < height - 1; ++row) {
        put(c, x, y + row, tone, c->unicode ? "│" : "|");
        put(c, x + width - 1, y + row, tone, c->unicode ? "│" : "|");
    }
    put(c, x, y, tone, c->unicode ? "╭" : "+");
    put(c, x + width - 1, y, tone, c->unicode ? "╮" : "+");
    put(c, x, y + height - 1, tone, c->unicode ? "╰" : "+");
    put(c, x + width - 1, y + height - 1, tone, c->unicode ? "╯" : "+");
}

static void present(const Ui *ui, const Canvas *c)
{
    fputs("\033[H", stdout);
    int margin = (ui->columns - c->columns) / 2;
    for (int y = 0; y < c->rows; ++y) {
        fputs("\r\033[2K", stdout);
        if (margin > 0) printf("%*s", margin, "");
        Tone previous = NORMAL;
        if (ui->color) fputs(tones[NORMAL], stdout);
        for (int x = 0; x < c->columns; ++x) {
            const Cell *cell = &c->cells[y][x];
            if (ui->color && cell->tone != previous) {
                fputs(tones[cell->tone], stdout);
                previous = cell->tone;
            }
            fputs(cell->glyph[0] ? cell->glyph : " ", stdout);
        }
        if (ui->color) fputs(tones[NORMAL], stdout);
        putchar('\n');
    }
    fputs("\033[J", stdout);
    fflush(stdout);
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
    /* Locale is configured before main creates any worker. Every UI glyph is
     * one terminal column; non-UTF-8 locales use an ASCII fallback. */
    (void)setlocale(LC_CTYPE, "");
    const char *encoding = nl_langinfo(CODESET);
    ui->unicode = strcmp(encoding, "UTF-8") == 0 || strcmp(encoding, "UTF8") == 0;
    ui->active = true;
    ui->color = !options->no_color && getenv("NO_COLOR") == NULL;
    fputs("\033[?1049h\033[?25l\033[2J\033[H", stdout);
    fflush(stdout);
    return true;
}

void ui_reset_session(Ui *ui)
{
    ui->details = false;
    ui->menu = false;
    ui->transfer_sequence = 0;
    ui->flash_until = 0;
    memset(ui->proof, 0, sizeof(ui->proof));
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


static void worker_card(Canvas *c, int x, int width, Actor actor, const MonitorSnapshot *s,
                        int items)
{
    Tone identity = actor == PRODUCER ? PRODUCER_TONE : CONSUMER_TONE;
    WorkerState state = s->state[actor];
    Tone status = state == WAITING ? WAIT_TONE : state == FINISHED ? OK_TONE : identity;
    static const char *const names[] = { "Pendiente de iniciar", "En ejecucion", "", "Terminado" };
    const char *name = state == WAITING
        ? (actor == PRODUCER ? "Esperando espacio" : "Esperando datos") : names[state];
    box(c, x, 4, width, 5, MUTED);
    put(c, x + 3, 4, identity, actor == PRODUCER ? " PRODUCTOR " : " CONSUMIDOR ");
    put(c, x + 3, 6, status, c->unicode ? (state == NOT_STARTED ? "○" : "●")
                                                      : (state == NOT_STARTED ? "o" : "*"));
    put(c, x + 5, 6, status, name);
    uint64_t count = actor == PRODUCER ? s->produced : s->consumed;
    char numbers[40];
    snprintf(numbers, sizeof(numbers), "%" PRIu64 " de %d", count, items);
    put(c, x + 3, 7, BRIGHT, numbers);
    /* On narrow terminals, numbers already communicate progress. */
    if (width > text_width(numbers) + 16)
        put(c, x + 4 + text_width(numbers), 7, MUTED,
            actor == PRODUCER ? "producidos" : "consumidos");
}

static int buffer_tiles(Canvas *c, const Options *o, const MonitorSnapshot *s,
                        const MonitorEvent *transfer, bool flash)
{
    put(c, 2, 10, BRIGHT, "BUFFER");
    if (s->count > 0) label(c, 14, 10, MUTED, "Proximo en salir: %03d", s->cells[s->head]);
    char occupancy[48];
    snprintf(occupancy, sizeof(occupancy), "%zu / %zu", s->count, s->capacity);
    put(c, c->columns - text_width(occupancy) - 2, 10,
        s->count == 0 || s->count == s->capacity ? WAIT_TONE : MUTED, occupancy);
    char largest[16];
    snprintf(largest, sizeof(largest), "%d", o->items);
    int width = s->capacity <= 5 ? 11 : 7;
    if (width < (int)strlen(largest) + 2) width = (int)strlen(largest) + 2;
    int per_row = (c->columns - 2) / (width + 2);
    int rows = ((int)s->capacity + per_row - 1) / per_row;
    /* Reserve space for status, the demo stages and controls at every capacity.
     * Very dense buffers use single-line cells rather than losing the footer. */
    bool compact = 12 + (rows - 1) * 4 + 3 + 2 >= c->rows - 5;
    int pitch = compact ? 2 : 4;
    if (compact && 12 + (rows - 1) * pitch + 1 + 2 >= c->rows - 5) pitch = 1;
    for (int row = 0; row < rows; ++row) {
        int count = smaller(per_row, (int)s->capacity - row * per_row);
        int start = (c->columns - (count * (width + 2) - 2)) / 2;
        for (int column = 0; column < count; ++column) {
            size_t slot = (size_t)(row * per_row + column);
            bool occupied = (slot + s->capacity - s->head) % s->capacity < s->count;
            size_t recent = transfer != NULL && transfer->kind == EV_PUT
                ? (s->tail + s->capacity - 1) % s->capacity
                : (s->head + s->capacity - 1) % s->capacity;
            bool highlighted = flash && slot == recent;
            Tone tone = highlighted ? (transfer->kind == EV_PUT ? PRODUCER_TONE : CONSUMER_TONE)
                                    : occupied ? BRIGHT : MUTED;
            int x = start + column * (width + 2), y = 12 + row * pitch;
            char value[16];
            if (occupied) snprintf(value, sizeof(value), "%03d", s->cells[slot]);
            else snprintf(value, sizeof(value), "%s", c->unicode ? "·" : ".");
            if (compact) {
                put(c, x, y, tone, occupied && slot == s->head ? (c->unicode ? "›" : ">")
                                                            : highlighted ? "{" : "[");
                put(c, x + width - 1, y, tone, highlighted ? "}" : "]");
            } else {
                box(c, x, y, width, 3, tone);
                if (occupied && slot == s->head)
                    put(c, x + width / 2, y, BRIGHT, c->unicode ? "▼" : "v");
            }
            put(c, x + (width - text_width(value)) / 2, y + (compact ? 0 : 1), tone, value);
        }
    }
    return 12 + (rows - 1) * pitch + (compact ? 1 : 3);
}

static void demo_stages(Canvas *c, const Ui *ui, int row)
{
    /* Historical evidence, deliberately separate from the live worker cards. */
    const char *labels[] = { "Espero", "Recibio senal", "Continuo" };
    bool observed[] = { ui->proof[0] != 0, ui->proof[3] != 0, ui->proof[4] != 0 };
    int x = (c->columns - 61) / 2;
    put(c, x, row, MUTED, "Evidencia:");
    x += 12;
    const int gaps[] = { 12, 20, 0 };
    for (int i = 0; i < 3; ++i) {
        Tone tone = observed[i] ? OK_TONE : MUTED;
        put(c, x, row, tone, c->unicode ? (observed[i] ? "✓" : "○")
                                                   : (observed[i] ? "+" : "o"));
        put(c, x + 2, row, tone, labels[i]);
        x += gaps[i];
    }
}

static void flow(Canvas *c, const MonitorEvent *transfer, bool flash)
{
    const char *left = c->unicode ? "PRODUCTOR ── insertar → " : "PRODUCTOR -- insertar > ";
    const char *right = c->unicode ? " ── extraer → CONSUMIDOR" : " -- extraer > CONSUMIDOR";
    int x = (c->columns - text_width(left) - 6 - text_width(right)) / 2;
    put(c, x, 9, flash && transfer->kind == EV_PUT ? PRODUCER_TONE : MUTED, left);
    x += text_width(left);
    put(c, x, 9, BRIGHT, "BUFFER");
    put(c, x + 6, 9, flash && transfer->kind == EV_GET ? CONSUMER_TONE : MUTED, right);
}

static void scenario_menu(Canvas *c, const Options *o, bool complete)
{
    put(c, 2, 4, BRIGHT, "CAMBIAR CASO");
    put(c, 2, 5, MUTED, complete ? "Caso finalizado. Puedes repetirlo o elegir otro."
                                          : "El menu no pausa los hilos. Elegir otro caso cierra el actual.");
    put(c, 4, 7, PRODUCER_TONE, "[1] Productor bloqueado");
    put(c, 8, 8, MUTED, "Llenar el buffer y esperar espacio.");
    put(c, 4, 10, CONSUMER_TONE, "[2] Consumidor bloqueado");
    put(c, 8, 11, MUTED, "Esperar datos con el buffer inicialmente vacio.");
    put(c, 4, 13, BRIGHT, "[3] Flujo continuo");
    put(c, 8, 14, MUTED, "Ambos hilos trabajan desde el inicio.");
    put(c, 4, 16, CONSUMER_TONE, "[4] Vaciar y esperar datos");
    put(c, 8, 17, MUTED, "Precarga inicial, vaciado, espera y reanudacion.");
    label(c, 2, 18, MUTED, "Capacidad %zu | Meta %d | Se reinician buffer y contadores.", o->capacity, o->items);
    if ((size_t)o->items <= o->capacity)
        label(c, 2, 19, WAIT_TONE, "Casos 1 y 4: meta ajustada a %zu para demostrar el bloqueo.", o->capacity + 1);
    horizontal(c, 2, c->rows - 3, c->columns - 4, MUTED);
    put(c, 2, c->rows - 2, MUTED, "1/2/3/4 Elegir   R Repetir   M Volver   Q Salir");
}

static void details(Canvas *c, const Ui *ui, const Options *o, const MonitorSnapshot *s,
                    const char *status, Tone tone)
{
    put(c, 2, 4, tone, status);
    if (o->scenario != SCENARIO_NORMAL) {
        bool producer = o->scenario == SCENARIO_PRODUCER;
        label(c, 2, 6, MUTED, "WAIT #%" PRIu64 " > %s #%" PRIu64 " > SIGNAL #%" PRIu64,
              ui->proof[0], producer ? "GET" : "PUT", ui->proof[1], ui->proof[2]);
        label(c, 2, 7, MUTED, "WAKE #%" PRIu64 " > %s #%" PRIu64,
              ui->proof[3], producer ? "PUT" : "GET", ui->proof[4]);
    } else put(c, 2, 6, MUTED, "Flujo continuo");
    label(c, 2, 9, MUTED, "H %zu   T %zu     Esperas P %" PRIu64 " / C %" PRIu64
          "     Buffer %zu/%zu", s->head, s->tail, s->waits[PRODUCER],
          s->waits[CONSUMER], s->count, s->capacity);
    put(c, 2, 11, BRIGHT, "EVENTOS RECIENTES");
    int available = smaller(c->rows - 18, 10);
    size_t count = s->event_count < (size_t)available ? s->event_count : (size_t)available;
    for (size_t i = 0; i < count; ++i) {
        const MonitorEvent *e = &s->events[s->event_count - count + i];
        char action[80];
        switch (e->kind) {
        case EV_WAIT: snprintf(action, sizeof(action), "wait(%s)", e->actor == PRODUCER ? "not_full" : "not_empty"); break;
        case EV_SIGNAL: snprintf(action, sizeof(action), "signal(%s)", e->actor == PRODUCER ? "not_empty" : "not_full"); break;
        case EV_WAKE: snprintf(action, sizeof(action), "retorna de wait"); break;
        case EV_PUT: snprintf(action, sizeof(action), "inserta %d", e->value); break;
        case EV_GET: snprintf(action, sizeof(action), "extrae %d", e->value); break;
        case EV_START: snprintf(action, sizeof(action), "inicia"); break;
        case EV_FINISH: snprintf(action, sizeof(action), "termina"); break;
        case EV_CLOSE: snprintf(action, sizeof(action), "cierre / broadcast"); break;
        }
        Tone event_tone = e->kind == EV_WAIT ? WAIT_TONE
            : e->actor == PRODUCER ? PRODUCER_TONE : CONSUMER_TONE;
        label(c, 2, 12 + (int)i, event_tone, "#%04" PRIu64 "  %7.0f ms   %c   %s",
              e->sequence, e->milliseconds, e->actor == PRODUCER ? 'P' : 'C', action);
    }
}

void ui_render(Ui *ui, const Options *o, const MonitorSnapshot *s,
               bool released, bool complete, bool valid, int delay_ms)
{
    struct winsize size;
    ui->rows = 30;
    ui->columns = 110;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_row != 0) ui->rows = size.ws_row;
        if (size.ws_col != 0) ui->columns = size.ws_col;
    }
    if (ui->columns < 2) ui->columns = 2;
    Canvas canvas = { .rows = smaller(ui->rows - 1, CANVAS_ROWS),
                      .columns = smaller(ui->columns - 1, CANVAS_COLUMNS),
                      .unicode = ui->unicode };
    Canvas *c = &canvas;
    collect_proof(ui, o->scenario, s);
    if (ui->rows < 24 || ui->columns < 80) {
        put(c, 1, 1, BRIGHT, "MONITOR");
        put(c, 1, 3, WAIT_TONE, "amplia la terminal a 80 x 24");
        label(c, 1, 5, MUTED, "Buffer %zu/%zu   %s", s->count, s->capacity,
              complete ? (valid ? "COMPLETADO" : "ERROR") : "En curso");
        put(c, 1, 7, MUTED, ui->menu ? "1/2/3/4 elegir  M volver  Q salir" : "M modos  R repetir  Q salir");
        present(ui, c);
        return;
    }
    const char *modes[] = { "01 / PRODUCTOR", "02 / CONSUMIDOR", "03 / FLUJO LIBRE", "04 / VACIADO" };
    put(c, 2, 1, BRIGHT, "MONITOR");
    label(c, c->columns - 36, 1, MUTED, "%s   Ritmo: %d ms", modes[o->scenario], delay_ms);
    horizontal(c, 2, 2, c->columns - 4, MUTED);
    if (ui->menu) {
        scenario_menu(c, o, complete);
        present(ui, c);
        return;
    }
    Actor first = (o->scenario == SCENARIO_CONSUMER || o->scenario == SCENARIO_DRAIN) ? CONSUMER : PRODUCER;
    bool waiting = !released && s->state[first] == WAITING;
    const char *status = complete ? (valid ? "COMPLETADO - FIFO verificado" : "ERROR DE INTEGRIDAD") : waiting
        ? (first == PRODUCER ? "BUFFER LLENO" : "BUFFER VACIO")
        : !released ? (o->scenario == SCENARIO_DRAIN ? "Vaciando buffer precargado" : "Preparando demo") : "En curso";
    Tone status_tone = complete ? (valid ? OK_TONE : ERROR_TONE) : waiting ? WAIT_TONE : MUTED;
    const MonitorEvent *transfer = last_transfer(s);
    if (transfer != NULL && transfer->sequence != ui->transfer_sequence) {
        ui->transfer_sequence = transfer->sequence;
        ui->flash_until = monotonic_ms() + 350;
    }
    bool flash = transfer != NULL && monotonic_ms() < ui->flash_until;
    if (ui->details) details(c, ui, o, s, status, status_tone);
    else {
        int width = (c->columns - 8) / 2;
        worker_card(c, 2, width, PRODUCER, s, o->items);
        worker_card(c, c->columns - width - 2, width, CONSUMER, s, o->items);
        flow(c, transfer, flash);
        int end = buffer_tiles(c, o, s, transfer, flash);
        centered(c, end, status_tone, status);
        char message[100] = "";
        if (complete) {
            snprintf(message, sizeof(message), "%" PRIu64 " producidos | %" PRIu64
                     " consumidos | %zu pendientes", s->produced, s->consumed, s->count);
        } else if (o->scenario == SCENARIO_DRAIN && !released) {
            snprintf(message, sizeof(message), "Preparacion: %zu precargados sin hilo productor | %" PRIu64 " retirados",
                     o->capacity, s->consumed);
        } else if (transfer != NULL) {
            snprintf(message, sizeof(message), "Ultimo evento: %s %03d",
                     transfer->kind == EV_PUT ? "productor inserto" : "consumidor retiro", transfer->value);
        }
        centered(c, end + 1, MUTED, message);
        if (o->scenario != SCENARIO_NORMAL) demo_stages(c, ui, end + 2);
    }
    int footer = c->rows - 2;
    horizontal(c, 2, footer - 3, c->columns - 4, MUTED);
    const char *enter = complete ? "[Enter] Elegir otro caso" : waiting
        ? (o->automatic ? "Continuacion automatica" : first == PRODUCER
            ? "[Enter] Iniciar consumidor" : "[Enter] Iniciar productor") : "";
    put(c, 2, footer - 2, waiting ? WAIT_TONE : BRIGHT, enter);
    label(c, 2, footer, MUTED, "M Modos   R Repetir   D %s   +/- Velocidad   Q salir",
          ui->details ? "volver" : "detalles");
    present(ui, c);
}
