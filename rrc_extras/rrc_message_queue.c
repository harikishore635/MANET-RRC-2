/*
 * RRC Message Queue System - Implementation
 * Thread-safe message queue with POSIX semaphores and mutexes
 */

#include "rrc_message_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

// Global message queues
MessageQueue rrc_to_olsr_queue;
MessageQueue olsr_to_rrc_queue;
MessageQueue rrc_to_tdma_queue;
MessageQueue tdma_to_rrc_queue;
MessageQueue rrc_to_phy_queue;
MessageQueue phy_to_rrc_queue;
MessageQueue app_to_rrc_queue;
MessageQueue rrc_to_app_queue;
MessageQueue mac_to_rrc_relay_queue;

// Request ID counter
static uint32_t global_request_id = 0;
static pthread_mutex_t request_id_mutex = PTHREAD_MUTEX_INITIALIZER;

// Generate unique request ID
uint32_t generate_request_id(void) {
    pthread_mutex_lock(&request_id_mutex);
    uint32_t id = ++global_request_id;
    pthread_mutex_unlock(&request_id_mutex);
    return id;
}

// Initialize a message queue
int message_queue_init(MessageQueue *mq, const char *name) {
    if (!mq || !name) {
        return -1;
    }
    
    memset(mq, 0, sizeof(MessageQueue));
    strncpy(mq->name, name, sizeof(mq->name) - 1);
    
    mq->read_index = 0;
    mq->write_index = 0;
    mq->count = 0;
    mq->enqueue_count = 0;
    mq->dequeue_count = 0;
    mq->overflow_count = 0;
    
    // Initialize mutex
    if (pthread_mutex_init(&mq->mutex, NULL) != 0) {
        fprintf(stderr, "Failed to initialize mutex for queue %s\n", name);
        return -1;
    }
    
    // Initialize semaphores
    if (sem_init(&mq->empty_slots, 0, MESSAGE_QUEUE_SIZE) != 0) {
        fprintf(stderr, "Failed to initialize empty_slots for queue %s\n", name);
        pthread_mutex_destroy(&mq->mutex);
        return -1;
    }
    
    if (sem_init(&mq->filled_slots, 0, 0) != 0) {
        fprintf(stderr, "Failed to initialize filled_slots for queue %s\n", name);
        sem_destroy(&mq->empty_slots);
        pthread_mutex_destroy(&mq->mutex);
        return -1;
    }
    
    printf("MessageQueue '%s' initialized (size=%d)\n", name, MESSAGE_QUEUE_SIZE);
    return 0;
}

// Cleanup message queue
void message_queue_cleanup(MessageQueue *mq) {
    if (!mq) {
        return;
    }
    
    sem_destroy(&mq->empty_slots);
    sem_destroy(&mq->filled_slots);
    pthread_mutex_destroy(&mq->mutex);
    
    printf("MessageQueue '%s' cleaned up (enq=%u, deq=%u, ovf=%u)\n",
           mq->name, mq->enqueue_count, mq->dequeue_count, mq->overflow_count);
}

// Enqueue message with timeout
int message_queue_enqueue(MessageQueue *mq, const LayerMessage *msg, int timeout_ms) {
    if (!mq || !msg) {
        return -1;
    }
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    
    // Wait for empty slot
    if (sem_timedwait(&mq->empty_slots, &ts) != 0) {
        if (errno == ETIMEDOUT) {
            pthread_mutex_lock(&mq->mutex);
            mq->overflow_count++;
            pthread_mutex_unlock(&mq->mutex);
        }
        return -1;
    }
    
    // Critical section
    pthread_mutex_lock(&mq->mutex);
    
    mq->buffer[mq->write_index] = *msg;
    mq->write_index = (mq->write_index + 1) % MESSAGE_QUEUE_SIZE;
    mq->count++;
    mq->enqueue_count++;
    
    pthread_mutex_unlock(&mq->mutex);
    
    // Signal filled slot
    sem_post(&mq->filled_slots);
    
    return 0;
}

