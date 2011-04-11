#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* The API */
#include "qthread/qthread.h"

/* Internal Headers */
#include "qthread_innards.h"
#include "qt_blocking_structs.h"
#include "qt_qthread_struct.h"
#include "qt_threadqueues.h"
#include "qt_locks.h"

/* Types */
typedef enum bt {
    LOCK,
    UNLOCK
} blocker_type;
typedef struct {
    pthread_mutex_t lock;
    void           *a;
    blocker_type    type;
} qthread_lock_blocker_t;

/* Function Prototypes */
static QINLINE qthread_queue_t *qthread_queue_new(qthread_shepherd_t *shepherd);
static QINLINE void             qthread_queue_free(qthread_queue_t *q);
static QINLINE qthread_t *      qthread_dequeue(qthread_queue_t *q);

#if defined(UNPOOLED_LOCKS) || defined(UNPOOLED)
# define ALLOC_LOCK(shep) (qthread_lock_t *)malloc(sizeof(qthread_lock_t))
# define FREE_LOCK(t)     free(t)
#else
static QINLINE qthread_lock_t *ALLOC_LOCK(qthread_shepherd_t *shep)
{                      /*{{{ */
    extern qt_mpool generic_lock_pool;
    qthread_lock_t *tmp = (qthread_lock_t *)qt_mpool_alloc(shep ? (shep->lock_pool) : generic_lock_pool);

    if (tmp != NULL) {
        tmp->creator_ptr = shep;
    }
    return tmp;
}                      /*}}} */

static QINLINE void FREE_LOCK(qthread_lock_t *t)
{                      /*{{{ */
    extern qt_mpool generic_lock_pool;

    qt_mpool_free(t->creator_ptr ? (t->creator_ptr->lock_pool) : generic_lock_pool, t);
}                      /*}}} */

#endif /* if defined(UNPOOLED_LOCKS) || defined(UNPOOLED) */

#if defined(UNPOOLED_QUEUES) || defined(UNPOOLED)
# define ALLOC_QUEUE(shep) (qthread_queue_t *)malloc(sizeof(qthread_queue_t))
# define FREE_QUEUE(t)     free(t)
#else
static QINLINE qthread_queue_t *ALLOC_QUEUE(qthread_shepherd_t *shep)
{                      /*{{{ */
    extern qt_mpool  generic_queue_pool;
    qthread_queue_t *tmp = (qthread_queue_t *)qt_mpool_alloc(shep ? (shep->queue_pool) : generic_queue_pool);

    if (tmp != NULL) {
        tmp->creator_ptr = shep;
    }
    return tmp;
}                      /*}}} */

static QINLINE void FREE_QUEUE(qthread_queue_t *t)
{                      /*{{{ */
    extern qt_mpool generic_queue_pool;

    qt_mpool_free(t->creator_ptr ? (t->creator_ptr->queue_pool) : generic_queue_pool, t);
}                      /*}}} */

#endif /* if defined(UNPOOLED_QUEUES) || defined(UNPOOLED) */

#ifdef QTHREAD_COUNT_THREADS
# define QTHREAD_COUNT_THREADS_BINCOUNTER(TYPE, BIN) qthread_internal_incr(&qlib->TYPE ## _stripes[(BIN)], &qlib->TYPE ## _stripes_locks[(BIN)], 1)
#else
# define QTHREAD_COUNT_THREADS_BINCOUNTER(TYPE, BIN) do { } while(0)
#endif

static aligned_t qthread_lock_blocker_thread(void *arg)
{                                      /*{{{ */
    qthread_lock_blocker_t *const restrict a = (qthread_lock_blocker_t *)arg;

    switch (a->type) {
        case LOCK:
            qthread_lock(a->a);
            break;
        case UNLOCK:
            qthread_unlock(a->a);
            break;
    }
    pthread_mutex_unlock(&(a->lock));
    return 0;
}                                      /*}}} */

static void qthread_lock_blocker_func(void        *addr,
                                      blocker_type t)
{   /*{{{*/
    qthread_lock_blocker_t args = { PTHREAD_MUTEX_INITIALIZER, addr, t };

    pthread_mutex_lock(&args.lock);
    qthread_fork(qthread_lock_blocker_thread, &args, NULL);
    pthread_mutex_lock(&args.lock);
    pthread_mutex_unlock(&args.lock);
    pthread_mutex_destroy(&args.lock);
} /*}}}*/

