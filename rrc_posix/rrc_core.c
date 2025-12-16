/**
 * RRC POSIX Integration - Core Implementation
 * Demonstrates complete message flows and layer integration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "rrc_posix_mq_defs.h"
#include "rrc_shm_pool.h"
#include "rrc_mq_adapters.h"

// ============================================================================
// GLOBAL STATE
// ============================================================================

static bool g_running = true;
static uint8_t g_node_id = 1;  // This node's ID

// Message queue contexts
static MQContext mq_app_to_rrc;
static MQContext mq_rrc_to_app;
static MQContext mq_rrc_to_olsr;
static MQContext mq_olsr_to_rrc;
static MQContext mq_rrc_to_tdma;
static MQContext mq_tdma_to_rrc;
static MQContext mq_mac_to_rrc;

// Data type queues (7 queues for different priorities)
static MQContext mq_datatype_queues[7];

// Shared memory pool contexts
static PoolContext frame_pool;
static PoolContext app_pool;
static PoolContext mac_rx_pool;

// ============================================================================
// SIGNAL HANDLER
// ============================================================================

void signal_handler(int signum) {
    printf("\n[RRC] Received signal %d, shutting down...\n", signum);
    g_running = false;
}

// ============================================================================
// INITIALIZATION AND CLEANUP
// ============================================================================

int init_rrc_core(void) {
    printf("[RRC] Initializing RRC Core...\n");
    
    // Initialize shared memory pools
    if (pool_init(&frame_pool, SHM_FRAME_POOL, sizeof(FramePoolEntry), 
                  FRAME_POOL_SIZE, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init frame pool\n");
        return -1;
    }
    
    if (pool_init(&app_pool, SHM_APP_POOL, sizeof(AppPacketPoolEntry), 
                  APP_POOL_SIZE, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init app pool\n");
        return -1;
    }
    
    if (pool_init(&mac_rx_pool, SHM_MAC_RX_POOL, sizeof(FramePoolEntry), 
                  FRAME_POOL_SIZE, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init MAC RX pool\n");
        return -1;
    }
    
    // Initialize message queues
    if (mq_init(&mq_app_to_rrc, MQ_APP_TO_RRC, O_RDONLY, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init APP->RRC queue\n");
        return -1;
    }
    
    if (mq_init(&mq_rrc_to_app, MQ_RRC_TO_APP, O_WRONLY, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init RRC->APP queue\n");
        return -1;
    }
    
    if (mq_init(&mq_rrc_to_olsr, MQ_RRC_TO_OLSR, O_WRONLY, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init RRC->OLSR queue\n");
        return -1;
    }
    
    if (mq_init(&mq_olsr_to_rrc, MQ_OLSR_TO_RRC, O_RDONLY, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init OLSR->RRC queue\n");
        return -1;
    }
    
    if (mq_init(&mq_rrc_to_tdma, MQ_RRC_TO_TDMA, O_WRONLY, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init RRC->TDMA queue\n");
        return -1;
    }
    
    if (mq_init(&mq_tdma_to_rrc, MQ_TDMA_TO_RRC, O_RDONLY, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init TDMA->RRC queue\n");
        return -1;
    }
    
    if (mq_init(&mq_mac_to_rrc, MQ_MAC_TO_RRC, O_RDONLY, true) < 0) {
        fprintf(stderr, "[RRC] Failed to init MAC->RRC queue\n");
        return -1;
    }
    
    // Initialize data type queues
    const char* dt_queues[7] = {
        MQ_RRC_MSG_QUEUE, MQ_RRC_VOICE_QUEUE, MQ_RRC_VIDEO_QUEUE,
        MQ_RRC_FILE_QUEUE, MQ_RRC_RELAY_QUEUE, MQ_RRC_PTT_QUEUE,
        MQ_RRC_UNKNOWN_QUEUE
    };
    
    for (int i = 0; i < 7; i++) {
        if (mq_init(&mq_datatype_queues[i], dt_queues[i], O_WRONLY, true) < 0) {
            fprintf(stderr, "[RRC] Failed to init datatype queue %d\n", i);
            return -1;
        }
    }
    
    printf("[RRC] Initialization complete\n");
    return 0;
}

void cleanup_rrc_core(void) {
    printf("[RRC] Cleaning up...\n");
    
    // Cleanup message queues
    mq_cleanup(&mq_app_to_rrc, true);
    mq_cleanup(&mq_rrc_to_app, true);
    mq_cleanup(&mq_rrc_to_olsr, true);
    mq_cleanup(&mq_olsr_to_rrc, true);
    mq_cleanup(&mq_rrc_to_tdma, true);
    mq_cleanup(&mq_tdma_to_rrc, true);
    mq_cleanup(&mq_mac_to_rrc, true);
    
    for (int i = 0; i < 7; i++) {
        mq_cleanup(&mq_datatype_queues[i], true);
    }
    
    // Cleanup shared memory pools
    pool_cleanup(&frame_pool, SHM_FRAME_POOL, true);
    pool_cleanup(&app_pool, SHM_APP_POOL, true);
    pool_cleanup(&mac_rx_pool, SHM_MAC_RX_POOL, true);
    
    printf("[RRC] Cleanup complete\n");
}

// ============================================================================
// APP -> RRC MESSAGE HANDLER
// ============================================================================

void handle_app_to_rrc_message(void) {
    AppToRrcMsg msg;
    unsigned int priority;
    
    ssize_t bytes = mq_try_recv_msg(&mq_app_to_rrc, &msg, sizeof(msg), &priority);
    
    if (bytes <= 0) return;  // No message or error
    
    printf("[RRC] Received APP->RRC message: pool_index=%d, dtype=%d\n", 
           msg.pool_index, msg.data_type);
    
    // Get app packet from shared memory
    AppPacketPoolEntry* app_pkt = app_pool_get(&app_pool, msg.pool_index);
    if (!app_pkt || !app_pkt->in_use) {
        fprintf(stderr, "[RRC] Invalid app pool index: %d\n", msg.pool_index);
        return;
    }
    
    // Extract dest_id and src_id from CustomApplicationPacket
    uint8_t dest_id = app_pkt->dest_id;
    uint8_t src_id = app_pkt->src_id;
    
    printf("[RRC] Processing packet: src=%d, dest=%d, dtype=%d, prio=%d\n",
           src_id, dest_id, app_pkt->data_type, app_pkt->priority);
    
    // Step 1: Query OLSR for route to dest_id
    RrcToOlsrMsg olsr_req;
    init_message_header(&olsr_req.header, MSG_RRC_TO_OLSR_ROUTE_REQ);
    olsr_req.dest_node = dest_id;
    olsr_req.src_node = src_id;
    olsr_req.purpose = 0;  // Route lookup
    olsr_req.pool_index = 0;
    
    if (mq_send_msg(&mq_rrc_to_olsr, &olsr_req, sizeof(olsr_req), priority) < 0) {
        fprintf(stderr, "[RRC] Failed to send OLSR route request\n");
        // Send error to app
        RrcToAppMsg err_msg;
        init_message_header(&err_msg.header, MSG_RRC_TO_APP_ERROR);
        err_msg.is_error = 1;
        err_msg.error_code = ERROR_OLSR_NO_ROUTE;
        snprintf(err_msg.error_text, sizeof(err_msg.error_text), 
                 "OLSR: Failed to send route request");
        mq_send_msg(&mq_rrc_to_app, &err_msg, sizeof(err_msg), 0);
        return;
    }
    
    printf("[RRC] Sent OLSR route request for dest=%d\n", dest_id);
    
    // Wait for OLSR response (with timeout)
    OlsrToRrcMsg olsr_rsp;
    bytes = mq_recv_msg_timeout(&mq_olsr_to_rrc, &olsr_rsp, sizeof(olsr_rsp), 
                               &priority, REQUEST_TIMEOUT_MS);
    
    if (bytes == -2) {  // Timeout
        fprintf(stderr, "[RRC] OLSR route request timeout\n");
        RrcToAppMsg err_msg;
        init_message_header(&err_msg.header, MSG_RRC_TO_APP_ERROR);
        err_msg.is_error = 1;
        err_msg.error_code = ERROR_TIMEOUT;
        snprintf(err_msg.error_text, sizeof(err_msg.error_text), 
                 "OLSR: Route request timeout");
        mq_send_msg(&mq_rrc_to_app, &err_msg, sizeof(err_msg), 0);
        return;
    } else if (bytes < 0) {  // Error
        fprintf(stderr, "[RRC] OLSR communication error\n");
        return;
    }
    
    if (olsr_rsp.status != 0) {  // No route found
        fprintf(stderr, "[RRC] OLSR: No route to dest=%d\n", dest_id);
        RrcToAppMsg err_msg;
        init_message_header(&err_msg.header, MSG_RRC_TO_APP_ERROR);
        err_msg.is_error = 1;
        err_msg.error_code = ERROR_OLSR_NO_ROUTE;
        snprintf(err_msg.error_text, sizeof(err_msg.error_text), 
                 "OLSR: No route found to node %d", dest_id);
        mq_send_msg(&mq_rrc_to_app, &err_msg, sizeof(err_msg), 0);
        return;
    }
    
    printf("[RRC] OLSR route found: next_hop=%d, hop_count=%d\n", 
           olsr_rsp.next_hop, olsr_rsp.hop_count);
    
    // Step 2: Check TDMA slot availability for next_hop
    RrcToTdmaMsg tdma_req;
    init_message_header(&tdma_req.header, MSG_RRC_TO_TDMA_SLOT_CHECK);
    tdma_req.req_type = 1;  // Slot check
    tdma_req.next_hop = olsr_rsp.next_hop;
    tdma_req.priority = app_pkt->priority;
    tdma_req.pool_index = 0;
    memset(tdma_req.slot_bitmap, 0, sizeof(tdma_req.slot_bitmap));
    
    if (mq_send_msg(&mq_rrc_to_tdma, &tdma_req, sizeof(tdma_req), priority) < 0) {
        fprintf(stderr, "[RRC] Failed to send TDMA slot check\n");
        RrcToAppMsg err_msg;
        init_message_header(&err_msg.header, MSG_RRC_TO_APP_ERROR);
        err_msg.is_error = 1;
        err_msg.error_code = ERROR_TDMA_SLOT_UNAVAILABLE;
        snprintf(err_msg.error_text, sizeof(err_msg.error_text), 
                 "TDMA: Failed to send slot check");
        mq_send_msg(&mq_rrc_to_app, &err_msg, sizeof(err_msg), 0);
        return;
    }
    
    printf("[RRC] Sent TDMA slot check for next_hop=%d\n", olsr_rsp.next_hop);
    
    // Wait for TDMA response
    TdmaToRrcMsg tdma_rsp;
    bytes = mq_recv_msg_timeout(&mq_tdma_to_rrc, &tdma_rsp, sizeof(tdma_rsp), 
                               &priority, REQUEST_TIMEOUT_MS);
    
    if (bytes == -2) {  // Timeout
        fprintf(stderr, "[RRC] TDMA slot check timeout\n");
        RrcToAppMsg err_msg;
        init_message_header(&err_msg.header, MSG_RRC_TO_APP_ERROR);
        err_msg.is_error = 1;
        err_msg.error_code = ERROR_TIMEOUT;
        snprintf(err_msg.error_text, sizeof(err_msg.error_text), 
                 "TDMA: Slot check timeout");
        mq_send_msg(&mq_rrc_to_app, &err_msg, sizeof(err_msg), 0);
        return;
    } else if (bytes < 0) {
        fprintf(stderr, "[RRC] TDMA communication error\n");
        return;
    }
    
    if (!tdma_rsp.success) {  // No slot available
        fprintf(stderr, "[RRC] TDMA: No slot available for next_hop=%d\n", 
                olsr_rsp.next_hop);
        RrcToAppMsg err_msg;
        init_message_header(&err_msg.header, MSG_RRC_TO_APP_ERROR);
        err_msg.is_error = 1;
        err_msg.error_code = ERROR_TDMA_SLOT_UNAVAILABLE;
        snprintf(err_msg.error_text, sizeof(err_msg.error_text), 
                 "TDMA: No slot available for next hop");
        mq_send_msg(&mq_rrc_to_app, &err_msg, sizeof(err_msg), 0);
        return;
    }
    
    printf("[RRC] TDMA slot available: slot=%d\n", tdma_rsp.assigned_slot);
    
    // Step 3: Allocate frame pool entry and build RRC frame
    int frame_idx = frame_pool_alloc(&frame_pool);
    if (frame_idx < 0) {
        fprintf(stderr, "[RRC] Frame pool full\n");
        RrcToAppMsg err_msg;
        init_message_header(&err_msg.header, MSG_RRC_TO_APP_ERROR);
        err_msg.is_error = 1;
        err_msg.error_code = ERROR_BUFFER_FULL;
        snprintf(err_msg.error_text, sizeof(err_msg.error_text), 
                 "RRC: Frame pool full");
        mq_send_msg(&mq_rrc_to_app, &err_msg, sizeof(err_msg), 0);
        return;
    }
    
    FramePoolEntry frame;
    frame.src_id = src_id;
    frame.dest_id = dest_id;
    frame.next_hop = olsr_rsp.next_hop;
    frame.ttl = 16;
    frame.data_type = app_pkt->data_type;
    frame.priority = app_pkt->priority;
    frame.payload_len = app_pkt->payload_len;
    frame.sequence_number = app_pkt->sequence_number;
    frame.timestamp_ms = get_timestamp_ms();
    memcpy(frame.payload, app_pkt->payload, app_pkt->payload_len);
    frame.in_use = true;
    frame.valid = true;
    
    frame_pool_set(&frame_pool, frame_idx, &frame);
    
    printf("[RRC] Built RRC frame at pool_index=%d: src=%d, dest=%d, next_hop=%d\n",
           frame_idx, src_id, dest_id, olsr_rsp.next_hop);
    
    // Step 4: Route to PHY layer via data-type-specific queue
    // (In real implementation, PHY would read from frame pool)
    // For demo, we just log it
    printf("[RRC] Frame ready for PHY transmission at pool_index=%d\n", frame_idx);
    
    // Release app pool entry
    app_pool_release(&app_pool, msg.pool_index);
    
    printf("[RRC] APP->RRC message processing complete\n\n");
}

// ============================================================================
// MAC -> RRC MESSAGE HANDLER (RX path)
// ============================================================================

void handle_mac_to_rrc_message(void) {
    MacToRrcMsg msg;
    unsigned int priority;
    
    ssize_t bytes = mq_try_recv_msg(&mq_mac_to_rrc, &msg, sizeof(msg), &priority);
    
    if (bytes <= 0) return;
    
    printf("[RRC] Received MAC->RRC frame notification: pool_index=%d, RSSI=%.1f dBm\n",
           msg.pool_index, msg.rssi_dbm);
    
    // Get frame from MAC RX pool
    FramePoolEntry* frame = frame_pool_get(&mac_rx_pool, msg.pool_index);
    if (!frame || !frame->in_use || !frame->valid) {
        fprintf(stderr, "[RRC] Invalid MAC RX frame at pool_index=%d\n", msg.pool_index);
        return;
    }
    
    printf("[RRC] MAC RX frame: src=%d, dest=%d, dtype=%d\n",
           frame->src_id, frame->dest_id, frame->data_type);
    
    // Check if frame is for this node
    if (frame->dest_id != g_node_id) {
        printf("[RRC] Frame not for us (dest=%d), dropping\n", frame->dest_id);
        frame_pool_release(&mac_rx_pool, msg.pool_index);
        return;
    }
    
    // Allocate frame in delivery pool
    int deliver_idx = frame_pool_alloc(&frame_pool);
    if (deliver_idx < 0) {
        fprintf(stderr, "[RRC] Frame delivery pool full\n");
        frame_pool_release(&mac_rx_pool, msg.pool_index);
        return;
    }
    
    // Copy to delivery pool
    frame_pool_set(&frame_pool, deliver_idx, frame);
    
    // Send notification to APP layer
    RrcToAppMsg app_msg;
    init_message_header(&app_msg.header, MSG_RRC_TO_APP_FRAME);
    app_msg.pool_index = deliver_idx;
    app_msg.is_error = 0;
    app_msg.error_code = 0;
    memset(app_msg.error_text, 0, sizeof(app_msg.error_text));
    
    if (mq_send_msg(&mq_rrc_to_app, &app_msg, sizeof(app_msg), priority) < 0) {
        fprintf(stderr, "[RRC] Failed to send frame notification to APP\n");
        frame_pool_release(&frame_pool, deliver_idx);
    } else {
        printf("[RRC] Delivered frame to APP at pool_index=%d\n", deliver_idx);
    }
    
    // Release MAC RX pool entry
    frame_pool_release(&mac_rx_pool, msg.pool_index);
    
    printf("[RRC] MAC->RRC message processing complete\n\n");
}

// ============================================================================
// MAIN PROCESSING LOOP
// ============================================================================

void* rrc_processing_thread(void* arg) {
    printf("[RRC] Processing thread started\n");
    
    while (g_running) {
        // Handle APP->RRC messages
        handle_app_to_rrc_message();
        
        // Handle MAC->RRC messages
        handle_mac_to_rrc_message();
        
        usleep(10000);  // 10ms sleep to avoid busy waiting
    }
    
    printf("[RRC] Processing thread exiting\n");
    return NULL;
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc > 1) {
        g_node_id = atoi(argv[1]);
    }
    
    printf("========================================\n");
    printf("RRC POSIX Integration Core\n");
    printf("Node ID: %d\n", g_node_id);
    printf("========================================\n\n");
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize RRC core
    if (init_rrc_core() < 0) {
        fprintf(stderr, "Failed to initialize RRC core\n");
        return 1;
    }
    
    // Start processing thread
    pthread_t proc_thread;
    if (pthread_create(&proc_thread, NULL, rrc_processing_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create processing thread\n");
        cleanup_rrc_core();
        return 1;
    }
    
    printf("[RRC] Core is running. Press Ctrl+C to exit.\n\n");
    
    // Main thread waits for shutdown signal
    while (g_running) {
        sleep(1);
    }
    
    // Wait for processing thread to exit
    pthread_join(proc_thread, NULL);
    
    // Cleanup
    cleanup_rrc_core();
    
    printf("\n========================================\n");
    printf("RRC Core shutdown complete\n");
    printf("========================================\n");
    
    return 0;
}
