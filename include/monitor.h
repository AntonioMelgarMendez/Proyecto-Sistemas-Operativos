#ifndef MONITOR_H
#define MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MONITOR_MAX_CAPACITY 16
#define MONITOR_EVENT_LIMIT 128

typedef struct Monitor Monitor;
typedef enum { PRODUCER, CONSUMER } Actor;
typedef enum { NOT_STARTED, RUNNING, WAITING, FINISHED } WorkerState;
typedef enum {
    EV_START, EV_PUT, EV_GET, EV_WAIT, EV_WAKE, EV_SIGNAL, EV_CLOSE, EV_FINISH
} EventKind;

typedef struct {
    uint64_t sequence;
    double milliseconds;
    EventKind kind;
    Actor actor;
    int value;
    size_t count;
} MonitorEvent;

typedef struct {
    size_t capacity, count, head, tail;
    int cells[MONITOR_MAX_CAPACITY];
    bool closed;
    WorkerState state[2];
    uint64_t produced, consumed, waits[2];
    size_t event_count;
    MonitorEvent events[MONITOR_EVENT_LIMIT];
} MonitorSnapshot;

/* All shared state is private. No caller may lock the monitor directly. */
int monitor_create(Monitor **out, size_t capacity);
void monitor_destroy(Monitor *monitor); /* Only after all workers are joined. */
bool monitor_put(Monitor *monitor, int value);
bool monitor_get(Monitor *monitor, int *value);
void monitor_start(Monitor *monitor, Actor actor);
void monitor_finish(Monitor *monitor, Actor actor);
void monitor_close(Monitor *monitor); /* Reject puts; allow draining; wake both. */
void monitor_snapshot(Monitor *monitor, MonitorSnapshot *snapshot);
const char *monitor_state_name(WorkerState state);
const char *monitor_event_name(EventKind kind);

#endif
