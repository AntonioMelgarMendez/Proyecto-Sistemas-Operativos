/* Link-time fault injection; never linked into the production binary. */
#include "monitor.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg)
{
    static int calls;
    const char *setting = getenv("FAIL_CREATE_AT");
    if (setting != NULL && ++calls == atoi(setting)) return EAGAIN;
    return __real_pthread_create(thread, attr, start, arg);
}

/* Corrupt only the value delivered to the consumer, not the monitor counters.
 * This proves that the success UI depends on FIFO validation, not just totals. */
bool __real_monitor_get(Monitor *, int *);
bool __wrap_monitor_get(Monitor *monitor, int *value)
{
    bool result = __real_monitor_get(monitor, value);
    if (result && getenv("FAIL_FIFO") != NULL) ++*value;
    return result;
}
