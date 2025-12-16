#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include<time.h>
#include "timesync.h"
#include "rrc_shared_memory.h"

#define QUEUE_SIZE 10
#define PAYLOAD_SIZE_BYTES 16
#define NUM_PRIORITY 4
#define TOTAL_SLOTS 10
#define SLOT_DURATION_MS 10
#define FRAME_DURATION_MS (TOTAL_SLOTS * SLOT_DURATION_MS)

uint8_t node_addr = 0xFE;

// RRC function stubs
bool rrc_has_nc_packet_for_slot(int slot);
struct frame rrc_tdma_dequeue_nc_packet(int slot);
bool rrc_has_relay_packets();
struct frame rrc_tdma_dequeue_relay_packet();
bool rrc_has_data_for_priority(int priority);
void rrc_get_data_for_priority(int priority, struct frame *out);
int rrc_get_my_nc_slot();
bool rrc_is_neighbor_tx(int node_id, int slot);
bool rrc_is_neighbor_rx(int node_id, int slot);

typedef enum { 
    SLOT_TYPE_MV, 
    SLOT_TYPE_DU, 
    SLOT_TYPE_GU, 
    SLOT_TYPE_NC 
} SLOT_TYPE;

typedef enum { 
    VOICE_INACTIVE, 
    VOICE_CR_SENT, 
    VOICE_ACTIVE_TX 
} VOICE_STATUS;

struct slot_definition {
    int slot_id;
    SLOT_TYPE type;
    const char* description;
};

struct slot_definition TDMA_FRAME_SCHEDULE[TOTAL_SLOTS] = {
    {1, SLOT_TYPE_MV, "Voice Reserved"},
    {2, SLOT_TYPE_DU, "Dynamic Use"},
    {3, SLOT_TYPE_DU, "Dynamic Use"},
    {4, SLOT_TYPE_DU, "Dynamic Use"},
    {5, SLOT_TYPE_GU, "General Use"},
    {6, SLOT_TYPE_GU, "General Use"},
    {7, SLOT_TYPE_GU, "General Use"},
    {8, SLOT_TYPE_GU, "General Use"},
    {9, SLOT_TYPE_NC, "Network Control"},
    {10, SLOT_TYPE_NC, "Network Control"}
};

struct tdma_state { 
    VOICE_STATUS voice_status; 
    int frame_count; 
};

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
    uint8_t next_hop_add; 
    int priority; 
    DATATYPE data_type; 
    char payload[PAYLOAD_SIZE_BYTES]; 
};

struct queue{ 
    struct frame item[QUEUE_SIZE]; 
    int front; 
    int back; 
};

static struct tdma_state tdma_state = { 
    .voice_status = VOICE_INACTIVE, 
    .frame_count = 0 
};

static struct queue analog_voice_queue = { 
    .front = -1, 
    .back = -1 
};

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
    if (q->front > q->back) { 
        q->front = -1; 
        q->back = -1; 
    }
    return dequeued_frame;
}

void phy_transmit_frame(struct frame *f) {
    printf("-> [PHY_TX] Frame (P:%d T:%d S:0x%02X D:0x%02X)\n", 
           f->priority, f->data_type, f->source_add, f->dest_add);
}

bool send_control_request(void) {
    if (tdma_state.voice_status != VOICE_INACTIVE) {
        printf("[PTT] Voice active. Ignoring PTT.\n");
        return false;
    }
    
    if (rand() % 100 < 80) {
        tdma_state.voice_status = VOICE_CR_SENT;
        printf("[PTT] CR sent. Status: CR_SENT.\n");
        return true;
    } else {
        printf("[PTT] CR failed. Retry PTT.\n");
        return false;
    }
}

void receive_control_confirm(void) {
    if (tdma_state.voice_status == VOICE_CR_SENT) {
        tdma_state.voice_status = VOICE_ACTIVE_TX;
        printf("[CC] Slot 1 access granted.\n");
    }
}

void end_call(void) {
    tdma_state.voice_status = VOICE_INACTIVE;
    while(!is_empty(&analog_voice_queue)) 
        dequeue(&analog_voice_queue);
    printf("[END] Call ended.\n");
}

