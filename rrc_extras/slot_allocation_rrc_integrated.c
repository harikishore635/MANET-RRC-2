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

// ==================== RRC INTEGRATION: STRUCTURES ====================
// Message from Application → RRC → TDMA
typedef enum {
    RRC_DATA_TYPE_SMS,
    RRC_DATA_TYPE_VOICE,
    RRC_DATA_TYPE_PING,
    RRC_DATA_TYPE_FILE,
    RRC_DATA_TYPE_VIDEO
} RRC_DataType;

typedef enum {
    MSG_PRIORITY_VOICE = 0,             // [FIX #3] Highest: Voice (maps to priority 0, not -1)
    MSG_PRIORITY_HIGH = 0,              // Priority 0: Digital Voice / C2
    MSG_PRIORITY_MEDIUM_HIGH = 1,       // Priority 1: High-priority Data
    MSG_PRIORITY_MEDIUM = 2,            // Priority 2: Medium Data
    MSG_PRIORITY_LOW = 3                // Priority 3: Low-priority Data
} MessagePriority;

// RRC Application Message structure (from upper layers)
typedef struct {
    uint8_t node_id;                    // Source node ID
    uint8_t dest_node_id;               // Destination node ID
    RRC_DataType data_type;             // SMS/Voice/Ping etc.
    MessagePriority priority;           // Mapped from data type
    uint8_t data[PAYLOAD_SIZE_BYTES];   // Payload
    size_t data_size;                   // Payload length in bytes
} ApplicationMessage;

// RRC-to-TDMA queued message (internal representation)
typedef struct {
    ApplicationMessage app_msg;
    uint32_t enqueue_time_ms;           // When this message entered RRC queue
} RRC_QueuedMessage;

// Simple RRC message queue (simulated)
typedef struct {
    RRC_QueuedMessage items[QUEUE_SIZE];
    int front;
    int back;
} RRC_MessageQueue;

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
    int frame_count;                /**< [FRAME-1 RULE] Frame counter: 0=Frame-1, 1+=Frame-2+ */
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


// ==================== RRC INTEGRATION: GLOBAL STATE ====================
// Simulated RRC message queue (holds messages from application layer)
RRC_MessageQueue rrc_queue = { .front = -1, .back = -1 };

// Simulated RRC→TDMA received frames (for returning data to RRC)
struct queue rrc_rx_queue = { .front = -1, .back = -1 };


// ==================== RRC INTEGRATION: HELPER FUNCTIONS ====================

/**
 * @brief Initialize RRC message queue
 */
void rrc_queue_init(RRC_MessageQueue *q) {
    q->front = -1;
    q->back = -1;
}

/**
 * @brief Check if RRC queue is empty
 */
bool rrc_queue_is_empty(RRC_MessageQueue *q) {
    return (q->front == -1 || q->front > q->back);
}

/**
 * @brief Enqueue ApplicationMessage into RRC queue
 */
void rrc_queue_enqueue(RRC_MessageQueue *q, ApplicationMessage app_msg, uint32_t current_time_ms) {
    if (q->back >= QUEUE_SIZE - 1) {
        printf("[RRC] Queue full! Dropping message.\n");
        return;
    }
    if (q->front == -1) q->front = 0;
    q->back++;
    q->items[q->back].app_msg = app_msg;
    q->items[q->back].enqueue_time_ms = current_time_ms;
}

/**
 * @brief Dequeue and retrieve next ApplicationMessage from RRC queue
 */
RRC_QueuedMessage rrc_queue_dequeue(RRC_MessageQueue *q) {
    RRC_QueuedMessage empty = { .app_msg = {0}, .enqueue_time_ms = 0 };
    if (rrc_queue_is_empty(q)) return empty;
    
    RRC_QueuedMessage dequeued = q->items[q->front];
    q->front++;
    if (q->front > q->back) {
        q->front = -1;
        q->back = -1;
    }
    return dequeued;
}


// ==================== RRC INTEGRATION: REQUIRED FUNCTIONS ====================

/**
 * @brief TDMA pulls next message from RRC, converts to TDMA frame, and enqueues into appropriate data queue
 * 
 * This is the "Pull Model" where TDMA actively requests data from RRC layer.
 * Pulled frames are enqueued into data_queues based on priority (NOT transmitted immediately).
 * Implements: Direct pull-and-enqueue model (no one-slot-ahead reservation)
 */
