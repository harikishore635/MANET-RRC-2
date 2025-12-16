/**
 * PHY Layer Thread - Physical Layer
 * Provides link metrics, link status, and packet counts
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include "rrc_message_queue.h"

// Link state tracking
typedef struct {
    uint8_t node_id;
    float rssi;
    float snr;
    float per;
    bool active;
    uint32_t packet_count;
    time_t last_update;
} LinkState;

#define MAX_LINKS 40
static LinkState link_table[MAX_LINKS];
static pthread_mutex_t link_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Initialize link table with simulated values
 */
void init_link_table(void)
{
    pthread_mutex_lock(&link_mutex);
    
    srand(time(NULL));
    
    for (int i = 0; i < MAX_LINKS; i++) {
        link_table[i].node_id = i + 1;
        link_table[i].rssi = -70.0f - (rand() % 30); // -70 to -100 dBm
        link_table[i].snr = 10.0f + (rand() % 20);   // 10 to 30 dB
        link_table[i].per = 0.01f * (rand() % 30);   // 0 to 0.3
        link_table[i].active = (i < 10); // First 10 links active
        link_table[i].packet_count = rand() % 1000;
        link_table[i].last_update = time(NULL);
    }
    
    pthread_mutex_unlock(&link_mutex);
    
    printf("PHY: Link table initialized with simulated metrics\n");
}

/**
 * Update link metrics (simulates dynamic changes)
 */
void update_link_metrics(uint8_t node_id)
{
    pthread_mutex_lock(&link_mutex);
    
    if (node_id > 0 && node_id <= MAX_LINKS) {
        LinkState *link = &link_table[node_id - 1];
        
        // Add some random variation
        link->rssi += ((rand() % 10) - 5) * 0.5f; // +/- 2.5 dBm
        link->snr += ((rand() % 6) - 3) * 0.5f;   // +/- 1.5 dB
        link->per += ((rand() % 10) - 5) * 0.01f; // +/- 0.05
        
        // Clamp values
        if (link->rssi < -120.0f) link->rssi = -120.0f;
        if (link->rssi > -50.0f) link->rssi = -50.0f;
        if (link->snr < 0.0f) link->snr = 0.0f;
        if (link->snr > 40.0f) link->snr = 40.0f;
        if (link->per < 0.0f) link->per = 0.0f;
        if (link->per > 1.0f) link->per = 1.0f;
        
        link->last_update = time(NULL);
    }
    
    pthread_mutex_unlock(&link_mutex);
}

/**
 * Get link metrics for a node
 */
void get_link_metrics(uint8_t node_id, float *rssi, float *snr, float *per)
{
    pthread_mutex_lock(&link_mutex);
    
    if (node_id > 0 && node_id <= MAX_LINKS) {
        LinkState *link = &link_table[node_id - 1];
        *rssi = link->rssi;
        *snr = link->snr;
        *per = link->per;
    } else {
        *rssi = -120.0f;
        *snr = 0.0f;
        *per = 1.0f;
    }
    
    pthread_mutex_unlock(&link_mutex);
}

/**
 * Check if link is active
 */
bool is_link_active(uint8_t node_id)
{
    bool active = false;
    
    pthread_mutex_lock(&link_mutex);
    
    if (node_id > 0 && node_id <= MAX_LINKS) {
        active = link_table[node_id - 1].active;
    }
    
    pthread_mutex_unlock(&link_mutex);
    
    return active;
}

/**
 * Get packet count for a node
 */
uint32_t get_packet_count(uint8_t node_id)
{
    uint32_t count = 0;
    
    pthread_mutex_lock(&link_mutex);
    
    if (node_id > 0 && node_id <= MAX_LINKS) {
        count = link_table[node_id - 1].packet_count;
        link_table[node_id - 1].packet_count++; // Increment for simulation
    }
    
    pthread_mutex_unlock(&link_mutex);
    
    return count;
}

/**
 * PHY Layer Thread - Process messages from RRC
 */
void* phy_layer_thread(void* arg)
{
    printf("PHY: Layer thread started\n");
    init_link_table();
    
    while (1) {
        LayerMessage msg;
        
        // Wait for message from RRC
        if (message_queue_dequeue(&rrc_to_phy_queue, &msg, 10000)) {
            
            if (msg.type == MSG_PHY_METRICS_REQUEST) {
                // Process metrics request
                uint8_t node_id = msg.data.phy_metrics_req.target_node;
                uint32_t req_id = msg.data.phy_metrics_req.request_id;
                
                // Update metrics to simulate changes
                update_link_metrics(node_id);
                
                // Get current metrics
                float rssi, snr, per;
                get_link_metrics(node_id, &rssi, &snr, &per);
                
                printf("PHY: Metrics request for node %u -> RSSI=%.1f SNR=%.1f PER=%.3f\n",
                       node_id, rssi, snr, per);
                
                // Send response back to RRC
                LayerMessage response;
                response.type = MSG_PHY_METRICS_RESPONSE;
                response.data.phy_metrics_resp.request_id = req_id;
                response.data.phy_metrics_resp.target_node = node_id;
                response.data.phy_metrics_resp.rssi = rssi;
                response.data.phy_metrics_resp.snr = snr;
                response.data.phy_metrics_resp.per = per;
                
                if (!message_queue_enqueue(&phy_to_rrc_queue, &response, 5000)) {
                    printf("PHY: Failed to send metrics response\n");
                }
            }
            else if (msg.type == MSG_PHY_LINK_STATUS) {
                // Process link status request
                uint8_t node_id = msg.data.phy_link_status.target_node;
                uint32_t req_id = msg.data.phy_link_status.request_id;
                
                bool active = is_link_active(node_id);
                
                printf("PHY: Link status request for node %u -> %s\n",
                       node_id, active ? "ACTIVE" : "INACTIVE");
                
                // Send response back to RRC
                LayerMessage response;
                response.type = MSG_PHY_LINK_STATUS;
                response.data.phy_link_status.request_id = req_id;
                response.data.phy_link_status.target_node = node_id;
                response.data.phy_link_status.is_active = active;
                
                if (!message_queue_enqueue(&phy_to_rrc_queue, &response, 5000)) {
                    printf("PHY: Failed to send link status response\n");
                }
            }
            else if (msg.type == MSG_PHY_PACKET_COUNT) {
                // Process packet count request
                uint8_t node_id = msg.data.phy_packet_count.target_node;
                uint32_t req_id = msg.data.phy_packet_count.request_id;
                
                uint32_t count = get_packet_count(node_id);
                
                printf("PHY: Packet count request for node %u -> %u packets\n",
                       node_id, count);
                
                // Send response back to RRC
                LayerMessage response;
                response.type = MSG_PHY_PACKET_COUNT;
                response.data.phy_packet_count.request_id = req_id;
                response.data.phy_packet_count.target_node = node_id;
                response.data.phy_packet_count.packet_count = count;
                
                if (!message_queue_enqueue(&phy_to_rrc_queue, &response, 5000)) {
                    printf("PHY: Failed to send packet count response\n");
                }
            }
        }
    }
    
    return NULL;
}

/**
 * Start PHY layer thread
 */
pthread_t start_phy_thread(void)
{
    pthread_t thread;
    
    if (pthread_create(&thread, NULL, phy_layer_thread, NULL) != 0) {
        fprintf(stderr, "PHY: Failed to create thread\n");
        return 0;
    }
    
    return thread;
}
