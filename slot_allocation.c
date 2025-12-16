#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include<time.h>

// --- MAC Layer Design Constraints ---
#define QUEUE_SIZE 10
#define PAYLOAD_SIZE_BYTES 16
#define NUM_PRIORITY 4          // 4 data priority queues (0-3)
#define TOTAL_SLOTS 10 
#define SLOT_DURATION_MS 10     // The 10ms fundamental slot duration
#define FRAME_DURATION_MS (TOTAL_SLOTS * SLOT_DURATION_MS) // 100ms frame
#define MAX_SCAN_TIME_MS 200    // Max scan time for cold start

uint8_t node_addr = 0xFE; // Node MAC address for this simulation

// --- TDMA STATE STRUCTURES ---
typedef enum {
    SLOT_TYPE_MV, 
    SLOT_TYPE_DU, 
    SLOT_TYPE_GU, 
    SLOT_TYPE_NC    
} SLOT_TYPE;

typedef enum {
    STATUS_UNSYNCHRONIZED,
    STATUS_MASTER,        /**< Self-designated time master (Cold Start). */
    STATUS_MASTER_HEARD   /**< Synchronized to an external master (Joined). */
} NODE_STATUS;

// Voice Access State Machine for Slot 1 (MV)
typedef enum {
    VOICE_INACTIVE,      /**< Default state, no PTT pressed, no reservation. */
    VOICE_CR_SENT,       /**< PTT pressed, Control Request (CR) sent, awaiting CC. */
    VOICE_ACTIVE_TX      /**< Control Confirm (CC) received, exclusive access to Slot 1 granted. */
} VOICE_STATUS;

struct slot_definition {
    int slot_id;
    SLOT_TYPE type;
    const char* description;
};

// The 10-slot TDMA Frame Schedule (100ms total)
struct slot_definition TDMA_FRAME_SCHEDULE[TOTAL_SLOTS] = {
    {1, SLOT_TYPE_MV, "Voice Reserved (PTT/Prio 0)"}, 
    {2, SLOT_TYPE_DU, "Dynamic Use (Prio 0/1)"}, 
    {3, SLOT_TYPE_DU, "Dynamic Use (Prio 0/1)"}, 
    {4, SLOT_TYPE_DU, "Dynamic Use (Prio 0/1)"},
    {5, SLOT_TYPE_GU, "General Use (Prio 2/3/Relay)"}, 
    {6, SLOT_TYPE_GU, "General Use (Prio 2/3/Relay)"},
    {7, SLOT_TYPE_GU, "General Use (Prio 2/3/Relay)"}, 
    {8, SLOT_TYPE_GU, "General Use (Prio 2/3/Relay)"},
    {9, SLOT_TYPE_NC, "Network Control (Beacon Tx/Rx)"}, 
    {10, SLOT_TYPE_NC, "Network Control (Beacon Tx/Rx)"}
};

// Synchronization and Operational State
struct tdma_sync {
    bool is_synchronized;
    NODE_STATUS status;             /**< M, HM, or UNSYNCHRONIZED. */
    uint32_t local_time_ms;         /**< Current time offset in the 100ms frame. */
    int current_slot_index;
    uint8_t master_mac;             /**< MAC address of the current time master. */
    VOICE_STATUS voice_status;      /**< Current state of voice reservation (CR/CC Handshake). */
};

// Control Frame (Beacon) structure
struct control_frame {
    uint8_t source_mac;
    uint32_t network_timestamp_ms;  /**< The Master's time at transmission. */
    NODE_STATUS source_status; 
};

// --- DATA QUEUE STRUCTURES ---
typedef enum{
    DATA_TYPE_DIGITAL_VOICE, 
    DATA_TYPE_SMS, 
    DATA_TYPE_FILE_TRANSFER, 
    DATA_TYPE_VIDEO_STREAM, 
    DATA_TYPE_ANALOG_VOICE, 
    DATA_TYPE_CR, 
    DATA_TYPE_CC
} DATATYPE;

struct frame{
    uint8_t source_add;
    uint8_t dest_add;
    uint8_t next_hop_add;           /**< Used for relaying logic. */
    int priority;                   /**< QoS priority (0 is highest). */
    DATATYPE data_type;
    char payload[PAYLOAD_SIZE_BYTES];
};

struct queue{
    struct frame item[QUEUE_SIZE];
    int front;
    int back;
};


// -----------------------------------------------------------------------------
// Utility Functions (Queue and TX)
// -----------------------------------------------------------------------------
bool is_empty(struct queue *q) { 
    return (q->front == -1 || q->front > q->back); 
}