/* functions to implement FEB-ish locking/unlocking
 *
 * These are atomic and functional, but do not have the same semantics as full
 * FEB locking/unlocking (for example, unlocking cannot block)
 *
 * NOTE: these have not been profiled, and so may need tweaking for speed
 * (e.g. multiple hash tables, shortening critical section, etc.)
 */

/* The lock ordering in these functions is very particular, and is designed to
 * reduce the impact of having a centralized hashtable. Don't monkey with it
 * unless you REALLY know what you're doing!
 */

int qthread_lock(const aligned_t *a)
{                      /*{{{ */
    qthread_lock_t *m;
    const int       lockbin = QTHREAD_CHOOSE_STRIPE(a);
    qthread_t      *me      = qthread_internal_self();

    QTHREAD_LOCK_TIMER_DECLARATION(aquirelock);

    if (!me) {
        qthread_lock_blocker_func((void *)a, LOCK);
        return QTHREAD_SUCCESS;
    }
    qthread_debug(LOCK_BEHAVIOR, "tid(%u), a(%p): starting...\n",
                  me->thread_id, a);
    QTHREAD_LOCK_UNIQUERECORD(lock, a, me);
    QTHREAD_LOCK_TIMER_START(aquirelock);

    QTHREAD_COUNT_THREADS_BINCOUNTER(locks, lockbin);
    qt_hash_lock(qlib->locks[lockbin]);
    m = (qthread_lock_t *)qt_hash_get_locked(qlib->locks[lockbin], (void *)a);
    if (m == NULL) {
        m = ALLOC_LOCK(me->rdata->shepherd_ptr);
        if (m == NULL) {
            qt_hash_unlock(qlib->locks[lockbin]);
            return QTHREAD_MALLOC_ERROR;
        }

        assert(me->rdata->shepherd_ptr == qthread_internal_getshep());
        m->waiting = qthread_queue_new(me->rdata->shepherd_ptr);
        if (m->waiting == NULL) {
            FREE_LOCK(m);
            qt_hash_unlock(qlib->locks[lockbin]);
            return QTHREAD_MALLOC_ERROR;
        }
        QTHREAD_FASTLOCK_INIT(m->lock);
        QTHREAD_HOLD_TIMER_INIT(m);
        qt_hash_put_locked(qlib->locks[lockbin], (void *)a, m);
        /* since we just created it, we own it */
        QTHREAD_FASTLOCK_LOCK(&m->lock);
        /* can only unlock the hash after we've locked the address, because
         * otherwise there's a race condition: the address could be removed
         * before we have a chance to add ourselves to it */
        qt_hash_unlock(qlib->locks[lockbin]);

#ifdef QTHREAD_DEBUG
        m->owner = me->thread_id;
#endif
        QTHREAD_FASTLOCK_UNLOCK(&m->lock);
        qthread_debug(LOCK_BEHAVIOR,
                      "tid(%u), a(%p): returned (wasn't locked)\n",
                      me->thread_id, a);
    } else {
        QTHREAD_WAIT_TIMER_DECLARATION;
        /* success==failure: because it's in the hash, someone else owns
         * the lock; dequeue this thread and yield. NOTE: it's up to the
         * master thread to enqueue this thread and unlock the address
         */
        QTHREAD_FASTLOCK_LOCK(&m->lock);
        /* for an explanation of the lock/unlock ordering here, see above */
        qt_hash_unlock(qlib->locks[lockbin]);

        me->thread_state     = QTHREAD_STATE_BLOCKED;
        me->rdata->blockedon = m;

        QTHREAD_WAIT_TIMER_START();

        qthread_back_to_master(me);

        QTHREAD_WAIT_TIMER_STOP(me, lockwait);

        /* once I return to this context, I own the lock! */
        /* conveniently, whoever unlocked me already set up everything too */
        qthread_debug(LOCK_BEHAVIOR,
                      "tid(%u), a(%p): returned (was locked)\n",
                      me->thread_id, a);
    }
    QTHREAD_LOCK_TIMER_STOP(aquirelock, me);
    QTHREAD_HOLD_TIMER_START(m);
    return QTHREAD_SUCCESS;
}                      /*}}} */

