#ifndef SIMULATION_H
#define SIMULATION_H

#include "app.h"
#include <pthread.h>
#include <stdatomic.h>

typedef struct {
    Monitor *monitor;
    Options options;
    pthread_t threads[2];
    bool started[2]; /* Owned exclusively by main. */
    atomic_bool stopping;
    atomic_bool integrity_failed;
    atomic_int delay_ms;
} Simulation;

int simulation_init(Simulation *simulation, const Options *options);
int simulation_start(Simulation *simulation, Actor actor);
void simulation_stop(Simulation *simulation);
int simulation_join(Simulation *simulation);
void simulation_destroy(Simulation *simulation);

#endif
