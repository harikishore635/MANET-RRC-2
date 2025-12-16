/**
 * MAC/PHY Simulator for RRC POSIX Integration
 * Simulates frame reception from PHY layer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "../rrc_posix/rrc_posix_mq_defs.h"
#include "../rrc_posix/rrc_shm_pool.h"
#include "../rrc_posix/rrc_mq_adapters.h"

static bool g_running = true;
static uint8_t g_node_id = 1;

static PoolContext mac_rx_pool;
static MQContext mq_mac_to_rrc;

void signal_handler(int signum) {
    printf("[MAC] Received signal %d, shutting down...\n", signum);
    g_running = false;
}

void inject_test_frame(uint8_t src_id, uint8_t dest_id, const char* payload) {
    printf("[MAC] Injecting test frame: src=%d, dest=%d\n", src_id, dest_id);
    
    // Allocate frame in MAC RX pool
    int pool_idx = frame_pool_alloc(&mac_rx_pool);
    if (pool_idx < 0) {
        fprintf(stderr, "[MAC] RX pool full\n");
        return;
    }
    
    // Build frame
    FramePoolEntry frame;
    memset(&frame, 0, sizeof(frame));
    frame.src_id = src_id;
    frame.dest_id = dest_id;
    frame.next_hop = dest_id;
    frame.ttl = 15;
    frame.data_type = DATA_TYPE_MSG;
    frame.priority = 5;
    frame.payload_len = strlen(payload) + 1;
    frame.sequence_number = rand() % 10000;
    frame.timestamp_ms = get_timestamp_ms();
    memcpy(frame.payload, payload, frame.payload_len);
    frame.in_use = true;
    frame.valid = true;
    
    frame_pool_set(&mac_rx_pool, pool_idx, &frame);
    
    // Send notification to RRC
    MacToRrcMsg msg;
    init_message_header(&msg.header, MSG_MAC_TO_RRC_RX_FRAME);
    msg.pool_index = pool_idx;
    msg.rssi_dbm = -60.0 + (rand() % 20);  // -60 to -40 dBm
    msg.snr_db = 15.0 + (rand() % 10);     // 15 to 25 dB
    
    if (mq_send_msg(&mq_mac_to_rrc, &msg, sizeof(msg), 5) < 0) {
        fprintf(stderr, "[MAC] Failed to send frame notification to RRC\n");
        frame_pool_release(&mac_rx_pool, pool_idx);
        return;
    }
    
    printf("[MAC] Frame injected at pool_index=%d, RSSI=%.1f dBm\n", 
           pool_idx, msg.rssi_dbm);
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        g_node_id = atoi(argv[1]);
    }
    
    printf("========================================\n");
    printf("MAC/PHY Simulator\n");
    printf("Node ID: %d\n", g_node_id);
    printf("========================================\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    srand(time(NULL));
    
    // Attach to MAC RX shared memory pool
    if (pool_init(&mac_rx_pool, SHM_MAC_RX_POOL, sizeof(FramePoolEntry), 
                  FRAME_POOL_SIZE, false) < 0) {
        fprintf(stderr, "[MAC] Failed to attach to MAC RX pool\n");
        return 1;
    }
    
    // Open message queue
    if (mq_init(&mq_mac_to_rrc, MQ_MAC_TO_RRC, O_WRONLY, false) < 0) {
        fprintf(stderr, "[MAC] Failed to open MAC->RRC queue\n");
        pool_cleanup(&mac_rx_pool, SHM_MAC_RX_POOL, false);
        return 1;
    }
    
    printf("[MAC] Simulator is running. Press Ctrl+C to exit.\n\n");
    printf("[MAC] Will inject test frames every 5 seconds...\n\n");
    
    int frame_count = 0;
    
    // Main loop - inject test frames periodically
    while (g_running) {
        sleep(5);  // Wait 5 seconds
        
        if (!g_running) break;
        
        // Inject test frame from different source nodes
        if (g_node_id == 1) {
            // Node 1 receives from Node 2
            char payload[256];
            snprintf(payload, sizeof(payload), 
                    "Test frame %d from Node 2 to Node 1", frame_count++);
            inject_test_frame(2, 1, payload);
        } else if (g_node_id == 2) {
            // Node 2 receives from Node 1 or Node 3
            char payload[256];
            if (frame_count % 2 == 0) {
                snprintf(payload, sizeof(payload), 
                        "Test frame %d from Node 1 to Node 2", frame_count++);
                inject_test_frame(1, 2, payload);
            } else {
                snprintf(payload, sizeof(payload), 
                        "Test frame %d from Node 3 to Node 2", frame_count++);
                inject_test_frame(3, 2, payload);
            }
        } else if (g_node_id == 3) {
            // Node 3 receives from Node 2
            char payload[256];
            snprintf(payload, sizeof(payload), 
                    "Test frame %d from Node 2 to Node 3", frame_count++);
            inject_test_frame(2, 3, payload);
        }
    }
    
    // Cleanup
    mq_cleanup(&mq_mac_to_rrc, false);
    pool_cleanup(&mac_rx_pool, SHM_MAC_RX_POOL, false);
    
    printf("\n[MAC] Simulator shutdown complete\n");
    return 0;
}
