#include <stdlib.h>
#include <hwloc.h>

#include "lock/zm_scmcsp.h"
#include "lock/zm_mcs.h"

#define LOCKED 1
#define UNLOCKED 0 

#define GLOBAL_RELEASED 0
#define LOCAL_RELEASED 1

static inline int get_core_count()
{
    hwloc_topology_t topo;
    hwloc_topology_init(&topo);
    hwloc_topology_load(topo);
    return hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_CORE);
}

static inline int get_core_id()
{
    hwloc_topology_t topo;
    hwloc_obj_t obj;
    hwloc_topology_init(&topo);
    hwloc_topology_load(topo);
    hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
    hwloc_get_cpubind(topo, cpuset, HWLOC_CPUBIND_THREAD);
    obj = hwloc_get_obj_inside_cpuset_by_type(topo, cpuset, HWLOC_OBJ_CORE, 0);
    hwloc_bitmap_free(cpuset);
    return obj->logical_index;
}

static inline int get_node_id()
{
    int i;
    hwloc_topology_t topo;
    hwloc_obj_t obj;
    hwloc_nodeset_t nodeset = hwloc_bitmap_alloc();
    hwloc_topology_init(&topo);
    hwloc_topology_load(topo);
    hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
    hwloc_get_cpubind(topo, cpuset, HWLOC_CPUBIND_THREAD);
    hwloc_cpuset_to_nodeset(topo, cpuset, nodeset);
    hwloc_bitmap_foreach_begin(i, nodeset) {
        obj = hwloc_get_numanode_obj_by_os_index(topo, i);
    } hwloc_bitmap_foreach_end();
    hwloc_bitmap_free(cpuset);
    hwloc_bitmap_free(nodeset);
    return obj->logical_index;
}

static inline int get_max_nodes()
{
    hwloc_topology_t topo;
    hwloc_topology_init(&topo);
    hwloc_topology_load(topo);
    return hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_NUMANODE);
}

typedef struct {
    zm_mcs_qnode_t* high_p_ctx;
    zm_mcs_qnode_t* low_p_ctx;
    int id;
} h_lock_private;

typedef struct {
    zm_mcs_t mcs;
    zm_atomic_flag_t state;
} numa_lock_t;

typedef struct {
   zm_atomic_flag_t root;
   numa_lock_t* numa;
} coho_t;

typedef struct zm_scmcsp {
    // high priority.
   coho_t high_p __attribute__((aligned(64)));

   // low priority.
   zm_mcs_t low_p __attribute__((aligned(64)));

   // filter lock for switching between.
   zm_atomic_flag_t filter __attribute__((aligned(64)));

   // selfish lock.
   zm_atomic_flag_t self_p __attribute__((aligned(64)));

} zm_scmcsp __attribute__((aligned(64)));

static __thread h_lock_private _tl_lock = { 0, 0, -1 };

static inline __attribute__((always_inline))
void tns_acquire(zm_atomic_flag_t * l)
{
    do {
        while (zm_atomic_load(l, zm_memord_acquire) == 1)
            ;
    } while (zm_atomic_flag_test_and_set(l, zm_memord_acq_rel)); 
}

static inline __attribute__((always_inline))
void tns_release(zm_atomic_flag_t * l)
{
    zm_atomic_flag_clear(l, zm_memord_release);
}

static inline __attribute__((always_inline))
void coho_acquire(zm_atomic_flag_t* root, numa_lock_t* ll, zm_mcs_qnode_t *c)
{
    // Acquire the numa node lock. {{{
    zm_mcs_acquire_c(ll->mcs, c);
    // }}}

    if (zm_atomic_load(&(ll->state), zm_memord_acquire) == GLOBAL_RELEASED) {
        // Acquire the root. {{{
        tns_acquire(root);
        // }}}
    }
}

static inline __attribute__((always_inline))
void coho_release(zm_atomic_flag_t* root, numa_lock_t* ll, zm_mcs_qnode_t* c)
{
    // If none is in my group.
    if (zm_mcs_nowaiters_c(ll->mcs, c)) {
        // Release the root. {{{
        tns_release(root);
        // }}}

        // Set the state to global release.
        zm_atomic_store(&ll->state, GLOBAL_RELEASED, zm_memord_release);
    } else {
        // Local release so global is still locked.
        zm_atomic_store(&ll->state, LOCAL_RELEASED, zm_memord_release);
    }

    // Release the NUMA lock {{{
    zm_mcs_release_c(ll->mcs, c);
    // }}}
}

