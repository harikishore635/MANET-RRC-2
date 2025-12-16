/**
 * OLSR Daemon Simulator for RRC POSIX Integration
 * Simulates OLSR routing protocol responses
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

// Simple routing table: dest_node -> next_hop
typedef struct {
    uint8_t dest_node;
    uint8_t next_hop;
    uint8_t hop_count;
    bool valid;
} RouteEntry;

#define MAX_ROUTES 10
static RouteEntry g_routing_table[MAX_ROUTES];

void signal_handler(int signum) {
    printf("[OLSR] Received signal %d, shutting down...\n", signum);
    g_running = false;
}

void init_routing_table(void) {
    memset(g_routing_table, 0, sizeof(g_routing_table));
    
    // Static routes for demo (Node 1 -> Node 2 -> Node 3)
    if (g_node_id == 1) {
        g_routing_table[0].dest_node = 2;
        g_routing_table[0].next_hop = 2;
        g_routing_table[0].hop_count = 1;
        g_routing_table[0].valid = true;
        
        g_routing_table[1].dest_node = 3;
        g_routing_table[1].next_hop = 2;
        g_routing_table[1].hop_count = 2;
        g_routing_table[1].valid = true;
    } else if (g_node_id == 2) {
        g_routing_table[0].dest_node = 1;
        g_routing_table[0].next_hop = 1;
        g_routing_table[0].hop_count = 1;
        g_routing_table[0].valid = true;
        
        g_routing_table[1].dest_node = 3;
        g_routing_table[1].next_hop = 3;
        g_routing_table[1].hop_count = 1;
        g_routing_table[1].valid = true;
    } else if (g_node_id == 3) {
        g_routing_table[0].dest_node = 1;
        g_routing_table[0].next_hop = 2;
        g_routing_table[0].hop_count = 2;
        g_routing_table[0].valid = true;
        
        g_routing_table[1].dest_node = 2;
        g_routing_table[1].next_hop = 2;
        g_routing_table[1].hop_count = 1;
        g_routing_table[1].valid = true;
    }
    
    printf("[OLSR] Routing table initialized\n");
}

RouteEntry* lookup_route(uint8_t dest_node) {
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (g_routing_table[i].valid && g_routing_table[i].dest_node == dest_node) {
            return &g_routing_table[i];
        }
    }
    return NULL;
}

void handle_route_request(MQContext* mq_in, MQContext* mq_out) {
    RrcToOlsrMsg req;
    unsigned int priority;
    
    ssize_t bytes = mq_try_recv_msg(mq_in, &req, sizeof(req), &priority);
    if (bytes <= 0) return;
    
    printf("[OLSR] Route request: dest=%d, src=%d, req_id=%u\n",
           req.dest_node, req.src_node, req.header.request_id);
    
    // Lookup route
    RouteEntry* route = lookup_route(req.dest_node);
    
    // Send response
    OlsrToRrcMsg rsp;
    init_message_header(&rsp.header, MSG_OLSR_TO_RRC_ROUTE_RSP);
    rsp.header.request_id = req.header.request_id;  // Correlation
    rsp.dest_node = req.dest_node;
    
    if (route) {
        rsp.next_hop = route->next_hop;
        rsp.hop_count = route->hop_count;
        rsp.status = 0;  // OK
        printf("[OLSR] Route found: next_hop=%d, hop_count=%d\n", 
               rsp.next_hop, rsp.hop_count);
    } else {
        rsp.next_hop = 0;
        rsp.hop_count = 0;
        rsp.status = 1;  // NO_ROUTE
        printf("[OLSR] No route to dest=%d\n", req.dest_node);
    }
    
    if (mq_send_msg(mq_out, &rsp, sizeof(rsp), priority) < 0) {
        fprintf(stderr, "[OLSR] Failed to send route response\n");
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        g_node_id = atoi(argv[1]);
    }
    
    printf("========================================\n");
    printf("OLSR Daemon Simulator\n");
    printf("Node ID: %d\n", g_node_id);
    printf("========================================\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize routing table
    init_routing_table();
    
    // Open message queues (attach to existing queues created by RRC)
    MQContext mq_rrc_to_olsr, mq_olsr_to_rrc;
    
    if (mq_init(&mq_rrc_to_olsr, MQ_RRC_TO_OLSR, O_RDONLY, false) < 0) {
        fprintf(stderr, "[OLSR] Failed to open RRC->OLSR queue\n");
        return 1;
    }
    
    if (mq_init(&mq_olsr_to_rrc, MQ_OLSR_TO_RRC, O_WRONLY, false) < 0) {
        fprintf(stderr, "[OLSR] Failed to open OLSR->RRC queue\n");
        mq_cleanup(&mq_rrc_to_olsr, false);
        return 1;
    }
    
    printf("[OLSR] Daemon is running. Press Ctrl+C to exit.\n\n");
    
    // Main processing loop
    while (g_running) {
        handle_route_request(&mq_rrc_to_olsr, &mq_olsr_to_rrc);
        usleep(10000);  // 10ms
    }
    
    // Cleanup
    mq_cleanup(&mq_rrc_to_olsr, false);
    mq_cleanup(&mq_olsr_to_rrc, false);
    
    printf("\n[OLSR] Daemon shutdown complete\n");
    return 0;
}