bool tdma_pull_from_rrc_and_enqueue(struct queue data_queues[], struct tdma_sync *sync_state) {
    // [RRC INTEGRATION] Step 1: Check if RRC has pending messages
    if (rrc_queue_is_empty(&rrc_queue)) {
        printf("[TDMA→RRC] RRC queue is empty. No frame to pull.\n");
        return false;
    }

    // [RRC INTEGRATION] Step 2: Dequeue next message from RRC
    RRC_QueuedMessage rrc_msg = rrc_queue_dequeue(&rrc_queue);
    
    printf("[TDMA→RRC] Pulled ApplicationMessage from RRC (Priority: %d, DataType: %d)\n", 
           rrc_msg.app_msg.priority, rrc_msg.app_msg.data_type);

    // [RRC INTEGRATION] Step 3: Convert ApplicationMessage → struct frame
    struct frame pulled_frame = {0};
    pulled_frame.source_add = rrc_msg.app_msg.node_id;          // RRC node_id → TDMA source_add
    pulled_frame.dest_add = rrc_msg.app_msg.dest_node_id;       // RRC dest_node_id → TDMA dest_add
    
    // Map voice data types to priority 0, never use -1
    if (rrc_msg.app_msg.data_type == RRC_DATA_TYPE_VOICE) {
        pulled_frame.priority = 0;  // Voice priority is 0 (highest)
    } else {
        pulled_frame.priority = (int)rrc_msg.app_msg.priority;  // Other priorities (0-3)
    }
    
    pulled_frame.next_hop_add = rrc_msg.app_msg.dest_node_id;   // Same as dest (no multi-hop here)
    
    // Map RRC data_type to TDMA DATATYPE
    switch (rrc_msg.app_msg.data_type) {
        case RRC_DATA_TYPE_SMS:
            pulled_frame.data_type = DATA_TYPE_SMS;
            break;
        case RRC_DATA_TYPE_VOICE:
            pulled_frame.data_type = DATA_TYPE_DIGITAL_VOICE;
            break;
        case RRC_DATA_TYPE_PING:
            pulled_frame.data_type = DATA_TYPE_SMS;  // Treat PING as SMS
            break;
        case RRC_DATA_TYPE_FILE:
            pulled_frame.data_type = DATA_TYPE_FILE_TRANSFER;
            break;
        case RRC_DATA_TYPE_VIDEO:
            pulled_frame.data_type = DATA_TYPE_VIDEO_STREAM;
            break;
        default:
            pulled_frame.data_type = DATA_TYPE_SMS;
            break;
    }
    
    // Copy payload
    memcpy(pulled_frame.payload, rrc_msg.app_msg.data, rrc_msg.app_msg.data_size);
    
    printf("[TDMA→RRC] Converted to TDMA frame (Priority: %d, Source: 0x%02X, Dest: 0x%02X)\n",
           pulled_frame.priority, pulled_frame.source_add, pulled_frame.dest_add);
    
    // [RRC INTEGRATION] Step 4: Enqueue pulled frame into appropriate data queue based on priority
    int queue_idx = pulled_frame.priority;
    if (queue_idx < 0 || queue_idx >= NUM_PRIORITY) queue_idx = NUM_PRIORITY - 1;
    
    enqueue(&data_queues[queue_idx], pulled_frame);
    printf("[TDMA→RRC] Frame enqueued into data_queues[%d] (will be transmitted according to slot availability).\n", queue_idx);
    
    return true;
}

/**
 * @brief Simulate receiving TDMA frame and delivering back to RRC
 * 
 * When TDMA receives a frame from PHY layer, it delivers it to RRC.
 * This function converts received TDMA frame → ApplicationMessage → RRC
 * Implements: Step 4 of RRC integration (RX path)
 */
void tdma_to_rrc(struct frame *received_tdma_frame) {
    if (received_tdma_frame == NULL) {
        printf("[TDMA→RRC_RX] Received NULL frame.\n");
        return;
    }

    printf("[TDMA→RRC_RX] Received frame from TDMA (Source: 0x%02X, Dest: 0x%02X, Priority: %d)\n",
           received_tdma_frame->source_add, received_tdma_frame->dest_add, received_tdma_frame->priority);

    // [RRC INTEGRATION] Step 1: Convert TDMA frame → ApplicationMessage
    ApplicationMessage app_msg = {0};
    app_msg.node_id = received_tdma_frame->source_add;
    app_msg.dest_node_id = received_tdma_frame->dest_add;
    app_msg.priority = (MessagePriority)received_tdma_frame->priority;
    
    // Map TDMA DATATYPE → RRC RRC_DataType
    switch (received_tdma_frame->data_type) {
        case DATA_TYPE_SMS:
            app_msg.data_type = RRC_DATA_TYPE_SMS;
            break;
        case DATA_TYPE_DIGITAL_VOICE:
            app_msg.data_type = RRC_DATA_TYPE_VOICE;
            break;
        case DATA_TYPE_FILE_TRANSFER:
            app_msg.data_type = RRC_DATA_TYPE_FILE;
            break;
        case DATA_TYPE_VIDEO_STREAM:
            app_msg.data_type = RRC_DATA_TYPE_VIDEO;
            break;
        default:
            app_msg.data_type = RRC_DATA_TYPE_SMS;
            break;
    }
    
    // Copy payload
    memcpy(app_msg.data, received_tdma_frame->payload, PAYLOAD_SIZE_BYTES);
    app_msg.data_size = PAYLOAD_SIZE_BYTES;

    // [RRC INTEGRATION] Step 2: Queue into RRC RX queue (for delivery to application layer)
    // For simulation, we just print that it would be delivered
    printf("[TDMA→RRC_RX] Converted to ApplicationMessage and queued for RRC delivery.\n");
    printf("[TDMA→RRC_RX] Would be delivered to Application Layer (Node: 0x%02X)\n", app_msg.node_id);
}