void enqueue(struct queue *q, struct frame rx_f){ 
    if (q->back == QUEUE_SIZE - 1) return; 
    if(q->front == -1) q->front = 0;
    q->back = (q->back + 1);
    q->item[q->back] = rx_f;
}
struct frame dequeue(struct queue *q){ 
    struct frame empty_frame = {0};
    if (is_empty(q)) return empty_frame;
    struct frame dequeued_frame = q->item[q->front];
    q->front = (q->front + 1);
    if (q->front > q->back) { q->front = -1; q->back = -1; }
    return dequeued_frame; 
}

/**
 * @brief Simulates transmission: checks queues in strict priority order.
 * This is called only after the scheduler grants access to a slot.
 */
void tx(struct queue *analog_voice_queue, struct queue data_queues[], struct queue *rx_queue){
    // 1. Analog Voice (PTT) has absolute preemption (if reserved)
    if(!is_empty(analog_voice_queue)){ dequeue(analog_voice_queue); printf("    -> [TX] Sent Analog Voice Frame (Highest Prio).\n"); return; }
    
    // 2. Check DTE data queues by priority level (0 is highest priority)
    for(int i = 0; i < NUM_PRIORITY; i++) {
        if(!is_empty(&data_queues[i])){ dequeue(&data_queues[i]); printf("    -> [TX] Sent DTE Data Frame (Priority %d).\n", i); return; }
    }
    
    // 3. Check RX Relay queue (lowest priority access)
    if(!is_empty(rx_queue)){ dequeue(rx_queue); printf("    -> [TX] Sent RX Relay Frame (Relay Prio).\n"); return; }
}


// -----------------------------------------------------------------------------
// Voice Reservation (CR/CC Handshake) Logic
// -----------------------------------------------------------------------------

/**
 * @brief Simulates PTT press and sending Control Request (CR).
 * The CR must contend for a GU/DU slot.
 */
bool send_control_request(struct tdma_sync *sync_state, struct queue data_queues[]) {
    if (sync_state->voice_status != VOICE_INACTIVE) {
        printf("[PTT] Voice already active or request pending. Ignoring PTT.\n");
        return false;
    }
    
    // Simulate contention success for the CR packet in a DU/GU slot
    // A 80% chance of CR being sent successfully on the first attempt
    if (rand() % 100 < 80) { 
        // In a real system, the CR packet would be enqueued here (Prio 0)
        // and transmitted in the next available DU/GU slot.
        sync_state->voice_status = VOICE_CR_SENT;
        
        // Simulating the CR packet (Prio 0) being enqueued for transmission
        struct frame cr_frame = { .priority = 0, .data_type = DATA_TYPE_CR, .source_add = node_addr };
        enqueue(&data_queues[0], cr_frame); 

        printf("[HANDSHAKE] PTT Pressed. CR (Prio 0) enqueued. Voice status: CR_SENT.\n");
        return true;
    } else {
        printf("[HANDSHAKE] PTT Pressed. CR Contention FAILED. Must retry PTT.\n");
        return false;
    }
}

/**
 * @brief Simulates receiving Control Confirm (CC) from MPRs.
 * This function grants exclusive access to Slot 1.
 */
void receive_control_confirm(struct tdma_sync *sync_state, struct queue *rx_queue) {
    // Check if the CC packet is present in the RX queue (simulated)
    // NOTE: In a real system, we'd check the contents of the last received frame.
    // Here we just check the state.
    if (sync_state->voice_status == VOICE_CR_SENT) {
        sync_state->voice_status = VOICE_ACTIVE_TX;
        printf("[HANDSHAKE] **Received CC!** EXCLUSIVE SLOT 1 ACCESS GRANTED. Voice status: ACTIVE_TX.\n");
    }
}

/**
 * @brief Releases the voice slot reservation.
 */
void end_call(struct tdma_sync *sync_state, struct queue *analog_voice_queue) {
    sync_state->voice_status = VOICE_INACTIVE;
    // Clear any residual voice frames
    while(!is_empty(analog_voice_queue)) {
        dequeue(analog_voice_queue);
    }
    printf("[HANDSHAKE] Call ended. Slot 1 reservation released. Voice status: INACTIVE.\n");
}

