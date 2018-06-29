/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 *
 * Author: Hoang-Vu Dang (dang.hvu@gmail.com).
 */

#include <stdlib.h>
#include <hwloc.h>

#include "lock/zm_shmcsp.h"
#include "lock/zm_hmcs.h"
#include "lock/zm_mcs.h"

int zm_shmcsp_init(zm_shmcsp_t *L) {
    zm_hmcs_init(&L->high_p);
    zm_mcs_init(&L->low_p);
    L->own_p = 0;
    L->filter = 0;
    return 0;
}

static inline __attribute__((always_inline)) void tns_acquire(zm_atomic_flag_t * l)
{
    do {
        while (zm_atomic_load(l, zm_memord_acquire) == 1)
            ;
    } while (zm_atomic_flag_test_and_set(l, zm_memord_acq_rel)); 
}

static inline __attribute__((always_inline)) void tns_release(zm_atomic_flag_t * l)
{
    zm_atomic_flag_clear(l, zm_memord_release);
}

int zm_shmcsp_acquire(zm_shmcsp_t *L) {
    if (zm_unlikely(zm_atomic_exchange(&L->own_p, 1, zm_memord_acq_rel) == 1)) {
        zm_hmcs_acquire(L->high_p);
        zm_atomic_store(&L->filter, 1, zm_memord_release);
        tns_acquire(&L->own_p);
        zm_atomic_store(&L->filter, 0, zm_memord_release);
        zm_hmcs_release(L->high_p);
    }
    // fast-path: acquired the own_p lock (very-biased lock).
    return 0;
}

int zm_shmcsp_tryacq(zm_shmcsp_t *L, int *success) {
    assert(0);
    return 0;
}

int zm_shmcsp_acquire_low(zm_shmcsp_t *L) {
    zm_mcs_acquire(L->low_p);

    // One of the low-priority can contest for the CS.
    while (zm_atomic_load(&L->filter, zm_memord_acquire))
        ; // spin
    tns_acquire(&L->own_p);

    zm_mcs_release(L->low_p);

    return 0;
}

int zm_shmcsp_tryacq_low(zm_shmcsp_t *L, int *success) {
    assert(0);
    return 0;
}

int zm_shmcsp_release(zm_shmcsp_t *L) {
    tns_release(&L->own_p);
    return 0;
}

int zm_shmcsp_acquire_c(zm_shmcsp_t *L, zm_mcs_qnode_t* I) {
    assert(0);
    return 0;
}

int zm_shmcsp_acquire_low_c(zm_shmcsp_t *L, zm_mcs_qnode_t* I) {
    assert(0);
    return 0;
}

int zm_shmcsp_release_c(zm_shmcsp_t *L, zm_mcs_qnode_t *I) {
    assert(0);
    return 0;
}

int zm_shmcsp_destroy(zm_shmcsp_t *L) {
    zm_hmcs_destroy(&L->high_p);
    zm_mcs_destroy(&L->low_p);
    return 0;
}
