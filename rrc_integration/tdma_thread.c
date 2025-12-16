/**
 * TDMA Layer Thread - L2 MAC
 * Processes slot availability checks and NC slot requests from RRC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include "rrc_message_queue.h"

// TDMA slot management
#define TOTAL_SLOTS 100
typedef struct {
    bool allocated;
    uint8_t owner_node;
    int priority;
} SlotInfo;

static SlotInfo slot_table[TOTAL_SLOTS];
static pthread_mutex_t slot_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Initialize slot table
 */
void init_slot_table(void)
{
    pthread_mutex_lock(&slot_mutex);
    
    for (int i = 0; i < TOTAL_SLOTS; i++) {
        slot_table[i].allocated = false;
        slot_table[i].owner_node = 0;
        slot_table[i].priority = 0;
    }
    
    pthread_mutex_unlock(&slot_mutex);
    
    printf("TDMA: Slot table initialized (%d total slots)\n", TOTAL_SLOTS);
}

/**
 * Check if slot is available for next hop and priority
 */
bool check_slot_availability(uint8_t next_hop_node, int priority)
{
    // Simple simulation: slots available if priority > 5
    bool available = (priority >= 5);
    
    printf("TDMA: Slot check for next_hop=%u priority=%d -> %s\n", 
           next_hop_node, priority, available ? "AVAILABLE" : "NOT AVAILABLE");
    
    return available;
}

/**
 * Allocate NC slot
 */
bool allocate_nc_slot(const uint8_t *payload, size_t payload_len, uint8_t *assigned_slot)
{
    pthread_mutex_lock(&slot_mutex);
    
    // Find first available slot
    for (int i = 0; i < TOTAL_SLOTS; i++) {
        if (!slot_table[i].allocated) {
            slot_table[i].allocated = true;
            slot_table[i].owner_node = 0; // NC slot (no specific owner)
            *assigned_slot = i;
            
            pthread_mutex_unlock(&slot_mutex);
            
            printf("TDMA: NC slot %u allocated (payload_len=%zu)\n", i, payload_len);
            return true;
        }
    }
    
    pthread_mutex_unlock(&slot_mutex);
    
    printf("TDMA: NC slot allocation failed - no slots available\n");
    return false;
}

/**
 * Process relay packet from MAC layer
 */
void process_relay_packet(const uint8_t *packet, size_t packet_len)
{
    printf("TDMA: Relay packet received (%zu bytes) - forwarding to RRC\n", packet_len);
    
    // Create message for RRC
    LayerMessage msg;
    msg.type = MSG_MAC_TO_RRC_RELAY;
    msg.data.mac_relay.packet_len = (packet_len < MAX_RELAY_PACKET_SIZE) ? 
                                     packet_len : MAX_RELAY_PACKET_SIZE;
    memcpy(msg.data.mac_relay.packet_data, packet, msg.data.mac_relay.packet_len);
    
    // Forward to RRC
    if (!message_queue_enqueue(&mac_to_rrc_relay_queue, &msg, 5000)) {
        printf("TDMA: Failed to forward relay packet to RRC\n");
    }
}

/**
 * TDMA Layer Thread - Process messages from RRC
 */
void* tdma_layer_thread(void* arg)
{
    printf("TDMA: Layer thread started\n");
    init_slot_table();
    
    while (1) {
        LayerMessage msg;
        
        // Wait for message from RRC
        if (message_queue_dequeue(&rrc_to_tdma_queue, &msg, 10000)) {
            
            if (msg.type == MSG_TDMA_SLOT_CHECK) {
                // Process slot availability check
                uint8_t next_hop = msg.data.tdma_slot_check.next_hop_node;
                int priority = msg.data.tdma_slot_check.priority;
                uint32_t req_id = msg.data.tdma_slot_check.request_id;
                
                bool available = check_slot_availability(next_hop, priority);
                
                // Send response back to RRC
                LayerMessage response;
                response.type = MSG_TDMA_SLOT_CHECK;
                response.data.tdma_slot_check.request_id = req_id;
                response.data.tdma_slot_check.next_hop_node = next_hop;
                response.data.tdma_slot_check.priority = priority;
                response.data.tdma_slot_check.slot_available = available;
                
                if (!message_queue_enqueue(&tdma_to_rrc_queue, &response, 5000)) {
                    printf("TDMA: Failed to send slot check response\n");
                }
            }
            else if (msg.type == MSG_TDMA_NC_SLOT_REQUEST) {
                // Process NC slot request
                uint32_t req_id = msg.data.tdma_nc_req.request_id;
                size_t payload_len = msg.data.tdma_nc_req.payload_len;
                
                uint8_t assigned_slot = 0;
                bool success = allocate_nc_slot(msg.data.tdma_nc_req.payload, 
                                                 payload_len, &assigned_slot);
                
                // Send response back to RRC
                LayerMessage response;
                response.type = MSG_TDMA_NC_SLOT_RESPONSE;
                response.data.tdma_nc_resp.request_id = req_id;
                response.data.tdma_nc_resp.success = success;
                response.data.tdma_nc_resp.assigned_slot = assigned_slot;
                
                if (!message_queue_enqueue(&tdma_to_rrc_queue, &response, 5000)) {
                    printf("TDMA: Failed to send NC slot response\n");
                }
            }
        }
    }
    
    return NULL;
}

/**
 * Start TDMA layer thread
 */
pthread_t start_tdma_thread(void)
{
    pthread_t thread;
    
    if (pthread_create(&thread, NULL, tdma_layer_thread, NULL) != 0) {
        fprintf(stderr, "TDMA: Failed to create thread\n");
        return 0;
    }
    
    return thread;
}