void tdma_scheduler_process(void) {
    NetworkTimeSync* sync_info = get_time_sync_instance();
    
    if (!is_synchronized()) {
        printf("[SCHED] Unsynchronized.\n");
        return;
    }
    
    if (rrc_shm == NULL || !rrc_shm->rrc_initialized) {
        printf("[SCHED] RRC not ready.\n");
        return;
    }
    
    int current_slot_id = (sync_info->current_slot % TOTAL_SLOTS) + 1;
    struct slot_definition current_slot = TDMA_FRAME_SCHEDULE[current_slot_id - 1];
    
    printf("\n--- SLOT %d (%s) F:%d V:%d ---\n", 
           current_slot.slot_id, current_slot.description, 
           tdma_state.frame_count, tdma_state.voice_status);

    // Frame-1 rule
    if (tdma_state.frame_count == 0 && current_slot.slot_id >= 1 && current_slot.slot_id <= 8) {
        printf("[F1] No TX allowed.\n");
        return;
    }
    
    if (current_slot_id == 10 && tdma_state.frame_count == 0) {
        tdma_state.frame_count = 1;
        printf("[F1] Complete.\n");
    }

    switch (current_slot.type) {
        case SLOT_TYPE_MV: {
            if (tdma_state.voice_status == VOICE_ACTIVE_TX) {
                if (!is_empty(&analog_voice_queue)) {
                    struct frame voice_frame = dequeue(&analog_voice_queue);
                    printf("-> [MV] Voice TX\n");
                    phy_transmit_frame(&voice_frame);
                    return;
                }
            }
            
            if (rrc_shm_has_data_for_priority(0)) {
                struct frame prio0_frame;
                if (rrc_shm_get_data_for_priority(0, &prio0_frame)) {
                    printf("-> [MV] P0 TX\n");
                    phy_transmit_frame(&prio0_frame);
                    return;
                }
            }
            
            printf("-> [MV] Idle\n");
            break;
        }
        
        case SLOT_TYPE_DU: {
            if (rrc_shm_has_data_for_priority(0)) {
                struct frame prio0_frame;
                if (rrc_shm_get_data_for_priority(0, &prio0_frame)) {
                    printf("-> [DU] P0 TX\n");
                    phy_transmit_frame(&prio0_frame);
                    return;
                }
            }
            
            if (rrc_shm_has_data_for_priority(1)) {
                struct frame prio1_frame;
                if (rrc_shm_get_data_for_priority(1, &prio1_frame)) {
                    printf("-> [DU] P1 TX\n");
                    phy_transmit_frame(&prio1_frame);
                    return;
                }
            }
            
            printf("-> [DU] Idle\n");
            break;
        }
        
        case SLOT_TYPE_GU: {
            if (rrc_shm_has_relay_packets()) {
                struct frame relay_frame;
                if (rrc_shm_dequeue_relay_packet(&relay_frame)) {
                    printf("-> [GU] Relay TX\n");
                    phy_transmit_frame(&relay_frame);
                    return;
                }
            }
            
            if (rrc_shm_has_data_for_priority(2)) {
                struct frame prio2_frame;
                if (rrc_shm_get_data_for_priority(2, &prio2_frame)) {
                    printf("-> [GU] P2 TX\n");
                    phy_transmit_frame(&prio2_frame);
                    return;
                }
            }
            
            if (rrc_shm_has_data_for_priority(3)) {
                struct frame prio3_frame;
                if (rrc_shm_get_data_for_priority(3, &prio3_frame)) {
                    printf("-> [GU] P3 TX\n");
                    phy_transmit_frame(&prio3_frame);
                    return;
                }
            }
            
            printf("-> [GU] Idle\n");
            break;
        }
        
        case SLOT_TYPE_NC: {
            int my_nc_slot = rrc_shm_get_my_nc_slot();
            if (current_slot.slot_id == my_nc_slot) {
                if (rrc_shm_has_nc_packet_for_slot(current_slot.slot_id)) {
                    struct frame nc_frame;
                    if (rrc_shm_dequeue_nc_packet(current_slot.slot_id, &nc_frame)) {
                        printf("-> [NC] TX slot %d\n", my_nc_slot);
                        phy_transmit_frame(&nc_frame);
                        return;
                    }
                }
                printf("-> [NC] No packet\n");
            } else {
                printf("-> [NC] Listen\n");
            }
            break;
        }
        
        default:
            printf("[SCHED] Unknown slot\n");
            break;
    }
}

void tdma_handle_received_frame(struct frame *received_frame, int rssi, int snr) {
    if (received_frame == NULL) {
        printf("[RX] NULL frame\n");
        return;
    }
    
    printf("[RX] S:0x%02X D:0x%02X T:%d R:%d S:%d\n", 
           received_frame->source_add, received_frame->dest_add, 
           received_frame->data_type, rssi, snr);
    
    if (received_frame->data_type == DATA_TYPE_CR || received_frame->data_type == DATA_TYPE_CC) {
        printf("[RX] Voice control\n");
    }
    
    printf("[RX] Forward to RRC\n");
}

void tdma_init(void) {
    if (!rrc_shared_memory_init()) {
        printf("[TDMA] RRC init failed\n");
        return;
    }
    printf("[TDMA] Init OK\n");
}

int main(){
    srand(time(NULL));
    printf("\n--- TDMA-TimeSync-RRC Test ---\n");
    
    network_time_sync_init();
    tdma_init();
    printf("[MAIN] Init complete\n");
    
    if (rrc_shm != NULL) {
        struct frame test_frame = { 
            .source_add = node_addr, 
            .dest_add = 0xFF, 
            .priority = 1, 
            .data_type = DATA_TYPE_SMS 
        };
        strcpy(test_frame.payload, "Test data");
        
        pthread_mutex_lock(&rrc_shm->priority_queues[1].mutex);
        if (!RRC_QUEUE_IS_FULL(&rrc_shm->priority_queues[1])) {
            rrc_shm->priority_queues[1].frames[rrc_shm->priority_queues[1].tail] = test_frame;
            rrc_shm->priority_queues[1].tail = (rrc_shm->priority_queues[1].tail + 1) % RRC_QUEUE_SIZE;
            rrc_shm->priority_queues[1].count++;
        }
        pthread_mutex_unlock(&rrc_shm->priority_queues[1].mutex);
        
        printf("[TEST] Added frame\n");
    }
    
    printf("\n--- Test 10 slots ---\n");
    for (int i = 0; i < 10; i++) {
        printf("\n[SIM] Slot %d\n", i);
        on_slot_timer_interrupt();
    }
    
    rrc_shared_memory_cleanup();
    return 0;
}
