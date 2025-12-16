/**
 * RRC API Wrappers - Message Queue Based Implementation
 * Extracted from rccv2.c for demo purposes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#define HAVE_STRUCT_TIMESPEC
#include "rrc_message_queue.h"

// Next hop update tracking
typedef struct {
    uint8_t dest_node;
    uint32_t update_count;
    uint8_t last_next_hop;
} NextHopUpdateStats;

#define MAX_NEXT_HOP_STATS 40
static NextHopUpdateStats next_hop_stats[MAX_NEXT_HOP_STATS];
static int next_hop_stats_count = 0;

// ============================================================================
// MESSAGE QUEUE-BASED API IMPLEMENTATIONS
// ============================================================================

/**
 * Get next hop for destination from OLSR via message queue
 * Returns next hop node ID, or 0xFF if no route available
 */
uint8_t olsr_get_next_hop(uint8_t destination_node_id)
{
    LayerMessage msg;
    msg.type = MSG_OLSR_ROUTE_REQUEST;
    msg.data.olsr_route_req.request_id = generate_request_id();
    msg.data.olsr_route_req.destination_node = destination_node_id;
    
    // Send request to OLSR layer
    if (!message_queue_enqueue(&rrc_to_olsr_queue, &msg, 5000)) {
        return 0xFF; // Timeout - no route
    }
    
    // Wait for response with timeout
    LayerMessage response;
    if (!message_queue_dequeue(&olsr_to_rrc_queue, &response, 5000)) {
        return 0xFF; // Timeout
    }
    
    if (response.type != MSG_OLSR_ROUTE_RESPONSE || 
        response.data.olsr_route_resp.request_id != msg.data.olsr_route_req.request_id) {
        return 0xFF; // Invalid or mismatched response
    }
    
    uint8_t next_hop = response.data.olsr_route_resp.next_hop_node;
    
    // Track next hop changes
    int stat_idx = -1;
    for (int i = 0; i < next_hop_stats_count; i++) {
        if (next_hop_stats[i].dest_node == destination_node_id) {
            stat_idx = i;
            break;
        }
    }
    
    if (stat_idx == -1 && next_hop_stats_count < MAX_NEXT_HOP_STATS) {
        stat_idx = next_hop_stats_count++;
        next_hop_stats[stat_idx].dest_node = destination_node_id;
        next_hop_stats[stat_idx].update_count = 0;
        next_hop_stats[stat_idx].last_next_hop = 0xFF;
    }
    
    if (stat_idx >= 0) {
        if (next_hop_stats[stat_idx].last_next_hop != next_hop && 
            next_hop_stats[stat_idx].last_next_hop != 0xFF) {
            next_hop_stats[stat_idx].update_count++;
            
            // If next hop changes frequently (>5 times), trigger route rediscovery
            if (next_hop_stats[stat_idx].update_count > 5) {
                olsr_trigger_route_discovery(destination_node_id);
                next_hop_stats[stat_idx].update_count = 0; // Reset counter
            }
        }
        next_hop_stats[stat_idx].last_next_hop = next_hop;
    }
    
    return next_hop;
}

/**
 * Trigger route discovery for destination (fire-and-forget)
 */
void olsr_trigger_route_discovery(uint8_t destination_node_id)
{
    LayerMessage msg;
    msg.type = MSG_OLSR_ROUTE_REQUEST;
    msg.data.olsr_route_req.request_id = generate_request_id();
    msg.data.olsr_route_req.destination_node = destination_node_id;
    
    // Fire and forget - just queue the request
    message_queue_enqueue(&rrc_to_olsr_queue, &msg, 1000);
}

/**
 * Check if TDMA slot is available for next hop
 */
bool tdma_check_slot_available(uint8_t next_hop_node, int priority)
{
    LayerMessage msg;
    msg.type = MSG_TDMA_SLOT_CHECK;
    msg.data.tdma_slot_check.request_id = generate_request_id();
    msg.data.tdma_slot_check.next_hop_node = next_hop_node;
    msg.data.tdma_slot_check.priority = priority;
    
    // Send request to TDMA layer
    if (!message_queue_enqueue(&rrc_to_tdma_queue, &msg, 5000)) {
        return false; // Timeout
    }
    
    // Wait for response
    LayerMessage response;
    if (!message_queue_dequeue(&tdma_to_rrc_queue, &response, 5000)) {
        return false; // Timeout
    }
    
    if (response.type != MSG_TDMA_SLOT_CHECK || 
        response.data.tdma_slot_check.request_id != msg.data.tdma_slot_check.request_id) {
        return false; // Invalid response
    }
    
    return response.data.tdma_slot_check.slot_available;
}

/**
 * Request NC slot from TDMA layer
 */
