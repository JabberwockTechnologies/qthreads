#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* System Headers */
#include <qthread/qthread-int.h> /* for uint64_t */

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <sys/syscall.h>         /* for SYS_accept and others */

/* Internal Headers */
#include "qt_io.h"
#include "qthread_asserts.h"
#include "qt_debug.h"
#include "qthread_innards.h" /* for qlib */

extern pthread_key_t shepherd_structs;
extern pthread_key_t IO_task_struct;

void qt_blocking_subsystem_begin_blocking_action(void)
{
    qthread_t *me;

    if ((qlib != NULL) && ((me = qthread_internal_self()) != NULL)) {
        qthread_debug(IO_CALLS, "in qthreads, me=%p\n", me);
        qt_blocking_queue_node_t *job = ALLOC_SYSCALLJOB;

        assert(job);
        job->next   = NULL;
        job->thread = me;
        job->op     = USER_DEFINED;

        assert(me->rdata);

        me->rdata->blockedon = (struct qthread_lock_s *)job;
        me->thread_state     = QTHREAD_STATE_SYSCALL;
        qthread_back_to_master(me);
        /* ...and I wake up in a dedicated pthread! */
    } else {
        qthread_debug(IO_CALLS, "NOT in qthreads\n");
    }
}

void qt_blocking_subsystem_end_blocking_action(void)
{
    void *ss = pthread_getspecific(shepherd_structs);

    if ((qlib != NULL) && (ss != NULL)) {
        qthread_t *me = pthread_getspecific(IO_task_struct);

        qthread_debug(IO_CALLS, "in qthreads, me=%p\n", me);
        assert(ss == (void *)1); // indicates an IO worker
        assert(me != NULL);
        qthread_back_to_master(me);
    } else {
        qthread_debug(IO_CALLS, "NOT in qthreads\n");
    }
}

/* vim:set expandtab: */
