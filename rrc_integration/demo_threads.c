/**
 * Demo Harness - Message Queue System Integration
 * Demonstrates RRC communicating with OLSR, TDMA, and PHY layers via message queues
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#include <unistd.h>
#include "rrc_message_queue.h"

// External thread start functions
extern pthread_t start_olsr_thread(uint8_t node_id);
extern pthread_t start_tdma_thread(void);
extern pthread_t start_phy_thread(void);

// External API functions (now using message queues internally)
extern uint8_t olsr_get_next_hop(uint8_t destination_node_id);
extern void olsr_trigger_route_discovery(uint8_t destination_node_id);
extern bool tdma_check_slot_available(uint8_t next_hop_node, int priority);
extern bool tdma_request_nc_slot(const uint8_t *payload, size_t payload_len, uint8_t *assigned_slot);
extern void phy_get_link_metrics(uint8_t node_id, float *rssi, float *snr, float *per);
extern bool phy_is_link_active(uint8_t node_id);
extern uint32_t phy_get_packet_count(uint8_t node_id);

/**
 * Test OLSR communication
 */
void test_olsr_communication(void)
{
    printf("\n=== Testing OLSR Communication ===\n");
    
    // Test 1: Get next hop for destination 3
    printf("\nTest 1: Get next hop for destination 3\n");
    uint8_t next_hop = olsr_get_next_hop(3);
    printf("RRC: Next hop for dest 3 = %u\n", next_hop);
    
    // Test 2: Get next hop for destination 5
    printf("\nTest 2: Get next hop for destination 5\n");
    next_hop = olsr_get_next_hop(5);
    printf("RRC: Next hop for dest 5 = %u\n", next_hop);
    
    // Test 3: Trigger route discovery
    printf("\nTest 3: Trigger route discovery for destination 10\n");
    olsr_trigger_route_discovery(10);
    sleep(1);
}

/**
 * Test TDMA communication
 */
void test_tdma_communication(void)
{
    printf("\n=== Testing TDMA Communication ===\n");
    
    // Test 1: Check slot availability with high priority
    printf("\nTest 1: Check slot availability (next_hop=2, priority=10)\n");
    bool available = tdma_check_slot_available(2, 10);
    printf("RRC: Slot available = %s\n", available ? "YES" : "NO");
    
    // Test 2: Check slot availability with low priority
    printf("\nTest 2: Check slot availability (next_hop=3, priority=3)\n");
    available = tdma_check_slot_available(3, 3);
    printf("RRC: Slot available = %s\n", available ? "YES" : "NO");
    
    // Test 3: Request NC slot
    printf("\nTest 3: Request NC slot\n");
    uint8_t payload[64] = {1, 2, 3, 4, 5};
    uint8_t assigned_slot = 0;
    bool success = tdma_request_nc_slot(payload, 5, &assigned_slot);
    printf("RRC: NC slot request %s, assigned slot = %u\n", 
           success ? "SUCCESS" : "FAILED", assigned_slot);
}

/**
 * Test PHY communication
 */
void test_phy_communication(void)
{
    printf("\n=== Testing PHY Communication ===\n");
    
    // Test 1: Get link metrics for node 2
    printf("\nTest 1: Get link metrics for node 2\n");
    float rssi, snr, per;
    phy_get_link_metrics(2, &rssi, &snr, &per);
    printf("RRC: Link metrics - RSSI=%.1f dBm, SNR=%.1f dB, PER=%.3f\n", rssi, snr, per);
    
    // Test 2: Get link metrics for node 5
    printf("\nTest 2: Get link metrics for node 5\n");
    phy_get_link_metrics(5, &rssi, &snr, &per);
    printf("RRC: Link metrics - RSSI=%.1f dBm, SNR=%.1f dB, PER=%.3f\n", rssi, snr, per);
    
    // Test 3: Check link status
    printf("\nTest 3: Check link status for node 3\n");
    bool active = phy_is_link_active(3);
    printf("RRC: Link status = %s\n", active ? "ACTIVE" : "INACTIVE");
    
    // Test 4: Get packet count
    printf("\nTest 4: Get packet count for node 2\n");
    uint32_t count = phy_get_packet_count(2);
    printf("RRC: Packet count = %u\n", count);
}

/**
 * Test application layer to RRC communication
 */
