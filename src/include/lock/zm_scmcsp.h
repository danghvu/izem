/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 *
 * Author: Hoang-Vu Dang (danghvu@gmail.com).
 */

#ifndef _ZM_SCMCSP_H
#define _ZM_SCMCSP_H
#include "lock/zm_lock_types.h"

int zm_scmcsp_init(zm_scmcsp_t *);
int zm_scmcsp_destroy(zm_scmcsp_t *);

int zm_scmcsp_acquire(zm_scmcsp_t);
int zm_scmcsp_acquire_low(zm_scmcsp_t);
int zm_scmcsp_tryacq(zm_scmcsp_t, int*);
int zm_scmcsp_release(zm_scmcsp_t);
int zm_scmcsp_nowaiters(zm_scmcsp_t);

int zm_scmcsp_acquire_c(zm_scmcsp_t, void*);
int zm_scmcsp_tryacq_c(zm_scmcsp_t, void*, int*);
int zm_scmcsp_release_c(zm_scmcsp_t, void*);
int zm_scmcsp_nowaiters_c(zm_scmcsp_t, void*);

#endif /* _ZM_SCMCSP_H */
