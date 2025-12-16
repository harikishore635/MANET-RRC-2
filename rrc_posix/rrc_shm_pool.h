/**
 * Shared Memory Pool Management for RRC POSIX Integration
 * Pool-based allocation with index referencing to avoid large copies
 */

#ifndef RRC_SHM_POOL_H
#define RRC_SHM_POOL_H

#include "rrc_posix_mq_defs.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// ============================================================================
// POOL MANAGEMENT STRUCTURES
// ============================================================================

typedef struct {
    int shm_fd;
    void* base_ptr;
    size_t entry_size;
    size_t pool_size;
    PoolStats stats;
    bool initialized;
} PoolContext;

// ============================================================================
// POOL INITIALIZATION AND CLEANUP
// ============================================================================

/**
 * Initialize shared memory pool (creates or attaches)
 * @param ctx Pool context to initialize
 * @param shm_name Shared memory name (e.g., SHM_FRAME_POOL)
 * @param entry_size Size of each pool entry
 * @param pool_size Number of entries in pool
 * @param create_new If true, create new pool; if false, attach existing
 * @return 0 on success, -1 on error
 */
static inline int pool_init(PoolContext* ctx, const char* shm_name, 
                           size_t entry_size, size_t pool_size, bool create_new) {
    if (!ctx || !shm_name) return -1;
    
    memset(ctx, 0, sizeof(PoolContext));
    ctx->entry_size = entry_size;
    ctx->pool_size = pool_size;
    
    size_t total_size = entry_size * pool_size;
    
    if (create_new) {
        // Create new shared memory
        shm_unlink(shm_name);  // Remove any existing
        ctx->shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
        if (ctx->shm_fd < 0) {
            perror("shm_open create");
            return -1;
        }
        
        if (ftruncate(ctx->shm_fd, total_size) < 0) {
            perror("ftruncate");
            close(ctx->shm_fd);
            return -1;
        }
    } else {
        // Attach to existing shared memory
        ctx->shm_fd = shm_open(shm_name, O_RDWR, 0666);
        if (ctx->shm_fd < 0) {
            perror("shm_open attach");
            return -1;
        }
    }
    
    // Map shared memory to process address space
    ctx->base_ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, 
                        MAP_SHARED, ctx->shm_fd, 0);
    if (ctx->base_ptr == MAP_FAILED) {
        perror("mmap");
        close(ctx->shm_fd);
        return -1;
    }
    
    // Zero out memory if creating new pool
    if (create_new) {
        memset(ctx->base_ptr, 0, total_size);
    }
    
    ctx->initialized = true;
    return 0;
}

/**
 * Cleanup and unmap shared memory pool
 */
static inline void pool_cleanup(PoolContext* ctx, const char* shm_name, bool unlink) {
    if (!ctx || !ctx->initialized) return;
    
    size_t total_size = ctx->entry_size * ctx->pool_size;
    
    if (ctx->base_ptr != NULL && ctx->base_ptr != MAP_FAILED) {
        munmap(ctx->base_ptr, total_size);
    }
    
    if (ctx->shm_fd >= 0) {
        close(ctx->shm_fd);
    }
    
    if (unlink && shm_name) {
        shm_unlink(shm_name);
    }
    
    memset(ctx, 0, sizeof(PoolContext));
}

// ============================================================================
// FRAME POOL OPERATIONS
// ============================================================================

/**
 * Allocate a free frame from the pool
 * @return pool_index on success, -1 if pool full
 */
static inline int frame_pool_alloc(PoolContext* ctx) {
    if (!ctx || !ctx->initialized) return -1;
    
    FramePoolEntry* entries = (FramePoolEntry*)ctx->base_ptr;
    
    for (size_t i = 0; i < ctx->pool_size; i++) {
        if (!entries[i].in_use) {
            entries[i].in_use = true;
            entries[i].valid = false;
            memset(entries[i].payload, 0, PAYLOAD_SIZE_BYTES);
            
            ctx->stats.alloc_count++;
            ctx->stats.in_use_count++;
            return (int)i;
        }
    }
    
    ctx->stats.overflow_count++;
    return -1;  // Pool full
}

/**
 * Release a frame back to the pool
 */