int qthread_unlock(const aligned_t *a)
{                      /*{{{ */
    qthread_lock_t *m;
    qthread_t      *u;
    const int       lockbin = QTHREAD_CHOOSE_STRIPE(a);
    qthread_t      *me      = qthread_internal_self();

    if (!me) {
        qthread_lock_blocker_func((void *)a, UNLOCK);
        return QTHREAD_SUCCESS;
    }
    qthread_debug(LOCK_BEHAVIOR, "tid(%u), a(%p)\n", me->thread_id, a);

    QTHREAD_COUNT_THREADS_BINCOUNTER(locks, lockbin);
    qt_hash_lock(qlib->locks[lockbin]);
    m = (qthread_lock_t *)qt_hash_get_locked(qlib->locks[lockbin], (void *)a);
    if (m == NULL) {
        /* unlocking an address that's already unlocked */
        qt_hash_unlock(qlib->locks[lockbin]);
        return QTHREAD_REDUNDANT;
    }
    QTHREAD_FASTLOCK_LOCK(&m->lock);

    QTHREAD_HOLD_TIMER_STOP(m, me->rdata->shepherd_ptr);

    /* unlock the address... if anybody's waiting for it, give them the lock
     * and put them in a ready queue.  If not, delete the lock structure.
     */

    QTHREAD_LOCK(&m->waiting->lock);
    u = qthread_dequeue(m->waiting);
    if (u == NULL) {
        qthread_debug(LOCK_DETAILS,
                      "tid(%u), a(%p): deleting waiting queue\n",
                      me->thread_id, a);
        qt_hash_remove_locked(qlib->locks[lockbin], (void *)a);
        qt_hash_unlock(qlib->locks[lockbin]);
        QTHREAD_HOLD_TIMER_DESTROY(m);
        QTHREAD_UNLOCK(&m->waiting->lock);
        qthread_queue_free(m->waiting);
        QTHREAD_FASTLOCK_UNLOCK(&m->lock);
        QTHREAD_FASTLOCK_DESTROY(m->lock);
        FREE_LOCK(m);
    } else {
        qt_hash_unlock(qlib->locks[lockbin]);
        qthread_debug(LOCK_DETAILS,
                      "tid(%u), a(%p): pulling thread from queue (%p)\n",
                      me->thread_id, a, u);
        u->thread_state = QTHREAD_STATE_RUNNING;
#ifdef QTHREAD_DEBUG
        m->owner = u->thread_id;
#endif

        /* NOTE: because of the use of getcontext()/setcontext(), threads
         * return to the shepherd that setcontext()'d into them, so they
         * must remain in that queue.
         */
        qt_threadqueue_enqueue(u->rdata->shepherd_ptr->ready, u, me->rdata->shepherd_ptr);

        QTHREAD_UNLOCK(&m->waiting->lock);
        QTHREAD_FASTLOCK_UNLOCK(&m->lock);
    }

    return QTHREAD_SUCCESS;
}                      /*}}} */

static QINLINE qthread_queue_t *qthread_queue_new(qthread_shepherd_t *shepherd)
{                      /*{{{ */
    qthread_queue_t *q;

    q = ALLOC_QUEUE(shepherd);
    if (q != NULL) {
        q->head = NULL;
        q->tail = NULL;
        if (pthread_mutex_init(&q->lock, NULL) != 0) {
            FREE_QUEUE(q);
            return NULL;
        }
        if (pthread_cond_init(&q->notempty, NULL) != 0) {
            QTHREAD_DESTROYLOCK(&q->lock);
            FREE_QUEUE(q);
            return NULL;
        }
    }
    return q;
}                      /*}}} */

static QINLINE qthread_t *qthread_dequeue(qthread_queue_t *q)
{                      /*{{{ */
    qthread_t *t = NULL;

    /* NOTE: it's up to the caller to lock/unlock the queue! */
    qthread_debug(ALL_FUNCTIONS, "q(%p), t(%p): started\n", q, t);

    if (q->head == NULL) {
        qthread_debug(ALL_DETAILS, "q(%p), t(%p): finished (nobody in list)\n", q, t);
        return (NULL);
    }

    t = q->head;
    if (q->head != q->tail) {
        q->head = q->head->next;
    } else {
        q->head = NULL;
        q->tail = NULL;
    }
    t->next = NULL;

    qthread_debug(ALL_DETAILS, "q(%p), t(%p): finished\n", q, t);
    return (t);
}                      /*}}} */

static QINLINE void qthread_queue_free(qthread_queue_t *q)
{                      /*{{{ */
    assert((q->head == NULL) && (q->tail == NULL));
    QTHREAD_DESTROYLOCK(&q->lock);
    QTHREAD_DESTROYCOND(&q->notempty);
    FREE_QUEUE(q);
}                      /*}}} */

/* vim:set expandtab: */