// -----------------------------------------------------------------------------
// Time Synchronization Logic
// -----------------------------------------------------------------------------
void sync_with_received_beacons(struct tdma_sync *sync_state, struct control_frame beacon_list[], int count) {
    if (sync_state->is_synchronized || count == 0) 
        return;

    int32_t total_offset = 0;
    
    printf("\n[RX_NC] Detected %d beacon(s). Calculating averaged offset (tau) for robust sync.\n", count);

    for (int i = 0; i < count; i++) {
        struct control_frame *beacon = &beacon_list[i];
        
        int32_t network_frame_time = beacon->network_timestamp_ms % FRAME_DURATION_MS;
        int32_t local_frame_time = sync_state->local_time_ms % FRAME_DURATION_MS;
        int32_t offset = network_frame_time - local_frame_time;

        total_offset += offset;
    }
    
    int32_t average_offset = total_offset / count;

    sync_state->local_time_ms = (sync_state->local_time_ms + average_offset);
    sync_state->local_time_ms %= FRAME_DURATION_MS; 
    
    if (sync_state->local_time_ms < 0) {
        sync_state->local_time_ms += FRAME_DURATION_MS;
    }

    sync_state->is_synchronized = true;
    sync_state->master_mac = beacon_list[0].source_mac; 
    sync_state->status = STATUS_MASTER_HEARD; // Node joins as a Master Heard node

    printf("[SYNC] **SUCCESS!** Node 0x%02X is synchronized (STATUS: HM).\n", node_addr);
}


// -----------------------------------------------------------------------------
// Slot Allocation and Scheduling Logic
// -----------------------------------------------------------------------------

/**
 * @brief Executes the MAC layer scheduler for one 10ms slot.
 */
void tdma_scheduler_check(struct tdma_sync *sync_state, 
                          struct queue *analog_voice_queue, 
                          struct queue data_queues[], 
                          struct queue *rx_queue) 
{
    int current_slot_index = sync_state->current_slot_index; 
    struct slot_definition current_slot = TDMA_FRAME_SCHEDULE[current_slot_index];
    
    printf("\n--- SCHEDULER: Slot %d (%s) [Time: %u ms] | Voice Status: %d ---\n", 
           current_slot.slot_id, current_slot.description, sync_state->local_time_ms, sync_state->voice_status);

    if (!sync_state->is_synchronized) {
        printf("[SCHEDULER] Unsynchronized. Listening.\n");
        return;
    }

    switch (current_slot.type) {
        
        case SLOT_TYPE_NC: // Network Control Slot (Slots 9, 10)
            if (sync_state->status == STATUS_MASTER || sync_state->status == STATUS_MASTER_HEARD) {
                printf("[NC] Slot reserved for Control. Node 0x%02X transmitting BEACON (Status: %s).\n", 
                       node_addr, sync_state->status == STATUS_MASTER ? "MASTER" : "HM");
            }
            break;

        case SLOT_TYPE_MV: // Voice Reserved Slot (Slot 1)
            // EXCLUSIVE ACCESS CHECK: Only transmit if we have the reservation token (CC received)
            if (sync_state->voice_status == VOICE_ACTIVE_TX) {
                printf("[MV] PTT/Voice Active. **TRANSMIT GRANTED** (Exclusive Voice Access).\n");
                tx(analog_voice_queue, data_queues, rx_queue);
            } else if (!is_empty(&data_queues[0])) {
                 // If the voice slot is free, Prio 0 (Digital Voice/C2) data gets access
                 printf("[MV] Prio 0 data detected. **TRANSMIT GRANTED** (Voice Slot Fallback).\n");
                 tx(analog_voice_queue, data_queues, rx_queue);
            } else {
                printf("[MV] No exclusive voice reservation or Prio 0 data. Listening.\n");
            }
            break;

        case SLOT_TYPE_DU: // Dynamic Use Slot (Slots 2-4)
            // High-priority data contention (Prio 0/1)
            if (!is_empty(&data_queues[0]) || !is_empty(&data_queues[1])) {
                // Assuming high-priority traffic gets preferential access (no random backoff needed)
                printf("[DU] Prio 0/1 data detected. **TRANSMIT GRANTED** (High-Prio Contention).\n");
                tx(analog_voice_queue, data_queues, rx_queue);
            } else {
                printf("[DU] Relevant high-priority queues empty. Listening.\n");
            }
            break;
            
        case SLOT_TYPE_GU: // General Use Slot (Slots 5-8)
            // Low-priority data contention (Prio 2/3 and RX Relay)
            if (!is_empty(&data_queues[2]) || !is_empty(&data_queues[3]) || !is_empty(rx_queue)) {
                
                // CSMA/CA Simulation: 50% chance to transmit
                int rand_val = rand() % 100;
                if (rand_val < 50) { 
                    printf("[GU] Low-Prio/Relay data detected. Contention success! **TRANSMIT GRANTED**.\n");
                    tx(analog_voice_queue, data_queues, rx_queue);
                } else {
                    printf("[GU] Low-Prio/Relay data detected. Contention FAILED (50%% backoff). Listening.\n");
                }
            } else {
                printf("[GU] All relevant data queues empty. Listening.\n");
            }
            break;

        default:
            printf("[MAC] Unknown slot type. Listening.\n");
            break;
    }
}


