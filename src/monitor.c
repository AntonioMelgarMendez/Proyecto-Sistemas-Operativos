#include "monitor.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

struct Monitor {
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    MonitorSnapshot data;
    size_t event_next;
    uint64_t sequence;
    struct timespec origin;
};

/* A runtime synchronization failure cannot be recovered by continuing with
 * unprotected data. Initialization errors, in contrast, are returned to main. */
static void checked(int error)
{
    if (error != 0) {
        fprintf(stderr, "Error fatal de pthread: %d\n", error);
        abort();
    }
}

static void invariant(const Monitor *m)
{
    assert(m->data.count <= m->data.capacity);
    assert(m->data.head < m->data.capacity);
    assert(m->data.tail < m->data.capacity);
    assert(m->data.produced - m->data.consumed == m->data.count);
}

/* Called under the mutex: no terminal I/O, allocation or artificial delays. */
static void record(Monitor *m, EventKind kind, Actor actor, int value)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        abort();
    }
    MonitorEvent event = {
        .sequence = ++m->sequence,
        .milliseconds = (now.tv_sec - m->origin.tv_sec) * 1000.0 +
                        (now.tv_nsec - m->origin.tv_nsec) / 1000000.0,
        .kind = kind, .actor = actor, .value = value, .count = m->data.count
    };
    m->data.events[m->event_next] = event;
    m->event_next = (m->event_next + 1) % MONITOR_EVENT_LIMIT;
    if (m->data.event_count < MONITOR_EVENT_LIMIT) {
        ++m->data.event_count;
    }
    invariant(m);
}

int monitor_create(Monitor **out, size_t capacity)
{
    if (out == NULL) return EINVAL;
    *out = NULL;
    if (capacity == 0 || capacity > MONITOR_MAX_CAPACITY) return EINVAL;
    Monitor *m = calloc(1, sizeof(*m));
    if (m == NULL) return ENOMEM;
    m->data.capacity = capacity;
    if (clock_gettime(CLOCK_MONOTONIC, &m->origin) != 0) {
        int error = errno;
        free(m);
        return error;
    }
    int error = pthread_mutex_init(&m->mutex, NULL);
    if (error != 0) { free(m); return error; }
    error = pthread_cond_init(&m->not_full, NULL);
    if (error != 0) {
        checked(pthread_mutex_destroy(&m->mutex));
        free(m);
        return error;
    }
    error = pthread_cond_init(&m->not_empty, NULL);
    if (error != 0) {
        checked(pthread_cond_destroy(&m->not_full));
        checked(pthread_mutex_destroy(&m->mutex));
        free(m);
        return error;
    }
    *out = m;
    return 0;
}

void monitor_destroy(Monitor *m)
{
    if (m == NULL) return;
    checked(pthread_cond_destroy(&m->not_empty));
    checked(pthread_cond_destroy(&m->not_full));
    checked(pthread_mutex_destroy(&m->mutex));
    free(m);
}

bool monitor_put(Monitor *m, int value)
{
    checked(pthread_mutex_lock(&m->mutex));
    /* Mesa-style monitor: waking up is NOT proof that the predicate holds. */
    while (m->data.count == m->data.capacity && !m->data.closed) {
        m->data.state[PRODUCER] = WAITING;
        ++m->data.waits[PRODUCER];
        record(m, EV_WAIT, PRODUCER, value);
        checked(pthread_cond_wait(&m->not_full, &m->mutex));
        m->data.state[PRODUCER] = RUNNING;
        record(m, EV_WAKE, PRODUCER, value);
    }
    if (m->data.closed) {
        checked(pthread_mutex_unlock(&m->mutex));
        return false;
    }
    m->data.cells[m->data.tail] = value;
    m->data.tail = (m->data.tail + 1) % m->data.capacity;
    ++m->data.count;
    ++m->data.produced;
    record(m, EV_PUT, PRODUCER, value);
    checked(pthread_cond_signal(&m->not_empty));
    record(m, EV_SIGNAL, PRODUCER, value);
    checked(pthread_mutex_unlock(&m->mutex));
    return true;
}

bool monitor_get(Monitor *m, int *value)
{
    assert(value != NULL);
    checked(pthread_mutex_lock(&m->mutex));
    while (m->data.count == 0 && !m->data.closed) {
        m->data.state[CONSUMER] = WAITING;
        ++m->data.waits[CONSUMER];
        record(m, EV_WAIT, CONSUMER, 0);
        checked(pthread_cond_wait(&m->not_empty, &m->mutex));
        m->data.state[CONSUMER] = RUNNING;
        record(m, EV_WAKE, CONSUMER, 0);
    }
    if (m->data.count == 0) {
        checked(pthread_mutex_unlock(&m->mutex));
        return false;
    }
    *value = m->data.cells[m->data.head];
    m->data.cells[m->data.head] = 0;
    m->data.head = (m->data.head + 1) % m->data.capacity;
    --m->data.count;
    ++m->data.consumed;
    record(m, EV_GET, CONSUMER, *value);
    checked(pthread_cond_signal(&m->not_full));
    record(m, EV_SIGNAL, CONSUMER, *value);
    checked(pthread_mutex_unlock(&m->mutex));
    return true;
}

void monitor_start(Monitor *m, Actor actor)
{
    checked(pthread_mutex_lock(&m->mutex));
    m->data.state[actor] = RUNNING;
    record(m, EV_START, actor, 0);
    checked(pthread_mutex_unlock(&m->mutex));
}

void monitor_finish(Monitor *m, Actor actor)
{
    checked(pthread_mutex_lock(&m->mutex));
    m->data.state[actor] = FINISHED;
    record(m, EV_FINISH, actor, 0);
    checked(pthread_mutex_unlock(&m->mutex));
}

void monitor_close(Monitor *m)
{
    checked(pthread_mutex_lock(&m->mutex));
    if (!m->data.closed) {
        m->data.closed = true;
        record(m, EV_CLOSE, PRODUCER, 0);
        /* Shutdown is the only broadcast: every waiter must observe closure. */
        checked(pthread_cond_broadcast(&m->not_full));
        checked(pthread_cond_broadcast(&m->not_empty));
    }
    checked(pthread_mutex_unlock(&m->mutex));
}

void monitor_snapshot(Monitor *m, MonitorSnapshot *out)
{
    checked(pthread_mutex_lock(&m->mutex));
    *out = m->data;
    size_t first = (m->event_next + MONITOR_EVENT_LIMIT - m->data.event_count)
                   % MONITOR_EVENT_LIMIT;
    for (size_t i = 0; i < out->event_count; ++i) {
        out->events[i] = m->data.events[(first + i) % MONITOR_EVENT_LIMIT];
    }
    checked(pthread_mutex_unlock(&m->mutex));
}

const char *monitor_state_name(WorkerState state)
{
    static const char *const names[] = {
        "NO INICIADO", "ACTIVO", "ESPERANDO", "TERMINADO"
    };
    return names[state];
}

const char *monitor_event_name(EventKind kind)
{
    static const char *const names[] = {
        "START", "PUT", "GET", "WAIT", "WAKE", "SIGNAL", "CLOSE", "FINISH"
    };
    return names[kind];
}
