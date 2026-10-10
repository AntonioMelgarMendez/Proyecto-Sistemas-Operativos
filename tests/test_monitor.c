#include "monitor.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

typedef struct { Monitor *monitor; bool producer; bool result; int value; } Job;

static void *operation(void *arg)
{
    Job *job = arg;
    job->result = job->producer ? monitor_put(job->monitor, job->value)
                               : monitor_get(job->monitor, &job->value);
    return NULL;
}

static void wait_for(Monitor *m, Actor actor)
{
    for (int i = 0; i < 3000; ++i) {
        MonitorSnapshot snapshot;
        monitor_snapshot(m, &snapshot);
        if (snapshot.state[actor] == WAITING) return;
        struct timespec delay = { .tv_nsec = 1000000 };
        nanosleep(&delay, NULL);
    }
    assert(!"worker did not enter condition wait");
}

static void test_fifo(void)
{
    Monitor *m = NULL;
    assert(monitor_create(&m, 0) == EINVAL && m == NULL);
    assert(monitor_create(&m, MONITOR_MAX_CAPACITY + 1) == EINVAL);
    assert(monitor_create(NULL, 1) == EINVAL);
    assert(monitor_create(&m, 3) == 0);
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (int i = 0; i < 3; ++i) assert(monitor_put(m, cycle * 3 + i));
        for (int i = 0; i < 3; ++i) {
            int value;
            assert(monitor_get(m, &value));
            assert(value == cycle * 3 + i);
        }
    }
    assert(monitor_put(m, 999));
    monitor_close(m);
    monitor_close(m);
    assert(!monitor_put(m, 1000));
    int value;
    assert(monitor_get(m, &value) && value == 999);
    assert(!monitor_get(m, &value));
    MonitorSnapshot snapshot;
    monitor_snapshot(m, &snapshot);
    assert(snapshot.produced == 301 && snapshot.consumed == 301);
    assert(snapshot.event_count == MONITOR_EVENT_LIMIT);
    for (size_t i = 1; i < snapshot.event_count; ++i) {
        assert(snapshot.events[i].sequence == snapshot.events[i - 1].sequence + 1);
    }
    monitor_destroy(m);
}

static void test_wait(bool producer, bool close_waiter)
{
    Monitor *m;
    assert(monitor_create(&m, 1) == 0);
    if (producer) assert(monitor_put(m, 10));
    Job job = { .monitor = m, .producer = producer, .value = 20 };
    pthread_t thread;
    assert(pthread_create(&thread, NULL, operation, &job) == 0);
    wait_for(m, producer ? PRODUCER : CONSUMER);
    if (close_waiter) monitor_close(m);
    else if (producer) {
        int value;
        assert(monitor_get(m, &value) && value == 10);
    } else assert(monitor_put(m, 30));
    assert(pthread_join(thread, NULL) == 0);
    assert(job.result == !close_waiter);
    if (!producer && !close_waiter) assert(job.value == 30);
    if (producer && !close_waiter) {
        int value;
        assert(monitor_get(m, &value) && value == 20);
    }
    monitor_close(m);
    monitor_destroy(m);
}

int main(void)
{
    test_fifo();
    test_wait(true, false);
    test_wait(false, false);
    test_wait(true, true);
    test_wait(false, true);
    puts("PASS: FIFO, wraparound, capacity=1, wait/signal, close, event ring");
    return 0;
}