// ==================== ORIGINAL TDMA FUNCTIONS (PRESERVED) ====================

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


// ==================== ORIGINAL VOICE RESERVATION LOGIC (PRESERVED) ====================

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

// ==================== ORIGINAL TIME SYNCHRONIZATION LOGIC (PRESERVED) ====================

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

// ==================== ORIGINAL SLOT ALLOCATION WITH RRC INTEGRATION ====================

// -----------------------------------------------------------------------------
// Slot Allocation and Scheduling Logic WITH RRC INTEGRATION
// -----------------------------------------------------------------------------

/**
 * @brief Executes the MAC layer scheduler for one 10ms slot.
 * 
 * [RRC INTEGRATION] During each slot, TDMA:
 *  1. Pulls messages from RRC (if current slot is 1-8 and not NC)
 *  2. Enqueues them into appropriate data queue based on priority
 *  3. Transmits frames from data queue according to slot type rules
 *  4. For NC slots (9-10), does NOT schedule new frames (NC handled separately)
 */
void tdma_scheduler_check(struct tdma_sync *sync_state, 
                          struct queue *analog_voice_queue, 
                          struct queue data_queues[], 
                          struct queue *rx_queue) 
{
    int current_slot_index = sync_state->current_slot_index; 
    struct slot_definition current_slot = TDMA_FRAME_SCHEDULE[current_slot_index];
    
    printf("\n--- SCHEDULER: Slot %d (%s) [Time: %u ms] | Frame: %d | Voice Status: %d ---\n", 
           current_slot.slot_id, current_slot.description, sync_state->local_time_ms, sync_state->frame_count, sync_state->voice_status);

    if (!sync_state->is_synchronized) {
        printf("[SCHEDULER] Unsynchronized. Listening.\n");
        return;
    }

    // ==================== RRC INTEGRATION: DIRECT PULL-AND-ENQUEUE ====================
    // [RRC INTEGRATION] For slots 1-8 (TDMA responsibility):
    // - Pull from RRC and enqueue into appropriate data queue based on priority
    // - Transmit frames from data queues according to slot type rules
    // ==================== 
    
    if (current_slot.slot_id >= 1 && current_slot.slot_id <= 8) {
        // [RRC INTEGRATION] Step A: Try to pull from RRC and enqueue
        tdma_pull_from_rrc_and_enqueue(data_queues, sync_state);
    }

    switch (current_slot.type) {
        
        case SLOT_TYPE_NC: // Network Control Slot (Slots 9, 10)
            printf("[NC] **No TDMA scheduling in Slot %d**. This is RRC responsibility (beacons/sync only).\n", current_slot.slot_id);
            break;

        case SLOT_TYPE_MV: // Voice Reserved Slot (Slot 1)
            // EXCLUSIVE ACCESS CHECK: Only transmit if we have the reservation token (CC received)
            if (sync_state->voice_status == VOICE_ACTIVE_TX) {
                printf("    -> [MV] Exclusive voice access granted. Transmitting.\n");
                tx(analog_voice_queue, data_queues, rx_queue);
            } else if (!is_empty(&data_queues[0])) {
                printf("    -> [MV] Transmitting Prio 0 data (no exclusive voice reservation).\n");
                tx(analog_voice_queue, data_queues, rx_queue);
            } else {
                printf("    -> [MV] No data to transmit. Slot idle.\n");
            }
            break;

        case SLOT_TYPE_DU: // Dynamic Use Slot (Slots 2-4)
            // High-priority data contention (Prio 0/1)
            if (!is_empty(&data_queues[0]) || !is_empty(&data_queues[1])) {
                printf("    -> [DU] Transmitting Prio 0-1 data.\n");
                tx(analog_voice_queue, data_queues, rx_queue);
            } else {
                printf("    -> [DU] No Prio 0-1 data. Slot idle.\n");
            }
            break;
            
        case SLOT_TYPE_GU: // General Use Slot (Slots 5-8)
            // Low-priority data contention (Prio 2/3 and RX Relay)
            if (!is_empty(&data_queues[2]) || !is_empty(&data_queues[3]) || !is_empty(rx_queue)) {
                printf("    -> [GU] Transmitting Prio 2-3 or relay data.\n");
                tx(analog_voice_queue, data_queues, rx_queue);
            } else {
                printf("    -> [GU] No Prio 2-3 or relay data. Slot idle.\n");
            }
            break;

        default:
            printf("[MAC] Unknown slot type. Listening.\n");
            break;
    }
}

