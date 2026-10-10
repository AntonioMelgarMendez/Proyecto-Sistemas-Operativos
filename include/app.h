#ifndef APP_H
#define APP_H

#include "monitor.h"
#include <stdbool.h>

typedef enum { SCENARIO_PRODUCER, SCENARIO_CONSUMER, SCENARIO_NORMAL } Scenario;
typedef struct {
    Scenario scenario;
    size_t capacity;
    int items, delay_ms;
    bool plain, automatic, no_color;
} Options;

/* 0 = parsed, 1 = help printed, -1 = invalid arguments. */
int options_parse(int argc, char **argv, Options *options);
const char *scenario_name(Scenario scenario);
void event_format(const MonitorEvent *event, char *text, size_t size);
void sleep_ms(int milliseconds);
double monotonic_ms(void);

#endif
