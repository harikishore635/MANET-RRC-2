/**
 * Application Layer Simulator for RRC POSIX Integration
 * Simulates application sending data and receiving frames
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

static PoolContext app_pool;
static PoolContext frame_pool;
static MQContext mq_app_to_rrc;
static MQContext mq_rrc_to_app;

void signal_handler(int signum) {
    printf("[APP] Received signal %d, shutting down...\n", signum);
    g_running = false;
}

void send_test_packet(uint8_t dest_id, const char* payload, DataType dtype) {
    printf("[APP] Sending packet: dest=%d, dtype=%d\n", dest_id, dtype);
    
    // Allocate app pool entry
    int pool_idx = app_pool_alloc(&app_pool);
    if (pool_idx < 0) {
        fprintf(stderr, "[APP] App pool full\n");
        return;
    }
    
    // Build application packet
    AppPacketPoolEntry pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = g_node_id;
    pkt.dest_id = dest_id;
    pkt.data_type = dtype;
    pkt.transmission_type = 0;  // Unicast
    pkt.priority = 5;
    pkt.payload_len = strlen(payload) + 1;
    pkt.sequence_number = rand() % 10000;
    pkt.timestamp_ms = get_timestamp_ms();
    memcpy(pkt.payload, payload, pkt.payload_len);
    pkt.in_use = true;
    pkt.urgent = false;
    
    app_pool_set(&app_pool, pool_idx, &pkt);
    
    // Send notification to RRC
    AppToRrcMsg msg;
    init_message_header(&msg.header, MSG_APP_TO_RRC_DATA);
    msg.pool_index = pool_idx;
    msg.data_type = dtype;
    msg.priority = pkt.priority;
    
    if (mq_send_msg(&mq_app_to_rrc, &msg, sizeof(msg), pkt.priority) < 0) {
        fprintf(stderr, "[APP] Failed to send packet notification to RRC\n");
        app_pool_release(&app_pool, pool_idx);
        return;
    }
    
    printf("[APP] Packet sent to RRC at pool_index=%d\n", pool_idx);
}

void check_received_frames(void) {
    RrcToAppMsg msg;
    unsigned int priority;
    
    ssize_t bytes = mq_try_recv_msg(&mq_rrc_to_app, &msg, sizeof(msg), &priority);
    
    if (bytes <= 0) return;
    
    if (msg.is_error) {
        // Error message
        printf("[APP] *** ERROR from RRC: code=%d, text='%s' ***\n",
               msg.error_code, msg.error_text);
        return;
    }
    
    // Frame delivery
    printf("[APP] Received frame notification: pool_index=%d\n", msg.pool_index);
    
    // Get frame from shared memory
    FramePoolEntry* frame = frame_pool_get(&frame_pool, msg.pool_index);
    if (!frame || !frame->in_use || !frame->valid) {
        fprintf(stderr, "[APP] Invalid frame at pool_index=%d\n", msg.pool_index);
        return;
    }
    
    printf("[APP] Frame details: src=%d, dest=%d, dtype=%d, payload_len=%d\n",
           frame->src_id, frame->dest_id, frame->data_type, frame->payload_len);
    printf("[APP] Payload: '%s'\n", (char*)frame->payload);
    
    // Release frame from pool
    frame_pool_release(&frame_pool, msg.pool_index);
    
    printf("[APP] Frame processed and released\n\n");
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        g_node_id = atoi(argv[1]);
    }
    
    printf("========================================\n");
    printf("Application Layer Simulator\n");
    printf("Node ID: %d\n", g_node_id);
    printf("========================================\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    srand(time(NULL));
    
    // Attach to shared memory pools
    if (pool_init(&app_pool, SHM_APP_POOL, sizeof(AppPacketPoolEntry), 
                  APP_POOL_SIZE, false) < 0) {
        fprintf(stderr, "[APP] Failed to attach to app pool\n");
        return 1;
    }
    
    if (pool_init(&frame_pool, SHM_FRAME_POOL, sizeof(FramePoolEntry), 
                  FRAME_POOL_SIZE, false) < 0) {
        fprintf(stderr, "[APP] Failed to attach to frame pool\n");
        pool_cleanup(&app_pool, SHM_APP_POOL, false);
        return 1;
    }
    
    // Open message queues
    if (mq_init(&mq_app_to_rrc, MQ_APP_TO_RRC, O_WRONLY, false) < 0) {
        fprintf(stderr, "[APP] Failed to open APP->RRC queue\n");
        pool_cleanup(&app_pool, SHM_APP_POOL, false);
        pool_cleanup(&frame_pool, SHM_FRAME_POOL, false);
        return 1;
    }
    
    if (mq_init(&mq_rrc_to_app, MQ_RRC_TO_APP, O_RDONLY, false) < 0) {
        fprintf(stderr, "[APP] Failed to open RRC->APP queue\n");
        mq_cleanup(&mq_app_to_rrc, false);
        pool_cleanup(&app_pool, SHM_APP_POOL, false);
        pool_cleanup(&frame_pool, SHM_FRAME_POOL, false);
        return 1;
    }
    
    printf("[APP] Simulator is running. Press Ctrl+C to exit.\n\n");
    
    // Send initial test packets after 2 seconds
    sleep(2);
    
    if (g_node_id == 1) {
        printf("[APP] Sending test packet to Node 3...\n");
        send_test_packet(3, "Hello from Node 1 to Node 3!", DATA_TYPE_MSG);
        sleep(1);
        send_test_packet(2, "Second message from Node 1 to Node 2", DATA_TYPE_MSG);
    } else if (g_node_id == 2) {
        printf("[APP] Sending test packet to Node 1...\n");
        send_test_packet(1, "Hello from Node 2 to Node 1!", DATA_TYPE_MSG);
        sleep(1);
        send_test_packet(3, "Message from Node 2 to Node 3", DATA_TYPE_MSG);
    } else if (g_node_id == 3) {
        printf("[APP] Sending test packet to Node 1...\n");
        send_test_packet(1, "Hello from Node 3 to Node 1!", DATA_TYPE_MSG);
        sleep(1);
        send_test_packet(2, "Message from Node 3 to Node 2", DATA_TYPE_VOICE);
    }
    
    // Main loop - check for received frames
    while (g_running) {
        check_received_frames();
        usleep(100000);  // 100ms
    }
    
    // Cleanup
    mq_cleanup(&mq_app_to_rrc, false);
    mq_cleanup(&mq_rrc_to_app, false);
    pool_cleanup(&app_pool, SHM_APP_POOL, false);
    pool_cleanup(&frame_pool, SHM_FRAME_POOL, false);
    
    printf("\n[APP] Simulator shutdown complete\n");
    return 0;
}
