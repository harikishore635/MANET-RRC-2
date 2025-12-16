#ifndef RRC_SHARED_MEMORY_H
#define RRC_SHARED_MEMORY_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// Forward declaration
struct frame;

#define RRC_QUEUE_SIZE 20
#define MAX_NEIGHBORS 16
#define NUM_PRIORITY_QUEUES 4

// RRC Priority Data Queues (shared memory)
typedef struct {
    struct frame frames[RRC_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
    volatile int count;
    pthread_mutex_t mutex;
} rrc_priority_queue_t;

// RRC Relay Queue (shared memory)
typedef struct {
    struct frame frames[RRC_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
    volatile int count;
    pthread_mutex_t mutex;
} rrc_relay_queue_t;

// RRC NC (Network Control) Queue (shared memory)
typedef struct {
    struct frame frames[RRC_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
    volatile int count;
    volatile int assigned_nc_slot;  // This node's assigned NC slot (9 or 10)
    pthread_mutex_t mutex;
} rrc_nc_queue_t;

// Neighbor Information (shared memory)
typedef struct {
    uint8_t node_id;
    bool is_active;
    int assigned_nc_slot;
    uint32_t last_heard_ms;
} neighbor_info_t;

// RRC Shared Memory Block
typedef struct {
    // Data queues by priority
    rrc_priority_queue_t priority_queues[NUM_PRIORITY_QUEUES];
    
    // Relay queue for forwarding packets
    rrc_relay_queue_t relay_queue;
    
    // Network control queue (Hello messages, beacons)
    rrc_nc_queue_t nc_queue;
    
    // Neighbor table
    neighbor_info_t neighbors[MAX_NEIGHBORS];
    volatile int neighbor_count;
    pthread_mutex_t neighbor_mutex;
    
    // Control flags
    volatile bool rrc_initialized;
    volatile uint32_t frame_sequence;
    
} rrc_shared_memory_t;

// Global shared memory pointer
extern rrc_shared_memory_t* rrc_shm;

// Shared memory initialization
bool rrc_shared_memory_init(void);
void rrc_shared_memory_cleanup(void);

// Queue helper macros for atomic operations
#define RRC_QUEUE_IS_EMPTY(q) ((q)->count == 0)
#define RRC_QUEUE_IS_FULL(q) ((q)->count >= RRC_QUEUE_SIZE)

#endif // RRC_SHARED_MEMORY_H