// -----------------------------------------------------------------------------
// Main Loop: Simulation of Acquisition, Handshake, and TDMA Cycle
// -----------------------------------------------------------------------------
int main(){
    srand(time(NULL)); 
    
    // --- Node Initialization ---
    struct tdma_sync node_sync = {
        .is_synchronized = false,
        .status = STATUS_UNSYNCHRONIZED,
        .local_time_ms = (rand() % TOTAL_SLOTS) * SLOT_DURATION_MS,
        .master_mac = 0x00,
        .voice_status = VOICE_INACTIVE
    };
    node_sync.current_slot_index = node_sync.local_time_ms / SLOT_DURATION_MS;

    // --- Queue Initialization ---
    struct queue analog_voice_queue = { .front = -1, .back = -1 };
    struct queue data_from_l3_queue[NUM_PRIORITY];
    struct queue rx_queue = { .front = -1, .back = -1 };
    for(int i=0;i<NUM_PRIORITY;i++) { data_from_l3_queue[i].front = -1; data_from_l3_queue[i].back = -1; }

    // --- Injecting data for non-voice queues (Prio 3 for GU slot test) ---
    struct frame p3_sms_frame = { .priority = 3, .data_type = DATA_TYPE_SMS };
    enqueue(&data_from_l3_queue[3], p3_sms_frame);   // Prio 3 (SMS) data ready
    enqueue(&rx_queue, p3_sms_frame);                // Relay data ready

    printf("--- TDMA Network ACQUISITION/FORMATION Simulation ---\n");
    
    // --- ACQUISITION PHASE ---
    struct control_frame received_beacons[] = {
        { .source_mac = 0xAA, .network_timestamp_ms = 85, .source_status = STATUS_MASTER }, 
    };
    int num_beacons_received = 1;

    // Forcing synchronization for active TDMA test
    node_sync.local_time_ms = 80; // Start at Slot 9 (NC)
    node_sync.current_slot_index = 8;
    
    printf("[ACQUISITION] Simulating 1 beacon received at %u ms.\n", node_sync.local_time_ms);
    sync_with_received_beacons(&node_sync, received_beacons, num_beacons_received);

    printf("\n==================== VOICE RESERVATION HANDSHAKE SIMULATION ====================\n");
    
    // SCENARIO: PTT Pressed -> CR Sent (Contention) -> CC Received (Grant)
    if (send_control_request(&node_sync, data_from_l3_queue)) {
        // 1. The CR packet is now in data_from_l3_queue[0]
        
        // 2. Simulate reception of CC (occurs after CR has been relayed and confirmed)
        // This grants the node exclusive access to Slot 1 (MV)
        receive_control_confirm(&node_sync, &rx_queue); 
        
        // 3. Queue 3 Analog Voice frames (since we have the reservation)
        struct frame ptt_frame = { .priority = 0, .data_type = DATA_TYPE_ANALOG_VOICE, .source_add = node_addr };
        enqueue(&analog_voice_queue, ptt_frame);
        enqueue(&analog_voice_queue, ptt_frame);
        enqueue(&analog_voice_queue, ptt_frame);
    }
    
    printf("\n==================== ACTIVE TDMA CYCLE (10 SLOTS) ====================\n");
    
    // Engaging TDMA Cycle
    if (node_sync.is_synchronized) {
        printf("[ACTIVE] Starting 10-slot Frame Cycle. Expecting TX in Slot 1 and GU slots.\n");
        
        // Start the frame cleanly at the beginning (0ms, Slot 1)
        for (int i = 0; i < TOTAL_SLOTS; i++) {
            node_sync.current_slot_index = i;
            node_sync.local_time_ms = i * SLOT_DURATION_MS; 
            tdma_scheduler_check(&node_sync, &analog_voice_queue, data_from_l3_queue, &rx_queue);
            
            // Cleanup: After the voice frames are sent, release the reservation
            if(i == 1 && is_empty(&analog_voice_queue) && node_sync.voice_status == VOICE_ACTIVE_TX) {
                end_call(&node_sync, &analog_voice_queue);
            }
        }
    }

    return 0;
}
