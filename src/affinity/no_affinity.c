#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qthread_innards.h"
#include "qt_affinity.h"
#include "qt_debug.h"

qthread_shepherd_id_t guess_num_shepherds(void);
qthread_worker_id_t guess_num_workers_per_shep(qthread_shepherd_id_t nshepherds);

void INTERNAL qt_affinity_init(qthread_shepherd_id_t *nbshepherds,
                               qthread_worker_id_t *nbworkers)
{                                      /*{{{ */
    if (*nbshepherds == 0) {
        *nbshepherds = guess_num_shepherds();
        if (*nbshepherds <= 0) {
            *nbshepherds = 1;
        }
    }
    if (*nbworkers == 0) {
        *nbworkers = guess_num_workers_per_shep(*nbshepherds);
        if (*nbworkers <= 0) {
            *nbworkers = 1;
        }
    }
}                                      /*}}} */

qthread_shepherd_id_t INTERNAL guess_num_shepherds(void)
{                                                          /*{{{ */
#if defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_CONF) /* Linux */
    long ret = sysconf(_SC_NPROCESSORS_CONF);
    qthread_debug(AFFINITY_CALLS, "based on sysconf(), guessing %i shepherds\n",
                  (int)ret);
    return (ret > 0) ? ret : 1;

#elif defined(HAVE_SYSCTL) && defined(CTL_HW) && defined(HW_NCPU)
    int      name[2] = { CTL_HW, HW_NCPU };
    uint32_t oldv;
    size_t   oldvlen = sizeof(oldv);
    if (sysctl(name, 2, &oldv, &oldvlen, NULL, 0) >= 0) {
        assert(oldvlen == sizeof(oldv));
        qthread_debug(AFFINITY_CALLS,
                      "based on sysctl(), guessing %i shepherds\n",
                      (int)oldv);
        return oldv;
    }
#endif /* if defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_CONF) */
    qthread_debug(AFFINITY_CALLS,
                  "no useful interfaces present; assuming a single shepherd\n");
    return 1;
}                                      /*}}} */

#ifdef QTHREAD_MULTITHREADED_SHEPHERDS
void INTERNAL qt_affinity_set(qthread_worker_t *me)
{}

#else
void INTERNAL qt_affinity_set(qthread_shepherd_t *me)
{}

#endif

unsigned int INTERNAL guess_num_workers_per_shep(qthread_shepherd_id_t nshepherds)
{                                      /*{{{ */
    size_t num_procs = 1;
    size_t guess     = 1;

    qthread_debug(AFFINITY_CALLS, "guessing workers for %i shepherds\n",
                  (int)nshepherds);
# if defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_CONF)     /* Linux */
    long ret = sysconf(_SC_NPROCESSORS_CONF);
    qthread_debug(AFFINITY_DETAILS, "sysconf() says %i processors\n", (int)ret);
    num_procs = (ret > 0) ? (size_t)ret : 1;
# elif defined(HAVE_SYSCTL) && defined(CTL_HW) && defined(HW_NCPU)
    int      name[2] = { CTL_HW, HW_NCPU };
    uint32_t oldv;
    size_t   oldvlen = sizeof(oldv);
    if (sysctl(name, 2, &oldv, &oldvlen, NULL, 0) >= 0) {
        assert(oldvlen == sizeof(oldv));
        qthread_debug(AFFINITY_DETAILS, "sysctl() says %i CPUs\n", (int)oldv);
        num_procs = (size_t)oldv;
    }
# endif /* if defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_CONF) */
    guess = num_procs / nshepherds;
    if (guess == 0) {
        guess = 1;
    }
    qthread_debug(AFFINITY_DETAILS, "guessing %i workers per shepherd\n",
                  (int)guess);
    return (qthread_shepherd_id_t)guess;
}                                      /*}}} */

int INTERNAL qt_affinity_gendists(qthread_shepherd_t   *sheps,
                                  qthread_shepherd_id_t nshepherds)
{                                      /*{{{ */
    return QTHREAD_SUCCESS;
}                                      /*}}} */

/* vim:set expandtab: */