bool tdma_request_nc_slot(const uint8_t *payload, size_t payload_len, uint8_t *assigned_slot)
{
    if (!payload || !assigned_slot || payload_len > MAX_NC_PAYLOAD_SIZE) {
        return false;
    }
    
    LayerMessage msg;
    msg.type = MSG_TDMA_NC_SLOT_REQUEST;
    msg.data.tdma_nc_req.request_id = generate_request_id();
    msg.data.tdma_nc_req.payload_len = payload_len;
    memcpy(msg.data.tdma_nc_req.payload, payload, payload_len);
    
    // Send request to TDMA layer
    if (!message_queue_enqueue(&rrc_to_tdma_queue, &msg, 5000)) {
        return false; // Timeout
    }
    
    // Wait for response
    LayerMessage response;
    if (!message_queue_dequeue(&tdma_to_rrc_queue, &response, 5000)) {
        return false; // Timeout
    }
    
    if (response.type != MSG_TDMA_NC_SLOT_RESPONSE || 
        response.data.tdma_nc_resp.request_id != msg.data.tdma_nc_req.request_id) {
        return false; // Invalid response
    }
    
    if (response.data.tdma_nc_resp.success) {
        *assigned_slot = response.data.tdma_nc_resp.assigned_slot;
        return true;
    }
    
    return false;
}

/**
 * Get link metrics from PHY layer
 */
void phy_get_link_metrics(uint8_t node_id, float *rssi, float *snr, float *per)
{
    LayerMessage msg;
    msg.type = MSG_PHY_METRICS_REQUEST;
    msg.data.phy_metrics_req.request_id = generate_request_id();
    msg.data.phy_metrics_req.target_node = node_id;
    
    // Send request to PHY layer
    if (!message_queue_enqueue(&rrc_to_phy_queue, &msg, 5000)) {
        // Timeout - return default poor values
        if (rssi) *rssi = -120.0f;
        if (snr) *snr = 0.0f;
        if (per) *per = 1.0f;
        return;
    }
    
    // Wait for response
    LayerMessage response;
    if (!message_queue_dequeue(&phy_to_rrc_queue, &response, 5000)) {
        // Timeout - return default poor values
        if (rssi) *rssi = -120.0f;
        if (snr) *snr = 0.0f;
        if (per) *per = 1.0f;
        return;
    }
    
    if (response.type != MSG_PHY_METRICS_RESPONSE || 
        response.data.phy_metrics_resp.request_id != msg.data.phy_metrics_req.request_id) {
        // Invalid response
        if (rssi) *rssi = -120.0f;
        if (snr) *snr = 0.0f;
        if (per) *per = 1.0f;
        return;
    }
    
    // Return metrics
    if (rssi) *rssi = response.data.phy_metrics_resp.rssi;
    if (snr) *snr = response.data.phy_metrics_resp.snr;
    if (per) *per = response.data.phy_metrics_resp.per;
}

/**
 * Check if PHY link is active
 */
bool phy_is_link_active(uint8_t node_id)
{
    LayerMessage msg;
    msg.type = MSG_PHY_LINK_STATUS;
    msg.data.phy_link_status.request_id = generate_request_id();
    msg.data.phy_link_status.target_node = node_id;
    
    // Send request to PHY layer
    if (!message_queue_enqueue(&rrc_to_phy_queue, &msg, 5000)) {
        return false; // Timeout
    }
    
    // Wait for response
    LayerMessage response;
    if (!message_queue_dequeue(&phy_to_rrc_queue, &response, 5000)) {
        return false; // Timeout
    }
    
    if (response.type != MSG_PHY_LINK_STATUS || 
        response.data.phy_link_status.request_id != msg.data.phy_link_status.request_id) {
        return false; // Invalid response
    }
    
    return response.data.phy_link_status.is_active;
}

/**
 * Get packet count from PHY layer
 */
uint32_t phy_get_packet_count(uint8_t node_id)
{
    LayerMessage msg;
    msg.type = MSG_PHY_PACKET_COUNT;
    msg.data.phy_packet_count.request_id = generate_request_id();
    msg.data.phy_packet_count.target_node = node_id;
    
    // Send request to PHY layer
    if (!message_queue_enqueue(&rrc_to_phy_queue, &msg, 5000)) {
        return 0; // Timeout
    }
    
    // Wait for response
    LayerMessage response;
    if (!message_queue_dequeue(&phy_to_rrc_queue, &response, 5000)) {
        return 0; // Timeout
    }
    
    if (response.type != MSG_PHY_PACKET_COUNT || 
        response.data.phy_packet_count.request_id != msg.data.phy_packet_count.request_id) {
        return 0; // Invalid response
    }
    
    return response.data.phy_packet_count.packet_count;
}
