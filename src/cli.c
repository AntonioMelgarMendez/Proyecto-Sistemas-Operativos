#include "app.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(void)
{
    puts("Uso: monitor-demo [opciones]\n"
         "  --scenario producer|consumer|normal  Demo (defecto: producer)\n"
         "  --capacity N    Capacidad 1..16 (defecto: 5)\n"
         "  --items N       Elementos 1..1000000 (defecto: 20)\n"
         "  --delay MS      Ritmo 0..2000 ms (defecto: 250)\n"
         "  --auto          Continuar automaticamente tras observar el bloqueo\n"
         "  --plain         Traza de texto sin pantalla interactiva\n"
         "  --no-color      Desactivar colores\n"
         "  --help          Mostrar ayuda\n"
         "Controles: Enter continuar demo, D detalles, +/- velocidad, Q salir.\n"
         "Sin terminal interactiva se activa modo texto automatico.");
}

static bool number(const char *text, long min, long max, int *out)
{
    if (*text == '\0') return false;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    errno = 0;
    char *end;
    long value = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < min || value > max) return false;
    *out = (int)value;
    return true;
}

int options_parse(int argc, char **argv, Options *o)
{
    *o = (Options){ .scenario = SCENARIO_PRODUCER, .capacity = 5,
                   .items = 20, .delay_ms = 250 };
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) { usage(); return 1; }
        if (strcmp(arg, "--plain") == 0) { o->plain = true; continue; }
        if (strcmp(arg, "--auto") == 0) { o->automatic = true; continue; }
        if (strcmp(arg, "--no-color") == 0) { o->no_color = true; continue; }
        if (i + 1 >= argc) {
            fprintf(stderr, "Falta valor para %s\n", arg);
            return -1;
        }
        const char *value = argv[++i];
        if (strcmp(arg, "--scenario") == 0) {
            if (strcmp(value, "producer") == 0) o->scenario = SCENARIO_PRODUCER;
            else if (strcmp(value, "consumer") == 0) o->scenario = SCENARIO_CONSUMER;
            else if (strcmp(value, "normal") == 0) o->scenario = SCENARIO_NORMAL;
            else { fprintf(stderr, "Escenario invalido: %s\n", value); return -1; }
        } else {
            int parsed;
            if (strcmp(arg, "--capacity") == 0 && number(value, 1, 16, &parsed)) {
                o->capacity = (size_t)parsed;
            } else if (strcmp(arg, "--items") == 0 && number(value, 1, 1000000, &parsed)) {
                o->items = parsed;
            } else if (strcmp(arg, "--delay") == 0 && number(value, 0, 2000, &parsed)) {
                o->delay_ms = parsed;
            } else {
                fprintf(stderr, "Opcion o valor invalido: %s %s\n", arg, value);
                return -1;
            }
        }
    }
    if (o->scenario == SCENARIO_PRODUCER && (size_t)o->items <= o->capacity) {
        fputs("Demo producer requiere --items mayor que --capacity para bloquear.\n", stderr);
        return -1;
    }
    return 0;
}

const char *scenario_name(Scenario scenario)
{
    static const char *const names[] = {
        "Bloqueo del productor", "Bloqueo del consumidor", "Flujo continuo"
    };
    return names[scenario];
}

void event_format(const MonitorEvent *e, char *text, size_t size)
{
    const char *actor = e->actor == PRODUCER ? "Productor" : "Consumidor";
    switch (e->kind) {
    case EV_WAIT:
        snprintf(text, size, "%s: wait(%s), buffer %s; libera mutex", actor,
                 e->actor == PRODUCER ? "not_full" : "not_empty",
                 e->actor == PRODUCER ? "lleno" : "vacio");
        break;
    case EV_WAKE:
        snprintf(text, size, "%s: retorna de wait, recupera mutex y reevalua", actor);
        break;
    case EV_SIGNAL:
        snprintf(text, size, "%s: signal(%s)", actor,
                 e->actor == PRODUCER ? "not_empty" : "not_full");
        break;
    case EV_PUT: snprintf(text, size, "Productor inserta elemento %d", e->value); break;
    case EV_GET: snprintf(text, size, "Consumidor extrae elemento %d", e->value); break;
    case EV_CLOSE: snprintf(text, size, "Monitor cerrado: broadcast ambas condiciones"); break;
    case EV_START: snprintf(text, size, "%s inicia", actor); break;
    case EV_FINISH: snprintf(text, size, "%s termina", actor); break;
    }
}

void sleep_ms(int milliseconds)
{
    struct timespec delay = { .tv_sec = milliseconds / 1000,
                             .tv_nsec = (milliseconds % 1000) * 1000000L };
    /* Main and workers recheck their stop flag after each short sleep. */
    (void)nanosleep(&delay, NULL);
}

double monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}
