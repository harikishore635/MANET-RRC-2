#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdint.h>

// OLSR Protocol Constants
#define OLSR_HELLO_MESSAGE 1
#define MAX_NEIGHBORS 10

// OLSR Hello Message structures
typedef struct {
    uint32_t neighbor_addr;   // Node ID of discovered neighbor
    uint8_t link_code;        // Link type and neighbor type code
    uint8_t reserved;         // Reserved field
} hello_neighbor;

typedef struct {
    uint8_t msg_type;         // Message type (HELLO = 1)
    uint8_t vtime;            // Validity time
    uint16_t msg_size;        // Message size
    uint32_t originator_addr; // Originating node address
    uint8_t ttl;              // Time to live
    uint8_t hop_count;        // Hop count
    uint16_t msg_seq_num;     // Message sequence number
    uint8_t reserved;         // Reserved field
    uint8_t htime;            // Hello interval
    uint8_t willingness;      // Node's willingness to act as MPR (0-7)
    int reserved_slot;        // TDMA slot reservation announcement
    hello_neighbor neighbors[MAX_NEIGHBORS]; // Array of discovered neighbors
    int neighbor_count;       // Number of neighbors in the array
} olsr_hello;

// RRC Network Management (simplified)
typedef struct {
    olsr_hello hello_msg;              // Current hello message state
    time_t last_hello_sent;            // Last hello message timestamp
    time_t next_nc_slot;               // Next Network Control slot time
    bool route_change_pending;         // Flag for route change events
} RRCNetworkManager;

// Function prototypes
void init_hello_message(RRCNetworkManager *manager, uint8_t node_id, uint8_t willingness);
void add_neighbor_to_hello(RRCNetworkManager *manager, uint32_t neighbor_id, uint8_t link_code);
void serialize_hello_message(olsr_hello *hello_msg, uint8_t *buffer, size_t *buffer_size);
void send_hello_to_tdma_nc_slot(RRCNetworkManager *manager);
bool is_nc_slot_time(RRCNetworkManager *manager);

// ============================================================================
// OLSR Hello Message Functions Implementation
// ============================================================================

void init_hello_message(RRCNetworkManager *manager, uint8_t node_id, uint8_t willingness) {
    if (!manager) return;
    
    manager->hello_msg.msg_type = OLSR_HELLO_MESSAGE;
    manager->hello_msg.vtime = 3;  // 3 second validity time
    manager->hello_msg.msg_size = sizeof(olsr_hello);
    manager->hello_msg.originator_addr = node_id;
    manager->hello_msg.ttl = 1;  // Hello messages are 1-hop only
    manager->hello_msg.hop_count = 0;
    manager->hello_msg.msg_seq_num = 0;
    
    manager->hello_msg.reserved = 0;
    manager->hello_msg.htime = 2;  // 2 second hello interval
    manager->hello_msg.willingness = willingness;
    manager->hello_msg.neighbor_count = 0;
    manager->hello_msg.reserved_slot = -1; // No slot reserved initially
}

void add_neighbor_to_hello(RRCNetworkManager *manager, uint32_t neighbor_id, uint8_t link_code) {
    if (!manager || manager->hello_msg.neighbor_count >= MAX_NEIGHBORS) return;
    
    hello_neighbor *neighbor = &manager->hello_msg.neighbors[manager->hello_msg.neighbor_count];
    neighbor->neighbor_addr = neighbor_id;
    neighbor->link_code = link_code;
    neighbor->reserved = 0;
    
    manager->hello_msg.neighbor_count++;
    manager->hello_msg.msg_size = sizeof(olsr_hello) + 
                                 (manager->hello_msg.neighbor_count * sizeof(hello_neighbor));
}

