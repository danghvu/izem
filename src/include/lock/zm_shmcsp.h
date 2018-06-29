/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 *
 * Author: Hoang-Vu Dang (danghvu@gmail.com).
 */
#include "zm_hmcs.h"

int zm_shmcsp_init(zm_shmcsp_t *);
int zm_shmcsp_destroy(zm_shmcsp_t *);

int zm_shmcsp_acquire(zm_shmcsp_t *);
int zm_shmcsp_acquire_low(zm_shmcsp_t*);
int zm_shmcsp_tryacq(zm_shmcsp_t *, int*);
int zm_shmcsp_tryacq_low(zm_shmcsp_t*, int*);
int zm_shmcsp_release(zm_shmcsp_t *);

int zm_shmcsp_acquire_c(zm_shmcsp_t *, zm_mcs_qnode_t*);
int zm_shmcsp_acquire_low_c(zm_shmcsp_t*, zm_mcs_qnode_t*);
int zm_shmcsp_release_c(zm_shmcsp_t *, zm_mcs_qnode_t *);