static inline void coho_init(coho_t* coho, int numa_nodes_count)
{
    int i;
    posix_memalign((void**) &coho->numa, ZM_CACHELINE_SIZE, numa_nodes_count * sizeof(numa_lock_t));

    for (i = 0; i < numa_nodes_count; i++) {
        numa_lock_t* ll = &coho->numa[i];
        zm_mcs_init(&ll->mcs);
        zm_atomic_store(&ll->state, GLOBAL_RELEASED, zm_memord_release);
    }

    zm_atomic_store(&coho->root, UNLOCKED, zm_memord_release);
}

/* SHCLHP lock. */
int zm_scmcsp_init(zm_scmcsp** lock_ptr) {
    zm_scmcsp *lock = NULL;
    posix_memalign((void**) &lock, ZM_CACHELINE_SIZE, sizeof(struct zm_scmcsp));
    int numa_nodes_count = get_max_nodes();

    // Initialize the selfish.
    zm_atomic_store(&lock->filter, 0, zm_memord_release);
    zm_atomic_store(&lock->self_p, UNLOCKED, zm_memord_release);

    // Initialize low priority.
    // coho_init(&lock->low_p, numa_nodes_count);
    zm_mcs_init(&lock->low_p);

    // Initialize high priority .
    coho_init(&lock->high_p, numa_nodes_count);
   
    *lock_ptr = lock;
    return 0;
}

int zm_scmcsp_destroy(zm_scmcsp** lock) {
    free((*lock));
    *lock = NULL;
    return 0;
}

static inline void init_new_lock(h_lock_private* p)
{
    p->high_p_ctx = calloc(1, sizeof(zm_mcs_qnode_t));
    p->low_p_ctx = calloc(1, sizeof(zm_mcs_qnode_t));
    p->id = get_node_id();
}

int zm_scmcsp_acquire(zm_scmcsp* lock) {
    // Selfish lock is for the champion, use swap for better bias..
    if (zm_unlikely(zm_atomic_exchange(&lock->self_p, LOCKED,
                                       zm_memord_acq_rel) == LOCKED)) {
        // Then the usual lock.
        h_lock_private* p = &_tl_lock;

        if (zm_unlikely(p->id == -1))
            init_new_lock(p);

        numa_lock_t* ll = &(lock->high_p.numa[p->id]);
        coho_acquire(&lock->high_p.root, ll, p->high_p_ctx);

        // Spin now.. should be alone Selfishly acquire... {{{
        zm_atomic_store(&lock->filter, 1, zm_memord_release);
        tns_acquire(&lock->self_p);
        zm_atomic_store(&lock->filter, 0, zm_memord_relaxed);
        // Selfishly acquired----- }}}
        
        // Unlock to elect the next person to wait.
        coho_release(&lock->high_p.root, ll, p->high_p_ctx);
    }
    return 0;
}

int zm_scmcsp_acquire_low(zm_scmcsp *lock) {
    // Give chance to higher thread if there is.
    h_lock_private* p = &_tl_lock;

    if (zm_unlikely(p->id == -1))
        init_new_lock(p);

    // Queue first.
    // numa_lock_t* ll = &(lock->low_p.numa[p->id]);
    // coho_acquire(&lock->low_p.root, ll, p->low_p_ctx);
    zm_mcs_acquire_c(lock->low_p, p->low_p_ctx);
    
    // Now need to wait for high priority.
    while (zm_atomic_load(&lock->filter, zm_memord_acquire))
        ;
    tns_acquire(&lock->self_p);
        
    // Unlok to elect the next person to wait.
    // coho_release(&lock->low_p.root, ll, p->low_p_ctx);
    zm_mcs_release_c(lock->low_p, p->low_p_ctx);

    return 0;
}

int zm_scmcsp_release(zm_scmcsp* lock) {
    tns_release(&lock->self_p);
    return 0;
}

int zm_scmcsp_acquire_c(zm_scmcsp *L, void* I) {
    assert(0);
    return 0;
}

int zm_scmcsp_acquire_low_c(zm_scmcsp *L, void* I) {
    assert(0);
    return 0;
}

int zm_scmcsp_release_c(zm_scmcsp *L, void* I) {
    assert(0);
    return 0;
}
