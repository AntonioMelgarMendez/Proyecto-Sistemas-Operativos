#include "app.h"
#include "simulation.h"
#include "ui.h"

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t interrupted;
static void on_signal(int signal_number) { interrupted = signal_number; }

static void print_events(const MonitorSnapshot *snapshot, uint64_t *last)
{
    for (size_t i = 0; i < snapshot->event_count; ++i) {
        const MonitorEvent *e = &snapshot->events[i];
        if (e->sequence <= *last) continue;
        if (e->sequence > *last + 1) {
            printf("GAP omitted=%" PRIu64 " (bounded visual event history)\n",
                   e->sequence - *last - 1);
        }
        char text[160];
        event_format(e, text, sizeof(text));
        printf("#%06" PRIu64 " %9.2fms %-6s actor=%c count=%zu value=%d | %s\n",
               e->sequence, e->milliseconds, monitor_event_name(e->kind),
               e->actor == PRODUCER ? 'P' : 'C', e->count, e->value, text);
        *last = e->sequence;
    }
    fflush(stdout);
}

static bool result_valid(const Simulation *s, const MonitorSnapshot *snapshot)
{
    return !atomic_load(&s->integrity_failed) &&
           snapshot->produced == (uint64_t)s->options.items &&
           snapshot->consumed == snapshot->produced && snapshot->count == 0;
}

/* One run owns its workers. Returning a requested mode does not create another
 * run: main must stop/join/destroy this one first. The terminal stays open. */
static int run_session(Simulation *s, Options *options, Ui *ui, bool visual,
                       int *next_mode, bool *cancelled, MonitorSnapshot *snapshot)
{
    Actor first = (options->scenario == SCENARIO_CONSUMER || options->scenario == SCENARIO_DRAIN) ? CONSUMER : PRODUCER;
    Actor second = first == PRODUCER ? CONSUMER : PRODUCER;
    bool released = options->scenario == SCENARIO_NORMAL;
    int error = simulation_start(s, first);
    if (error == 0 && released) error = simulation_start(s, second);
    uint64_t last = 0;
    double waiting_since = 0;
    if (!visual) printf("MONITORES C17 | %s | capacity=%zu items=%d\n",
                       scenario_name(options->scenario), options->capacity, options->items);
    if (!visual && options->scenario == SCENARIO_DRAIN)
        printf("PREPARACION: %zu elementos precargados sin hilos; consumidor vacia antes de WAIT.\n",
               options->capacity);
    while (error == 0 && !interrupted) {
        monitor_snapshot(s->monitor, snapshot);
        bool complete = released && snapshot->state[PRODUCER] == FINISHED &&
                        snapshot->state[CONSUMER] == FINISHED;
        bool valid = complete && result_valid(s, snapshot);
        if (visual) ui_render(ui, options, snapshot, released, complete, valid,
                              atomic_load(&s->delay_ms));
        else print_events(snapshot, &last);
        if (complete && (!visual || options->automatic) && !ui->menu) break;
        int key = ui_key(ui, visual ? 33 : 10);
        if (key == 'q' || key == 'Q') { *cancelled = !complete; break; }
        if (visual && (key == 'm' || key == 'M' ||
                       (complete && (key == '\n' || key == '\r') && !ui->menu))) {
            ui->menu = !ui->menu;
            continue;
        }
        if (visual && (key == 'r' || key == 'R')) {
            *next_mode = options->scenario;
            break;
        }
        if (ui->menu) {
            if (key >= '1' && key <= '4') {
                *next_mode = key - '1';
                break;
            }
            continue; /* Menu does not pause workers; suppress guided auto-start. */
        }
        if (key == 'd' || key == 'D') ui->details = !ui->details;
        if (key == '+' || key == '-') {
            int delay = atomic_load(&s->delay_ms) + (key == '+' ? -50 : 50);
            if (delay < 0) delay = 0;
            if (delay > 2000) delay = 2000;
            atomic_store(&s->delay_ms, delay);
        }
        if (!released && snapshot->state[first] == WAITING) {
            if (waiting_since == 0) waiting_since = monotonic_ms();
            bool automatic = !visual || options->automatic;
            bool proceed = automatic ? monotonic_ms() - waiting_since >= (visual ? 900 : 0)
                                     : key == '\n' || key == '\r';
            if (proceed) {
                if (!visual) puts("DEMO: bloqueo observado bajo mutex; iniciando el otro hilo.");
                error = simulation_start(s, second);
                released = error == 0;
            }
        }
    }
    options->delay_ms = atomic_load(&s->delay_ms);
    if (error != 0 || interrupted || *cancelled || *next_mode >= 0) simulation_stop(s);
    int join_error = simulation_join(s);
    if (join_error != 0) {
        ui_close(ui);
        /* Never destroy or replace resources if a worker could still own them. */
        fprintf(stderr, "pthread_join: %s\n", strerror(join_error));
        exit(EXIT_FAILURE);
    }
    monitor_snapshot(s->monitor, snapshot);
    if (!visual) print_events(snapshot, &last);
    return error;
}

int main(int argc, char **argv)
{
    Options options;
    int parsed = options_parse(argc, argv, &options);
    if (parsed != 0) return parsed > 0 ? EXIT_SUCCESS : 2;
    struct sigaction action = { .sa_handler = on_signal };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGQUIT, &action, NULL) != 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    Ui ui;
    bool visual = ui_open(&ui, &options);
    for (;;) {
        Simulation simulation;
        int error = simulation_init(&simulation, &options);
        if (error != 0) {
            ui_close(&ui);
            fprintf(stderr, "monitor: %s\n", strerror(error));
            return 1;
        }
        ui_reset_session(&ui);
        MonitorSnapshot snapshot;
        int next_mode = -1;
        bool cancelled = false;
        error = run_session(&simulation, &options, &ui, visual, &next_mode, &cancelled, &snapshot);
        bool valid = result_valid(&simulation, &snapshot);
        simulation_destroy(&simulation); /* Both workers already joined. */
        if (next_mode >= 0 && error == 0 && !interrupted) {
            options.scenario = (Scenario)next_mode;
            options.automatic = false; /* Explicit menu choices are guided. */
            if ((options.scenario == SCENARIO_PRODUCER || options.scenario == SCENARIO_DRAIN) && (size_t)options.items <= options.capacity)
                options.items = (int)options.capacity + 1;
            continue;
        }
        ui_close(&ui);
        printf("SUMMARY produced=%" PRIu64 " consumed=%" PRIu64
               " remaining=%zu waits_p=%" PRIu64 " waits_c=%" PRIu64 " status=%s\n",
               snapshot.produced, snapshot.consumed, snapshot.count,
               snapshot.waits[PRODUCER], snapshot.waits[CONSUMER],
               (interrupted || cancelled) ? "CANCELLED" : valid && error == 0 ? "OK" : "ERROR");
        if (error != 0) fprintf(stderr, "worker startup (pthread_create/sigmask): %s\n", strerror(error));
        if (interrupted) return 128 + interrupted;
        if (cancelled && error == 0) return 0;
        return valid && error == 0 ? 0 : 1;
    }
}
