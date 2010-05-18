#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <qthread/qthread.h>
#include "argparsing.h"

static syncvar_t x = SYNCVAR_STATIC_INITIALIZER;
static syncvar_t id = SYNCVAR_STATIC_INITIALIZER;
static uint64_t readout = 0;

static aligned_t consumer(qthread_t * t, void *arg)
{
    uint64_t me;

    iprintf("consumer(%p) locking id(%p)\n", t, &id);
    qthread_syncvar_readFE(t, &me, &id);
    iprintf("consumer(%p) id's status became: %u\n", t, qthread_syncvar_status(&id));
    iprintf("consumer(%p) unlocking id(%p), result is %lu\n", t, &id, (unsigned long)me);
    me++;
    qthread_syncvar_writeEF(t, &id, &me);

    iprintf("consumer(%p) readFF on x\n", t);
    qthread_syncvar_readFF(t, &readout, &x);

    return 0;
}

static aligned_t producer(qthread_t * t, void *arg)
{
    uint64_t me;
    uint64_t res = 55;

    iprintf("producer(%p) locking id(%p)\n", t, &id);
    qthread_syncvar_readFE(t, &me, &id);
    iprintf("producer(%p) unlocking id(%p), result is %lu\n", t, &id, (unsigned long)me);
    me++;
    qthread_syncvar_writeEF(t, &id, &me);

    iprintf("producer(%p) x's status is: %u (expect empty)\n", t, qthread_syncvar_status(&x));
    iprintf("producer(%p) filling x(%p)\n", t, &x);
    qthread_syncvar_writeEF(t, &x, &res);

    return 0;
}

int main(int argc, char *argv[])
{
    aligned_t t;
    uint64_t x_value;

    assert(qthread_initialize() == 0);

    CHECK_VERBOSE();

    iprintf("%i threads...\n", qthread_num_shepherds());
    iprintf("Initial value of x: %lu\n", (unsigned long)x.u.w);

    {
	uint64_t t = 1;
	qthread_syncvar_writeF(qthread_self(), &id, &t);
    }
    iprintf("id = 0x%lx\n", (unsigned long)id.u.w);
    { uint64_t t = 0;
	qthread_syncvar_readFF(qthread_self(), &t, &id);
	assert(t == 1);
    }
    iprintf("x's status is: %u (want 1; full nowait)\n", qthread_syncvar_status(&x));
    assert(qthread_syncvar_status(&x) == 1);
    qthread_syncvar_readFE(qthread_self(), NULL, &x);
    iprintf("x's status became: %u (want 0; empty nowait)\n", qthread_syncvar_status(&x));
    assert(qthread_syncvar_status(&x) == 0);
    qthread_fork(consumer, NULL, NULL);
    qthread_fork(producer, NULL, &t);
    qthread_readFF(qthread_self(), NULL, &t);
    iprintf("blocking on x (current status: %u)\n", qthread_syncvar_status(&x));
    qthread_syncvar_readFF(qthread_self(), &x_value, &x);
    assert(qthread_syncvar_status(&x) == 1);


    if (x_value == 55) {
	iprintf("Success! x==55\n");
	return 0;
    } else {
	fprintf(stderr, "Final value of x=%lu\n", (unsigned long)x_value);
	return -1;
    }
}