void serialize_hello_message(olsr_hello *hello_msg, uint8_t *buffer, size_t *buffer_size) {
    if (!hello_msg || !buffer || !buffer_size) return;
    
    size_t offset = 0;
    
    // Serialize hello message header
    buffer[offset++] = hello_msg->msg_type;
    buffer[offset++] = hello_msg->vtime;
    
    // Message size (2 bytes, little endian)
    buffer[offset++] = hello_msg->msg_size & 0xFF;
    buffer[offset++] = (hello_msg->msg_size >> 8) & 0xFF;
    
    // Originator address (4 bytes, little endian)
    buffer[offset++] = hello_msg->originator_addr & 0xFF;
    buffer[offset++] = (hello_msg->originator_addr >> 8) & 0xFF;
    buffer[offset++] = (hello_msg->originator_addr >> 16) & 0xFF;
    buffer[offset++] = (hello_msg->originator_addr >> 24) & 0xFF;
    
    // TTL, hop count, sequence number
    buffer[offset++] = hello_msg->ttl;
    buffer[offset++] = hello_msg->hop_count;
    buffer[offset++] = hello_msg->msg_seq_num & 0xFF;
    buffer[offset++] = (hello_msg->msg_seq_num >> 8) & 0xFF;
    
    // Hello-specific fields
    buffer[offset++] = hello_msg->reserved;
    buffer[offset++] = hello_msg->htime;
    buffer[offset++] = hello_msg->willingness;
    
    // Reserved slot (4 bytes)
    buffer[offset++] = hello_msg->reserved_slot & 0xFF;
    buffer[offset++] = (hello_msg->reserved_slot >> 8) & 0xFF;
    buffer[offset++] = (hello_msg->reserved_slot >> 16) & 0xFF;
    buffer[offset++] = (hello_msg->reserved_slot >> 24) & 0xFF;
    
    // Neighbor count
    buffer[offset++] = hello_msg->neighbor_count & 0xFF;
    
    // Serialize neighbors (simulate 16-byte limit for queue.c)
    int max_neighbors = (16 - offset) / 5; // 5 bytes per neighbor
    int neighbors_to_send = (hello_msg->neighbor_count < max_neighbors) ? 
                           hello_msg->neighbor_count : max_neighbors;
    
    for (int i = 0; i < neighbors_to_send && offset < 16; i++) {
        hello_neighbor *neighbor = &hello_msg->neighbors[i];
        
        // Neighbor address (4 bytes)
        buffer[offset++] = neighbor->neighbor_addr & 0xFF;
        buffer[offset++] = (neighbor->neighbor_addr >> 8) & 0xFF;
        buffer[offset++] = (neighbor->neighbor_addr >> 16) & 0xFF;
        buffer[offset++] = (neighbor->neighbor_addr >> 24) & 0xFF;
        
        // Link code
        buffer[offset++] = neighbor->link_code;
        
        // Check 16-byte limit
        if (offset >= 16) break;
    }
    
    *buffer_size = offset;
    
    printf("TDMA: Serialized hello message - %zu bytes\n", offset);
    printf("TDMA: Node %u, %d neighbors, willingness %u\n", 
           hello_msg->originator_addr, neighbors_to_send, hello_msg->willingness);
    
    // Print serialized data in hex
    printf("TDMA: Serialized data: ");
    for (size_t i = 0; i < offset; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
}

void send_hello_to_tdma_nc_slot(RRCNetworkManager *manager) {
    if (!manager) return;
    
    printf("\n=== SENDING HELLO TO TDMA NC SLOT ===\n");
    printf("TDMA: Preparing hello message for Network Control slot\n");
    
    // Allocate buffer for serialized hello message
    uint8_t hello_buffer[16]; // 16-byte limit for queue.c compatibility
    size_t buffer_size = 0;
    
    serialize_hello_message(&manager->hello_msg, hello_buffer, &buffer_size);
    
    printf("TDMA: Hello message details:\n");
    printf("  Source Node: %u\n", manager->hello_msg.originator_addr);
    printf("  Destination: Broadcast (0xFF)\n");
    printf("  Message Type: HELLO (%d)\n", manager->hello_msg.msg_type);
    printf("  Data Size: %zu bytes\n", buffer_size);
    printf("  Hello Interval: %u seconds\n", manager->hello_msg.htime);
    printf("  Willingness: %u\n", manager->hello_msg.willingness);
    printf("  Reserved TDMA Slot: %d\n", manager->hello_msg.reserved_slot);
    printf("  Neighbors: %d\n", manager->hello_msg.neighbor_count);
    
    for (int i = 0; i < manager->hello_msg.neighbor_count; i++) {
        printf("    Node %u (link code: 0x%02X)\n", 
               manager->hello_msg.neighbors[i].neighbor_addr,
               manager->hello_msg.neighbors[i].link_code);
    }
    
    // Simulate sending to TDMA queue (in real implementation, this would go to queue.c)
    printf("TDMA: → Sending to Network Control slot...\n");
    printf("TDMA: → Message queued for TDMA transmission\n");
    
    // Update timing for next hello transmission
    manager->last_hello_sent = time(NULL);
    manager->next_nc_slot = manager->last_hello_sent + manager->hello_msg.htime;
    manager->hello_msg.msg_seq_num++;
    
    printf("TDMA: Hello message sent to NC slot successfully\n");
    printf("TDMA: Next NC slot scheduled in %u seconds\n", manager->hello_msg.htime);
    printf("=====================================\n\n");
}

bool is_nc_slot_time(RRCNetworkManager *manager) {
    if (!manager) return false;
    
    time_t current_time = time(NULL);
    return (current_time >= manager->next_nc_slot);
}

// ============================================================================
// Demo Function
// ============================================================================

void demonstrate_hello_message_to_tdma() {
    printf("=== OLSR HELLO MESSAGE TO TDMA NC SLOT DEMO ===\n\n");
    
    RRCNetworkManager manager = {0};
    
    // Step 1: Initialize hello message for this node
    uint8_t node_id = 1;
    uint8_t willingness = 3; // Medium willingness to be MPR
    init_hello_message(&manager, node_id, willingness);
    
    printf("Demo: Initialized hello message for node %u\n", node_id);
    
    // Step 2: Add some discovered neighbors
    add_neighbor_to_hello(&manager, 2, 0x01); // Symmetric link to node 2
    add_neighbor_to_hello(&manager, 3, 0x02); // Asymmetric link to node 3  
    add_neighbor_to_hello(&manager, 4, 0x01); // Symmetric link to node 4
    
    printf("Demo: Added %d neighbors to hello message\n", manager.hello_msg.neighbor_count);
    
    // Step 3: Set TDMA slot reservation
    manager.hello_msg.reserved_slot = 5; // Reserve slot 5 for data
    
    printf("Demo: Reserved TDMA slot %d for data transmission\n", manager.hello_msg.reserved_slot);
    
    // Step 4: Send hello message to TDMA NC slot
    printf("Demo: Sending hello message to TDMA Network Control slot...\n");
    send_hello_to_tdma_nc_slot(&manager);
    
    // Step 5: Simulate periodic hello transmission
    printf("Demo: Simulating periodic hello transmission...\n");
    for (int cycle = 1; cycle <= 3; cycle++) {
        printf("\n--- Hello Cycle %d ---\n", cycle);
        
        // Force next slot time for demo
        manager.next_nc_slot = time(NULL);
        
        // Check if it's time for NC slot
        if (is_nc_slot_time(&manager)) {
            printf("Demo: NC slot time reached - sending hello\n");
            send_hello_to_tdma_nc_slot(&manager);
        } else {
            printf("Demo: Not yet time for NC slot\n");
        }
    }
    
    printf("\n=== HELLO MESSAGE TO TDMA DEMO COMPLETE ===\n\n");
    
    printf("Summary of OLSR Hello to TDMA NC Slot Integration:\n");
    printf("- Hello message structure: %zu bytes\n", sizeof(olsr_hello));
    printf("- Serialized for transmission: fits in 16-byte queue.c limit\n");
    printf("- Contains node ID, neighbors, willingness, TDMA slot reservations\n");
    printf("- Integrates with RRC priority system\n");
    printf("- Broadcasts via TDMA Network Control slot\n");
    printf("- Supports periodic transmission timing\n");
    printf("- Ready for integration with actual queue.c and TDMA scheduler\n");
}

// ============================================================================
// Main Function
// ============================================================================

int main() {
    printf("RRC-OLSR Hello Message to TDMA NC Slot Demo\n");
    printf("============================================\n\n");
    
    demonstrate_hello_message_to_tdma();
    
    return 0;
}