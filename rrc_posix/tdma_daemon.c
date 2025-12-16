/**
 * TDMA Daemon Simulator for RRC POSIX Integration
 * Simulates TDMA slot management responses
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "../rrc_posix/rrc_posix_mq_defs.h"
#include "../rrc_posix/rrc_mq_adapters.h"

static bool g_running = true;
static uint8_t g_node_id = 1;

// Simple slot table: neighbor_id -> available slots bitmap
typedef struct {
    uint8_t neighbor_id;
    uint64_t slot_bitmap;  // 64 slots
    bool valid;
} SlotTableEntry;

#define MAX_NEIGHBORS 10
static SlotTableEntry g_slot_table[MAX_NEIGHBORS];

void signal_handler(int signum) {
    printf("[TDMA] Received signal %d, shutting down...\n", signum);
    g_running = false;
}

void init_slot_table(void) {
    memset(g_slot_table, 0, sizeof(g_slot_table));
    
    // Static slot assignments for demo
    // Node 1 -> Node 2: slots 0-15 available
    // Node 2 -> Node 3: slots 16-31 available
    if (g_node_id == 1) {
        g_slot_table[0].neighbor_id = 2;
        g_slot_table[0].slot_bitmap = 0x000000000000FFFFULL;  // Slots 0-15
        g_slot_table[0].valid = true;
    } else if (g_node_id == 2) {
        g_slot_table[0].neighbor_id = 1;
        g_slot_table[0].slot_bitmap = 0x000000000000FFFFULL;  // Slots 0-15
        g_slot_table[0].valid = true;
        
        g_slot_table[1].neighbor_id = 3;
        g_slot_table[1].slot_bitmap = 0x00000000FFFF0000ULL;  // Slots 16-31
        g_slot_table[1].valid = true;
    } else if (g_node_id == 3) {
        g_slot_table[0].neighbor_id = 2;
        g_slot_table[0].slot_bitmap = 0x00000000FFFF0000ULL;  // Slots 16-31
        g_slot_table[0].valid = true;
    }
    
    printf("[TDMA] Slot table initialized\n");
}

SlotTableEntry* lookup_slots(uint8_t neighbor_id) {
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (g_slot_table[i].valid && g_slot_table[i].neighbor_id == neighbor_id) {
            return &g_slot_table[i];
        }
    }
    return NULL;
}

int find_available_slot(uint64_t bitmap) {
    for (int i = 0; i < 64; i++) {
        if (bitmap & (1ULL << i)) {
            return i;
        }
    }
    return -1;
}

void handle_slot_check(MQContext* mq_in, MQContext* mq_out) {
    RrcToTdmaMsg req;
    unsigned int priority;
    
    ssize_t bytes = mq_try_recv_msg(mq_in, &req, sizeof(req), &priority);
    if (bytes <= 0) return;
    
    printf("[TDMA] Slot check: next_hop=%d, priority=%d, req_id=%u\n",
           req.next_hop, req.priority, req.header.request_id);
    
    // Lookup slot table
    SlotTableEntry* entry = lookup_slots(req.next_hop);
    
    // Send response
    TdmaToRrcMsg rsp;
    init_message_header(&rsp.header, MSG_TDMA_TO_RRC_SLOT_RSP);
    rsp.header.request_id = req.header.request_id;  // Correlation
    
    if (entry && entry->slot_bitmap != 0) {
        int slot = find_available_slot(entry->slot_bitmap);
        if (slot >= 0) {
            rsp.success = 1;
            rsp.assigned_slot = slot;
            rsp.slot_bitmap_low = (uint16_t)(entry->slot_bitmap & 0xFFFF);
            rsp.slot_bitmap_high = (uint16_t)((entry->slot_bitmap >> 16) & 0xFFFF);
            printf("[TDMA] Slot available: slot=%d\n", slot);
        } else {
            rsp.success = 0;
            rsp.assigned_slot = 0;
            rsp.slot_bitmap_low = 0;
            rsp.slot_bitmap_high = 0;
            printf("[TDMA] No slot available for next_hop=%d\n", req.next_hop);
        }
    } else {
        rsp.success = 0;
        rsp.assigned_slot = 0;
        rsp.slot_bitmap_low = 0;
        rsp.slot_bitmap_high = 0;
        printf("[TDMA] No slot table entry for next_hop=%d\n", req.next_hop);
    }
    
    if (mq_send_msg(mq_out, &rsp, sizeof(rsp), priority) < 0) {
        fprintf(stderr, "[TDMA] Failed to send slot response\n");
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        g_node_id = atoi(argv[1]);
    }
    
    printf("========================================\n");
    printf("TDMA Daemon Simulator\n");
    printf("Node ID: %d\n", g_node_id);
    printf("========================================\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize slot table
    init_slot_table();
    
    // Open message queues
    MQContext mq_rrc_to_tdma, mq_tdma_to_rrc;
    
    if (mq_init(&mq_rrc_to_tdma, MQ_RRC_TO_TDMA, O_RDONLY, false) < 0) {
        fprintf(stderr, "[TDMA] Failed to open RRC->TDMA queue\n");
        return 1;
    }
    
    if (mq_init(&mq_tdma_to_rrc, MQ_TDMA_TO_RRC, O_WRONLY, false) < 0) {
        fprintf(stderr, "[TDMA] Failed to open TDMA->RRC queue\n");
        mq_cleanup(&mq_rrc_to_tdma, false);
        return 1;
    }
    
    printf("[TDMA] Daemon is running. Press Ctrl+C to exit.\n\n");
    
    // Main processing loop
    while (g_running) {
        handle_slot_check(&mq_rrc_to_tdma, &mq_tdma_to_rrc);
        usleep(10000);  // 10ms
    }
    
    // Cleanup
    mq_cleanup(&mq_rrc_to_tdma, false);
    mq_cleanup(&mq_tdma_to_rrc, false);
    
    printf("\n[TDMA] Daemon shutdown complete\n");
    return 0;
}