void test_app_to_rrc_communication(void)
{
    printf("\n=== Testing Application to RRC Communication ===\n");
    
    // Simulate application sending traffic to RRC
    LayerMessage msg;
    msg.type = MSG_APP_TO_RRC_TRAFFIC;
    msg.data.app_traffic.source_node = 1;
    msg.data.app_traffic.dest_node = 3;
    msg.data.app_traffic.priority = 5;
    msg.data.app_traffic.data_len = 100;
    memset(msg.data.app_traffic.data, 0xAA, 100);
    
    printf("APP: Sending traffic to RRC (src=1, dst=3, len=100)\n");
    if (message_queue_enqueue(&app_to_rrc_queue, &msg, 5000)) {
        printf("APP: Traffic message enqueued successfully\n");
    } else {
        printf("APP: Failed to enqueue traffic message\n");
    }
}

/**
 * Display message queue statistics
 */
void display_queue_statistics(void)
{
    printf("\n=== Message Queue Statistics ===\n");
    
    MessageQueueStats stats;
    
    printf("\nRRC -> OLSR Queue:\n");
    get_message_queue_stats(&rrc_to_olsr_queue, &stats);
    printf("  Enqueued: %u, Dequeued: %u, Overflows: %u\n",
           stats.enqueue_count, stats.dequeue_count, stats.overflow_count);
    
    printf("\nOLSR -> RRC Queue:\n");
    get_message_queue_stats(&olsr_to_rrc_queue, &stats);
    printf("  Enqueued: %u, Dequeued: %u, Overflows: %u\n",
           stats.enqueue_count, stats.dequeue_count, stats.overflow_count);
    
    printf("\nRRC -> TDMA Queue:\n");
    get_message_queue_stats(&rrc_to_tdma_queue, &stats);
    printf("  Enqueued: %u, Dequeued: %u, Overflows: %u\n",
           stats.enqueue_count, stats.dequeue_count, stats.overflow_count);
    
    printf("\nTDMA -> RRC Queue:\n");
    get_message_queue_stats(&tdma_to_rrc_queue, &stats);
    printf("  Enqueued: %u, Dequeued: %u, Overflows: %u\n",
           stats.enqueue_count, stats.dequeue_count, stats.overflow_count);
    
    printf("\nRRC -> PHY Queue:\n");
    get_message_queue_stats(&rrc_to_phy_queue, &stats);
    printf("  Enqueued: %u, Dequeued: %u, Overflows: %u\n",
           stats.enqueue_count, stats.dequeue_count, stats.overflow_count);
    
    printf("\nPHY -> RRC Queue:\n");
    get_message_queue_stats(&phy_to_rrc_queue, &stats);
    printf("  Enqueued: %u, Dequeued: %u, Overflows: %u\n",
           stats.enqueue_count, stats.dequeue_count, stats.overflow_count);
}

/**
 * Main demo function
 */
int main(void)
{
    printf("========================================\n");
    printf("RRC Message Queue System Demo\n");
    printf("========================================\n");
    
    // Initialize all message queues
    printf("\nInitializing message queues...\n");
    init_all_message_queues();
    printf("Message queues initialized\n");
    
    // Start layer threads
    printf("\nStarting layer threads...\n");
    uint8_t my_node_id = 1;
    pthread_t olsr_thread = start_olsr_thread(my_node_id);
    pthread_t tdma_thread = start_tdma_thread();
    pthread_t phy_thread = start_phy_thread();
    
    if (!olsr_thread || !tdma_thread || !phy_thread) {
        fprintf(stderr, "Failed to start all layer threads\n");
        return 1;
    }
    
    printf("All layer threads started\n");
    
    // Wait for threads to initialize
    sleep(2);
    
    // Run tests
    test_olsr_communication();
    sleep(1);
    
    test_tdma_communication();
    sleep(1);
    
    test_phy_communication();
    sleep(1);
    
    test_app_to_rrc_communication();
    sleep(1);
    
    // Display statistics
    display_queue_statistics();
    
    printf("\n========================================\n");
    printf("Demo completed successfully!\n");
    printf("========================================\n");
    
    // Keep threads running for a bit
    printf("\nThreads will continue running for 5 more seconds...\n");
    sleep(5);
    
    printf("Demo finished\n");
    
    // Note: In a real system, you would call cleanup_all_message_queues()
    // and join threads properly. For demo purposes, we'll just exit.
    
    return 0;
}
