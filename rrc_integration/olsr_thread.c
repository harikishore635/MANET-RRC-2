/**
 * OLSR Layer Thread - L3 Routing
 * Processes route requests from RRC and responds with next hop information
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#include <unistd.h>
#include "rrc_message_queue.h"

// Simple routing table for demonstration
typedef struct {
    uint8_t destination;
    uint8_t next_hop;
    uint8_t hop_count;
    bool valid;
} RouteEntry;

#define MAX_ROUTES 40
static RouteEntry routing_table[MAX_ROUTES];
static pthread_mutex_t routing_table_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Initialize routing table with some example routes
 */
void init_routing_table(uint8_t my_node_id)
{
    pthread_mutex_lock(&routing_table_mutex);
    
    // Clear routing table
    memset(routing_table, 0, sizeof(routing_table));
    
    // Add some example routes (in real system, OLSR protocol would populate these)
    // Example: Node 1 can reach nodes 2-5 directly
    if (my_node_id == 1) {
        routing_table[0] = (RouteEntry){.destination = 2, .next_hop = 2, .hop_count = 1, .valid = true};
        routing_table[1] = (RouteEntry){.destination = 3, .next_hop = 3, .hop_count = 1, .valid = true};
        routing_table[2] = (RouteEntry){.destination = 4, .next_hop = 2, .hop_count = 2, .valid = true};
        routing_table[3] = (RouteEntry){.destination = 5, .next_hop = 3, .hop_count = 2, .valid = true};
    }
    
    pthread_mutex_unlock(&routing_table_mutex);
    
    printf("OLSR: Routing table initialized for node %u\n", my_node_id);
}

/**
 * Look up next hop for destination
 */
uint8_t lookup_next_hop(uint8_t destination)
{
    uint8_t next_hop = 0xFF; // No route
    
    pthread_mutex_lock(&routing_table_mutex);
    
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].valid && routing_table[i].destination == destination) {
            next_hop = routing_table[i].next_hop;
            break;
        }
    }
    
    pthread_mutex_unlock(&routing_table_mutex);
    
    return next_hop;
}

/**
 * Trigger route discovery (simulated)
 */
void trigger_route_discovery(uint8_t destination)
{
    printf("OLSR: Route discovery triggered for destination %u\n", destination);
    // In real system, this would broadcast RREQ packets
    // For now, just log the request
}

/**
 * OLSR Layer Thread - Process messages from RRC
 */
void* olsr_layer_thread(void* arg)
{
    uint8_t my_node_id = *(uint8_t*)arg;
    
    printf("OLSR: Layer thread started for node %u\n", my_node_id);
    init_routing_table(my_node_id);
    
    while (1) {
        LayerMessage msg;
        
        // Wait for message from RRC
        if (message_queue_dequeue(&rrc_to_olsr_queue, &msg, 10000)) {
            
            if (msg.type == MSG_OLSR_ROUTE_REQUEST) {
                // Process route request
                uint8_t dest = msg.data.olsr_route_req.destination_node;
                uint32_t req_id = msg.data.olsr_route_req.request_id;
                
                printf("OLSR: Route request for destination %u (req_id=%u)\n", dest, req_id);
                
                // Look up next hop
                uint8_t next_hop = lookup_next_hop(dest);
                
                // Send response back to RRC
                LayerMessage response;
                response.type = MSG_OLSR_ROUTE_RESPONSE;
                response.data.olsr_route_resp.request_id = req_id;
                response.data.olsr_route_resp.destination_node = dest;
                response.data.olsr_route_resp.next_hop_node = next_hop;
                response.data.olsr_route_resp.hop_count = (next_hop == 0xFF) ? 0xFF : 1;
                
                if (message_queue_enqueue(&olsr_to_rrc_queue, &response, 5000)) {
                    printf("OLSR: Route response sent - next_hop=%u for dest=%u\n", next_hop, dest);
                } else {
                    printf("OLSR: Failed to send route response\n");
                }
                
                // If no route found, trigger route discovery
                if (next_hop == 0xFF) {
                    trigger_route_discovery(dest);
                }
            }
        }
    }
    
    return NULL;
}

/**
 * Start OLSR layer thread
 */
pthread_t start_olsr_thread(uint8_t node_id)
{
    pthread_t thread;
    uint8_t* node_id_arg = malloc(sizeof(uint8_t));
    *node_id_arg = node_id;
    
    if (pthread_create(&thread, NULL, olsr_layer_thread, node_id_arg) != 0) {
        fprintf(stderr, "OLSR: Failed to create thread\n");
        free(node_id_arg);
        return 0;
    }
    
    return thread;
}
