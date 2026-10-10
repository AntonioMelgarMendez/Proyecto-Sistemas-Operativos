#include "app.h"
#include "simulation.h"

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

int main(int argc, char **argv)
{
    Options options;
    int parsed = options_parse(argc, argv, &options);
    if (parsed != 0) return parsed > 0 ? EXIT_SUCCESS : 2;
    struct sigaction action = { .sa_handler = on_signal };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    Simulation simulation;
    int error = simulation_init(&simulation, &options);
    if (error != 0) { fprintf(stderr, "monitor: %s\n", strerror(error)); return 1; }
    Actor first = options.scenario == SCENARIO_CONSUMER ? CONSUMER : PRODUCER;
    Actor second = first == PRODUCER ? CONSUMER : PRODUCER;
    bool released = options.scenario == SCENARIO_NORMAL;
    error = simulation_start(&simulation, first);
    if (error == 0 && released) error = simulation_start(&simulation, second);
    uint64_t last = 0;
    MonitorSnapshot snapshot;
    printf("MONITORES C17 | %s | capacity=%zu items=%d\n",
           scenario_name(options.scenario), options.capacity, options.items);
    while (error == 0 && !interrupted) {
        monitor_snapshot(simulation.monitor, &snapshot);
        print_events(&snapshot, &last);
        if (!released && snapshot.state[first] == WAITING) {
            puts("DEMO: bloqueo observado bajo mutex; iniciando el otro hilo.");
            error = simulation_start(&simulation, second);
            released = error == 0;
        }
        if (released && snapshot.state[PRODUCER] == FINISHED &&
            snapshot.state[CONSUMER] == FINISHED) break;
        sleep_ms(10);
    }
    if (error != 0 || interrupted) simulation_stop(&simulation);
    int join_error = simulation_join(&simulation);
    if (join_error != 0) {
        /* Never destroy mutex/conditions if a worker could still own them. */
        fprintf(stderr, "pthread_join: %s\n", strerror(join_error));
        return 1;
    }
    monitor_snapshot(simulation.monitor, &snapshot);
    print_events(&snapshot, &last);
    bool valid = !atomic_load(&simulation.integrity_failed) &&
                 snapshot.produced == (uint64_t)options.items &&
                 snapshot.consumed == snapshot.produced && snapshot.count == 0;
    printf("SUMMARY produced=%" PRIu64 " consumed=%" PRIu64
           " remaining=%zu waits_p=%" PRIu64 " waits_c=%" PRIu64 " status=%s\n",
           snapshot.produced, snapshot.consumed, snapshot.count,
           snapshot.waits[PRODUCER], snapshot.waits[CONSUMER],
           interrupted ? "CANCELLED" : valid && error == 0 ? "OK" : "ERROR");
    simulation_destroy(&simulation);
    if (error != 0) fprintf(stderr, "pthread_create: %s\n", strerror(error));
    if (interrupted) return 128 + interrupted;
    return valid && error == 0 ? 0 : 1;
}