// Dequeue message with timeout
int message_queue_dequeue(MessageQueue *mq, LayerMessage *msg, int timeout_ms) {
    if (!mq || !msg) {
        return -1;
    }
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    
    // Wait for filled slot
    if (sem_timedwait(&mq->filled_slots, &ts) != 0) {
        return -1;
    }
    
    // Critical section
    pthread_mutex_lock(&mq->mutex);
    
    *msg = mq->buffer[mq->read_index];
    mq->read_index = (mq->read_index + 1) % MESSAGE_QUEUE_SIZE;
    mq->count--;
    mq->dequeue_count++;
    
    pthread_mutex_unlock(&mq->mutex);
    
    // Signal empty slot
    sem_post(&mq->empty_slots);
    
    return 0;
}

// Check if queue has messages
bool message_queue_has_messages(MessageQueue *mq) {
    if (!mq) {
        return false;
    }
    
    pthread_mutex_lock(&mq->mutex);
    bool has_msgs = (mq->count > 0);
    pthread_mutex_unlock(&mq->mutex);
    
    return has_msgs;
}

// Initialize all message queues
void init_all_message_queues(void) {
    printf("\n=== Initializing Message Queue System ===\n");
    
    message_queue_init(&rrc_to_olsr_queue, "RRC→OLSR");
    message_queue_init(&olsr_to_rrc_queue, "OLSR→RRC");
    message_queue_init(&rrc_to_tdma_queue, "RRC→TDMA");
    message_queue_init(&tdma_to_rrc_queue, "TDMA→RRC");
    message_queue_init(&rrc_to_phy_queue, "RRC→PHY");
    message_queue_init(&phy_to_rrc_queue, "PHY→RRC");
    message_queue_init(&app_to_rrc_queue, "APP→RRC");
    message_queue_init(&rrc_to_app_queue, "RRC→APP");
    message_queue_init(&mac_to_rrc_relay_queue, "MAC→RRC(relay)");
    
    printf("=== Message Queue System Ready ===\n\n");
}

// Cleanup all message queues
void cleanup_all_message_queues(void) {
    printf("\n=== Cleaning Up Message Queue System ===\n");
    
    message_queue_cleanup(&rrc_to_olsr_queue);
    message_queue_cleanup(&olsr_to_rrc_queue);
    message_queue_cleanup(&rrc_to_tdma_queue);
    message_queue_cleanup(&tdma_to_rrc_queue);
    message_queue_cleanup(&rrc_to_phy_queue);
    message_queue_cleanup(&phy_to_rrc_queue);
    message_queue_cleanup(&app_to_rrc_queue);
    message_queue_cleanup(&rrc_to_app_queue);
    message_queue_cleanup(&mac_to_rrc_relay_queue);
    
    printf("=== Message Queue System Shutdown Complete ===\n\n");
}

// Print all queue statistics
void print_all_message_queue_stats(void) {
    printf("\n╔═══════════════════════════════════════════════════════╗\n");
    printf("║          Message Queue System Statistics             ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n\n");
    
    MessageQueue *queues[] = {
        &rrc_to_olsr_queue, &olsr_to_rrc_queue,
        &rrc_to_tdma_queue, &tdma_to_rrc_queue,
        &rrc_to_phy_queue, &phy_to_rrc_queue,
        &app_to_rrc_queue, &rrc_to_app_queue,
        &mac_to_rrc_relay_queue
    };
    
    for (int i = 0; i < 9; i++) {
        MessageQueue *mq = queues[i];
        pthread_mutex_lock(&mq->mutex);
        printf("%-20s | count:%2d | enq:%5u | deq:%5u | ovf:%3u\n",
               mq->name, mq->count, mq->enqueue_count, 
               mq->dequeue_count, mq->overflow_count);
        pthread_mutex_unlock(&mq->mutex);
    }
    
    printf("\n");
}
