/**
 * POSIX Message Queue Adapter Layer for RRC Integration
 * Wrappers for mq_open, mq_send, mq_receive with timeout support
 */

#ifndef RRC_MQ_ADAPTERS_H
#define RRC_MQ_ADAPTERS_H

#include "rrc_posix_mq_defs.h"
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// ============================================================================
// MESSAGE QUEUE CONTEXT
// ============================================================================

typedef struct {
    mqd_t mqd;
    char mq_name[64];
    struct mq_attr attr;
    MQStats stats;
    bool initialized;
    bool is_read;   // true if opened for reading
    bool is_write;  // true if opened for writing
} MQContext;

// ============================================================================
// MESSAGE QUEUE INITIALIZATION
// ============================================================================

/**
 * Initialize POSIX message queue
 * @param ctx Message queue context
 * @param mq_name Queue name (e.g., MQ_APP_TO_RRC)
 * @param flags O_RDONLY, O_WRONLY, or O_RDWR
 * @param create_new If true, create with O_CREAT
 * @return 0 on success, -1 on error
 */
static inline int mq_init(MQContext* ctx, const char* mq_name, int flags, bool create_new) {
    if (!ctx || !mq_name) return -1;
    
    memset(ctx, 0, sizeof(MQContext));
    strncpy(ctx->mq_name, mq_name, sizeof(ctx->mq_name) - 1);
    
    // Set queue attributes
    ctx->attr.mq_flags = 0;
    ctx->attr.mq_maxmsg = 10;  // Max messages in queue
    ctx->attr.mq_msgsize = MAX_MQ_MSG_SIZE;
    ctx->attr.mq_curmsgs = 0;
    
    if (create_new) {
        mq_unlink(mq_name);  // Remove any existing
        ctx->mqd = mq_open(mq_name, flags | O_CREAT, 0666, &ctx->attr);
    } else {
        ctx->mqd = mq_open(mq_name, flags);
    }
    
    if (ctx->mqd == (mqd_t)-1) {
        perror("mq_open");
        fprintf(stderr, "Failed to open queue: %s\n", mq_name);
        return -1;
    }
    
    ctx->is_read = (flags & O_RDONLY) || (flags & O_RDWR);
    ctx->is_write = (flags & O_WRONLY) || (flags & O_RDWR);
    ctx->initialized = true;
    
    return 0;
}

/**
 * Cleanup message queue
 */
static inline void mq_cleanup(MQContext* ctx, bool unlink) {
    if (!ctx || !ctx->initialized) return;
    
    if (ctx->mqd != (mqd_t)-1) {
        mq_close(ctx->mqd);
    }
    
    if (unlink) {
        mq_unlink(ctx->mq_name);
    }
    
    memset(ctx, 0, sizeof(MQContext));
}

// ============================================================================
// MESSAGE SEND/RECEIVE OPERATIONS
// ============================================================================

/**
 * Send message to queue (blocking)
 * @param priority Message priority (0 = lowest)
 */
static inline int mq_send_msg(MQContext* ctx, const void* msg, size_t msg_size, 
                             unsigned int priority) {
    if (!ctx || !ctx->initialized || !msg) return -1;
    if (!ctx->is_write) return -1;
    if (msg_size > MAX_MQ_MSG_SIZE) return -1;
    
    if (mq_send(ctx->mqd, (const char*)msg, msg_size, priority) < 0) {
        ctx->stats.error_count++;
        return -1;
    }
    
    ctx->stats.enqueue_count++;
    return 0;
}

/**
 * Receive message from queue (blocking)
 */
static inline ssize_t mq_recv_msg(MQContext* ctx, void* msg, size_t msg_size, 
                                 unsigned int* priority) {
    if (!ctx || !ctx->initialized || !msg) return -1;
    if (!ctx->is_read) return -1;
    
    ssize_t bytes = mq_receive(ctx->mqd, (char*)msg, msg_size, priority);
    
    if (bytes < 0) {
        ctx->stats.error_count++;
        return -1;
    }
    
    ctx->stats.dequeue_count++;
    return bytes;
}

/**
 * Receive message with timeout
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes received, -1 on error, -2 on timeout
 */