// ==================== MAIN SIMULATION ====================

// -----------------------------------------------------------------------------
// Main Loop: Simulation of Acquisition, Handshake, and TDMA Cycle with RRC
// -----------------------------------------------------------------------------
int main(){
    srand(time(NULL)); 
    
    // --- Node Initialization ---
    struct tdma_sync node_sync = {
        .is_synchronized = false,
        .status = STATUS_UNSYNCHRONIZED,
        .local_time_ms = (rand() % TOTAL_SLOTS) * SLOT_DURATION_MS,
        .master_mac = 0x00,
        .voice_status = VOICE_INACTIVE,
        .frame_count = 0  // [FRAME-1 RULE] Start at Frame-1
    };
    node_sync.current_slot_index = node_sync.local_time_ms / SLOT_DURATION_MS;

    // --- Queue Initialization ---
    struct queue analog_voice_queue = { .front = -1, .back = -1 };
    struct queue data_from_l3_queue[NUM_PRIORITY];
    struct queue rx_queue = { .front = -1, .back = -1 };
    for(int i=0;i<NUM_PRIORITY;i++) { data_from_l3_queue[i].front = -1; data_from_l3_queue[i].back = -1; }

    // ==================== RRC INTEGRATION: Initialize RRC Queue ====================
    rrc_queue_init(&rrc_queue);
    printf("[INIT] RRC message queue initialized.\n");

    // ==================== RRC INTEGRATION: Simulate application messages arriving at RRC ====================
    printf("\n==================== RRC MESSAGE INJECTION ====================\n");
    
    // Inject application messages into RRC queue (simulating messages from Application Layer)
    ApplicationMessage app_msg_1 = {
        .node_id = 0x01,
        .dest_node_id = 0x02,
        .data_type = RRC_DATA_TYPE_SMS,
        .priority = MSG_PRIORITY_MEDIUM,
        .data_size = 10
    };
    strcpy((char*)app_msg_1.data, "Hello SMS");
    rrc_queue_enqueue(&rrc_queue, app_msg_1, 0);
    printf("[APP→RRC] SMS message injected into RRC queue (Priority: %d)\n", app_msg_1.priority);

    ApplicationMessage app_msg_2 = {
        .node_id = 0x01,
        .dest_node_id = 0x03,
        .data_type = RRC_DATA_TYPE_VOICE,
        .priority = MSG_PRIORITY_HIGH,
        .data_size = 8
    };
    strcpy((char*)app_msg_2.data, "Voice");
    rrc_queue_enqueue(&rrc_queue, app_msg_2, 10);
    printf("[APP→RRC] Voice message injected into RRC queue (Priority: %d)\n", app_msg_2.priority);

    // --- Injecting data for non-voice queues (Prio 3 for GU slot test) ---
    struct frame p3_sms_frame = { .priority = 3, .data_type = DATA_TYPE_SMS };
    enqueue(&data_from_l3_queue[3], p3_sms_frame);   // Prio 3 (SMS) data ready
    enqueue(&rx_queue, p3_sms_frame);                // Relay data ready

    printf("\n--- TDMA Network ACQUISITION/FORMATION Simulation ---\n");
    
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
    
    printf("\n==================== ACTIVE TDMA CYCLE (20 SLOTS = 2 FRAMES) WITH RRC INTEGRATION ====================\n");
    
    // Engaging TDMA Cycle
    if (node_sync.is_synchronized) {
        printf("[ACTIVE] Starting TDMA simulation with RRC integration (2 frames = 20 slots).\n");
        printf("[ACTIVE] TDMA directly pulls from RRC and enqueues frames into priority queues.\n");
        printf("[ACTIVE] Frames transmitted according to slot type and priority rules.\\n\n");
        
        // Simulate 2 frames (20 slots total)
        for (int i = 0; i < 2 * TOTAL_SLOTS; i++) {
            int slot_in_frame = i % TOTAL_SLOTS;
            node_sync.current_slot_index = slot_in_frame;
            node_sync.local_time_ms = i * SLOT_DURATION_MS; 
            
            // [RRC INTEGRATION] Call scheduler which now handles direct RRC pull-and-enqueue, then transmit
            tdma_scheduler_check(&node_sync, &analog_voice_queue, data_from_l3_queue, &rx_queue);
        }
    }

    

    return 0;
}
