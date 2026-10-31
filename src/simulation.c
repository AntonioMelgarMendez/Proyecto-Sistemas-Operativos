#include "simulation.h"

#include <signal.h>

static void pace(Simulation *s)
{
    int remaining = atomic_load(&s->delay_ms);
    while (remaining > 0 && !atomic_load(&s->stopping)) {
        int part = remaining > 20 ? 20 : remaining;
        sleep_ms(part);
        remaining -= part;
    }
}

static void *producer(void *arg)
{
    Simulation *s = arg;
    monitor_start(s->monitor, PRODUCER);
    int first = s->options.scenario == SCENARIO_DRAIN ? (int)s->options.capacity + 1 : 1;
    for (int i = first; i <= s->options.items && !atomic_load(&s->stopping); ++i) {
        if (!monitor_put(s->monitor, i)) break;
        pace(s);
    }
    monitor_close(s->monitor);
    monitor_finish(s->monitor, PRODUCER);
    return NULL;
}

static void *consumer(void *arg)
{
    Simulation *s = arg;
    monitor_start(s->monitor, CONSUMER);
    int value, expected = 1;
    while (!atomic_load(&s->stopping) && monitor_get(s->monitor, &value)) {
        if (value != expected++) atomic_store(&s->integrity_failed, true);
        pace(s);
    }
    monitor_finish(s->monitor, CONSUMER);
    return NULL;
}

int simulation_init(Simulation *s, const Options *o)
{
    *s = (Simulation){ .options = *o };
    atomic_init(&s->stopping, false);
    atomic_init(&s->integrity_failed, false);
    atomic_init(&s->delay_ms, o->delay_ms);
    int error = monitor_create(&s->monitor, o->capacity);
    if (error != 0) return error;
    /* Scenario setup only: no workers exist yet. Use the same protected FIFO
     * operations; the later producer continues after these initial IDs. */
    if (o->scenario == SCENARIO_DRAIN) {
        for (size_t i = 1; i <= o->capacity; ++i)
            (void)monitor_put(s->monitor, (int)i);
    }
    return 0;
}

int simulation_start(Simulation *s, Actor actor)
{
    if (s->started[actor]) return 0;
    /* Route process termination signals to main only. Workers inherit this
     * temporary mask, so the sig_atomic_t handler flag is never used as a
     * cross-thread synchronization primitive. Restore main's original mask. */
    sigset_t blocked, previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGQUIT);
    int error = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (error != 0) return error;
    error = pthread_create(&s->threads[actor], NULL,
                           actor == PRODUCER ? producer : consumer, s);
    if (error == 0) s->started[actor] = true;
    int restore_error = pthread_sigmask(SIG_SETMASK, &previous, NULL);
    return error != 0 ? error : restore_error;
}

void simulation_stop(Simulation *s)
{
    atomic_store(&s->stopping, true);
    monitor_close(s->monitor);
}

int simulation_join(Simulation *s)
{
    int first_error = 0;
    for (int i = 0; i < 2; ++i) {
        if (s->started[i]) {
            int error = pthread_join(s->threads[i], NULL);
            if (error != 0 && first_error == 0) first_error = error;
        }
    }
    return first_error;
}

void simulation_destroy(Simulation *s)
{
    monitor_destroy(s->monitor);
}