static inline ssize_t mq_recv_msg_timeout(MQContext* ctx, void* msg, size_t msg_size,
                                         unsigned int* priority, uint32_t timeout_ms) {
    if (!ctx || !ctx->initialized || !msg) return -1;
    if (!ctx->is_read) return -1;
    
    struct timespec abs_timeout;
    clock_gettime(CLOCK_REALTIME, &abs_timeout);
    
    abs_timeout.tv_sec += timeout_ms / 1000;
    abs_timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    
    // Handle nanosecond overflow
    if (abs_timeout.tv_nsec >= 1000000000) {
        abs_timeout.tv_sec++;
        abs_timeout.tv_nsec -= 1000000000;
    }
    
    ssize_t bytes = mq_timedreceive(ctx->mqd, (char*)msg, msg_size, 
                                   priority, &abs_timeout);
    
    if (bytes < 0) {
        if (errno == ETIMEDOUT) {
            ctx->stats.timeout_count++;
            return -2;  // Timeout
        }
        ctx->stats.error_count++;
        return -1;  // Error
    }
    
    ctx->stats.dequeue_count++;
    return bytes;
}

/**
 * Try to receive message without blocking
 * @return Number of bytes received, 0 if empty, -1 on error
 */
static inline ssize_t mq_try_recv_msg(MQContext* ctx, void* msg, size_t msg_size,
                                     unsigned int* priority) {
    if (!ctx || !ctx->initialized || !msg) return -1;
    if (!ctx->is_read) return -1;
    
    // Set queue to non-blocking
    struct mq_attr old_attr, new_attr;
    mq_getattr(ctx->mqd, &old_attr);
    
    new_attr = old_attr;
    new_attr.mq_flags = O_NONBLOCK;
    mq_setattr(ctx->mqd, &new_attr, NULL);
    
    ssize_t bytes = mq_receive(ctx->mqd, (char*)msg, msg_size, priority);
    
    // Restore blocking mode
    mq_setattr(ctx->mqd, &old_attr, NULL);
    
    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // Queue empty
        }
        ctx->stats.error_count++;
        return -1;  // Error
    }
    
    ctx->stats.dequeue_count++;
    return bytes;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Generate unique request ID
 */
static inline uint32_t generate_request_id(void) {
    static uint32_t counter = 0;
    return __sync_fetch_and_add(&counter, 1);
}

/**
 * Get current timestamp in milliseconds
 */
static inline uint32_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/**
 * Initialize message header
 */
static inline void init_message_header(MessageHeader* hdr, MessageType msg_type) {
    if (!hdr) return;
    memset(hdr, 0, sizeof(MessageHeader));
    hdr->request_id = generate_request_id();
    hdr->timestamp_ms = get_timestamp_ms();
    hdr->msg_type = msg_type;
}

/**
 * Get MQ statistics
 */
static inline void mq_get_stats(MQContext* ctx, MQStats* stats) {
    if (!ctx || !stats) return;
    memcpy(stats, &ctx->stats, sizeof(MQStats));
}

/**
 * Reset MQ statistics
 */
static inline void mq_reset_stats(MQContext* ctx) {
    if (!ctx) return;
    memset(&ctx->stats, 0, sizeof(MQStats));
}

/**
 * Get queue attributes (current message count, etc.)
 */
static inline int mq_get_attr(MQContext* ctx, struct mq_attr* attr) {
    if (!ctx || !ctx->initialized || !attr) return -1;
    return mq_getattr(ctx->mqd, attr);
}

// ============================================================================
// DATA TYPE QUEUE ROUTING
// ============================================================================

/**
 * Get the appropriate queue name based on data type
 */
static inline const char* get_datatype_queue_name(DataType dtype) {
    switch (dtype) {
        case DATA_TYPE_MSG:     return MQ_RRC_MSG_QUEUE;
        case DATA_TYPE_VOICE:   return MQ_RRC_VOICE_QUEUE;
        case DATA_TYPE_VIDEO:   return MQ_RRC_VIDEO_QUEUE;
        case DATA_TYPE_FILE:    return MQ_RRC_FILE_QUEUE;
        case DATA_TYPE_RELAY:   return MQ_RRC_RELAY_QUEUE;
        case DATA_TYPE_PTT:     return MQ_RRC_PTT_QUEUE;
        default:                return MQ_RRC_UNKNOWN_QUEUE;
    }
}

/**
 * Route message to appropriate data-type queue
 */
static inline int route_to_datatype_queue(DataType dtype, const void* msg, 
                                         size_t msg_size, unsigned int priority) {
    const char* mq_name = get_datatype_queue_name(dtype);
    
    MQContext temp_ctx;
    if (mq_init(&temp_ctx, mq_name, O_WRONLY, false) < 0) {
        return -1;
    }
    
    int ret = mq_send_msg(&temp_ctx, msg, msg_size, priority);
    mq_cleanup(&temp_ctx, false);
    
    return ret;
}

#endif // RRC_MQ_ADAPTERS_H
