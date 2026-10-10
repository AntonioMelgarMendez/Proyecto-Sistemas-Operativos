/* Link-time fault injection; never linked into the production binary. */
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