static inline int frame_pool_release(PoolContext* ctx, uint16_t pool_index) {
    if (!ctx || !ctx->initialized) return -1;
    if (pool_index >= ctx->pool_size) return -1;
    
    FramePoolEntry* entries = (FramePoolEntry*)ctx->base_ptr;
    
    if (!entries[pool_index].in_use) {
        return -1;  // Already released
    }
    
    entries[pool_index].in_use = false;
    entries[pool_index].valid = false;
    
    ctx->stats.release_count++;
    ctx->stats.in_use_count--;
    return 0;
}

/**
 * Get pointer to frame at pool_index
 */
static inline FramePoolEntry* frame_pool_get(PoolContext* ctx, uint16_t pool_index) {
    if (!ctx || !ctx->initialized) return NULL;
    if (pool_index >= ctx->pool_size) return NULL;
    
    FramePoolEntry* entries = (FramePoolEntry*)ctx->base_ptr;
    return &entries[pool_index];
}

/**
 * Copy data into frame pool entry
 */
static inline int frame_pool_set(PoolContext* ctx, uint16_t pool_index, 
                                const FramePoolEntry* data) {
    if (!ctx || !ctx->initialized || !data) return -1;
    if (pool_index >= ctx->pool_size) return -1;
    
    FramePoolEntry* entries = (FramePoolEntry*)ctx->base_ptr;
    if (!entries[pool_index].in_use) return -1;
    
    memcpy(&entries[pool_index], data, sizeof(FramePoolEntry));
    entries[pool_index].in_use = true;
    entries[pool_index].valid = true;
    
    return 0;
}

// ============================================================================
// APP PACKET POOL OPERATIONS
// ============================================================================

static inline int app_pool_alloc(PoolContext* ctx) {
    if (!ctx || !ctx->initialized) return -1;
    
    AppPacketPoolEntry* entries = (AppPacketPoolEntry*)ctx->base_ptr;
    
    for (size_t i = 0; i < ctx->pool_size; i++) {
        if (!entries[i].in_use) {
            entries[i].in_use = true;
            memset(entries[i].payload, 0, PAYLOAD_SIZE_BYTES);
            
            ctx->stats.alloc_count++;
            ctx->stats.in_use_count++;
            return (int)i;
        }
    }
    
    ctx->stats.overflow_count++;
    return -1;
}

static inline int app_pool_release(PoolContext* ctx, uint16_t pool_index) {
    if (!ctx || !ctx->initialized) return -1;
    if (pool_index >= ctx->pool_size) return -1;
    
    AppPacketPoolEntry* entries = (AppPacketPoolEntry*)ctx->base_ptr;
    
    if (!entries[pool_index].in_use) return -1;
    
    entries[pool_index].in_use = false;
    ctx->stats.release_count++;
    ctx->stats.in_use_count--;
    return 0;
}

static inline AppPacketPoolEntry* app_pool_get(PoolContext* ctx, uint16_t pool_index) {
    if (!ctx || !ctx->initialized) return NULL;
    if (pool_index >= ctx->pool_size) return NULL;
    
    AppPacketPoolEntry* entries = (AppPacketPoolEntry*)ctx->base_ptr;
    return &entries[pool_index];
}

static inline int app_pool_set(PoolContext* ctx, uint16_t pool_index, 
                               const AppPacketPoolEntry* data) {
    if (!ctx || !ctx->initialized || !data) return -1;
    if (pool_index >= ctx->pool_size) return -1;
    
    AppPacketPoolEntry* entries = (AppPacketPoolEntry*)ctx->base_ptr;
    if (!entries[pool_index].in_use) return -1;
    
    memcpy(&entries[pool_index], data, sizeof(AppPacketPoolEntry));
    entries[pool_index].in_use = true;
    
    return 0;
}

// ============================================================================
// POOL STATISTICS
// ============================================================================

static inline void pool_get_stats(PoolContext* ctx, PoolStats* stats) {
    if (!ctx || !stats) return;
    memcpy(stats, &ctx->stats, sizeof(PoolStats));
}

static inline void pool_reset_stats(PoolContext* ctx) {
    if (!ctx) return;
    memset(&ctx->stats, 0, sizeof(PoolStats));
}

#endif // RRC_SHM_POOL_H
