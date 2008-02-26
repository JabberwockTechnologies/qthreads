#ifndef QTHREAD_TIMER
#define QTHREAD_TIMER

typedef struct qtimer_s *qtimer_t;

void qtimer_start(qtimer_t);
void qtimer_stop(qtimer_t);
double qtimer_secs(qtimer_t);

qtimer_t qtimer_new();
void qtimer_free(qtimer_t);

#endif
