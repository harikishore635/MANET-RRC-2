/*
 * Minimal Changes for TDMA Team's queue.c
 * ONLY these lines need to be changed in the original queue.c
 * NO RRC functions are duplicated or overwritten
 */

// ============================================================================
// CHANGE 1: Remove MAC_BYTES definition and 6-byte addressing
// ============================================================================

// OLD CODE (in original queue.c):
// #define MAC_BYTES 6
// uint8_t node_addr[MAC_BYTES] = { 0x14, 0x09, 0x1B, 0x44, 0xC3, 0xFE};

// NEW CODE (for RRC compatibility):
uint8_t node_addr = 0xFE; // Single byte node address

// ============================================================================
// CHANGE 2: Update frame structure addressing
// ============================================================================

// OLD CODE (in original queue.c):
/*
struct frame{
    uint8_t source_add[MAC_BYTES];      // 6-byte MAC
    uint8_t dest_add[MAC_BYTES];        // 6-byte MAC
    uint8_t next_hop_add[MAC_BYTES];    // 6-byte MAC
    // ... rest unchanged
};
*/

// NEW CODE (for RRC compatibility):
struct frame{
    uint8_t source_add;      // Single byte
    uint8_t dest_add;        // Single byte  
    uint8_t next_hop_add;    // Single byte
    bool rx_or_l3;           // UNCHANGED
    int TTL;                 // UNCHANGED
    int priority;            // UNCHANGED
    DATATYPE data_type;      // UNCHANGED
    char payload[PAYLOAD_SIZE_BYTES];   // UNCHANGED
    int payload_length_bytes;           // UNCHANGED
    uint16_t checksum;                  // UNCHANGED
};

// ============================================================================
// CHANGE 3: Update address comparisons (3 functions only)
// ============================================================================

// OLD CODE (in original queue.c):
/*
bool our_data(struct frame *frame){
    if (frame->dest_add == node_addr) return true;  // This was array comparison
    else return false;
}
*/

// NEW CODE (for RRC compatibility):
bool our_data(struct frame *frame){
    if (frame->dest_add == node_addr) return true;  // Now single byte comparison
    else return false;
}

// OLD CODE (in original queue.c):
/*
void get_next_hop(struct frame *f){
    uint8_t add_from_l3[MAC_BYTES];
    for(int i=0;i<MAC_BYTES;i++)
        f->next_hop_add[i] = add_from_l3[i];  // Array copy
}
*/

// NEW CODE (for RRC compatibility):
void get_next_hop(struct frame *f){
    uint8_t add_from_l3 = 0xAA;
    f->next_hop_add = add_from_l3;  // Direct assignment
    printf("--- L3 Routing: New next hop assigned (0x%02X). ---\n", f->next_hop_add);
}

// OLD CODE (in original queue.c):
/*
bool next_hop_update(struct frame *frames){
    if (frames->next_hop_add == node_addr){  // This was array comparison
        get_next_hop(frames);
        return true;
    }
    else
        return false;
}
*/

// NEW CODE (for RRC compatibility):
bool next_hop_update(struct frame *frames){
    if (frames->next_hop_add == node_addr){  // Now single byte comparison
        printf("--- Relay Check: Node is the intended next hop. ---\n");
        get_next_hop(frames);
        return true;
    }
    else {
        return false;
    }
}

// ============================================================================
// CHANGE 4: Add interface function for RRC integration
// ============================================================================

// ADD this function to queue.c for RRC integration:
void rrc_to_tdma_interface(uint8_t source_node, uint8_t dest_node, 
                          uint8_t next_hop, int priority, int data_type,
                          const char* payload_data, size_t payload_size,
                          struct queue *analog_voice_queue,
                          struct queue data_queues[],
                          struct queue *rx_queue) {
    
    // Create frame from RRC data (RRC already parsed JSON)
    struct frame new_frame = {0};
    new_frame.source_add = source_node;
    new_frame.dest_add = dest_node;
    new_frame.next_hop_add = next_hop;
    new_frame.rx_or_l3 = false;
    new_frame.TTL = 10;
    new_frame.priority = (priority == -1) ? 0 : priority;
    
    // Map RRC data types to TDMA data types
    switch (data_type) {
        case 0: new_frame.data_type = DATA_TYPE_SMS; break;
        case 1: new_frame.data_type = (priority == -1) ? DATA_TYPE_ANALOG_VOICE : DATA_TYPE_DIGITAL_VOICE; break;
        case 2: new_frame.data_type = DATA_TYPE_VIDEO_STREAM; break;
        case 3: new_frame.data_type = DATA_TYPE_FILE_TRANSFER; break;
        default: new_frame.data_type = DATA_TYPE_SMS; break;
    }
    
    // Copy payload
    if (payload_data && payload_size > 0) {
        size_t copy_size = (payload_size > PAYLOAD_SIZE_BYTES) ? PAYLOAD_SIZE_BYTES : payload_size;
        memcpy(new_frame.payload, payload_data, copy_size);
        new_frame.payload_length_bytes = copy_size;
    }
    
    new_frame.checksum = calculate_checksum(new_frame.payload, new_frame.payload_length_bytes);
    
    // Enqueue based on priority
    if (priority == -1) {
        enqueue(analog_voice_queue, new_frame);  // PTT emergency
    } else if (new_frame.data_type == DATA_TYPE_ANALOG_VOICE) {
        enqueue(analog_voice_queue, new_frame);  // Regular voice
    } else if (priority >= 0 && priority < NUM_PRIORITY) {
        enqueue(&data_queues[priority], new_frame);  // Data queues 0-3
    } else {
        enqueue(rx_queue, new_frame);  // Relay
    }
}

// ============================================================================
// ALL OTHER FUNCTIONS IN queue.c REMAIN EXACTLY THE SAME!
// ============================================================================

/*
These functions need NO CHANGES:
- calculate_checksum()
- is_full()
- is_empty() 
- enqueue()
- dequeue()
- tx()
- main() (can be kept for testing)

Your RRC dup.c needs NO CHANGES - just add one function call to interface with TDMA.
*/

#include <stdio.h>

int main(void) {
    printf("📋 SUMMARY: Minimal Changes for TDMA Integration\n");
    printf("================================================\n\n");
    
    printf("✅ WHAT CHANGES IN TDMA queue.c:\n");
    printf("  1. Remove MAC_BYTES definition\n");
    printf("  2. Change addressing from arrays to single bytes\n");
    printf("  3. Update 3 address comparison functions\n");
    printf("  4. Add 1 interface function for RRC\n");
    printf("  5. Keep all other functions unchanged\n\n");
    
    printf("✅ WHAT STAYS THE SAME:\n");
    printf("  • Your RRC dup.c - NO CHANGES needed\n");
    printf("  • Your JSON parsing - stays in RRC\n");
    printf("  • Your priority system - works perfectly\n");
    printf("  • Your OLSR integration - unchanged\n");
    printf("  • TDMA scheduling logic - unchanged\n\n");
    
    printf("✅ INTEGRATION APPROACH:\n");
    printf("  • RRC does JSON parsing (your code)\n");
    printf("  • RRC calls rrc_to_tdma_interface() with parsed data\n");
    printf("  • TDMA receives structured data, not JSON\n");
    printf("  • No code duplication\n");
    printf("  • Clean separation of concerns\n\n");
    
    printf("🎯 RESULT:\n");
    printf("  Your excellent RRC work remains intact!\n");
    printf("  TDMA gets minimal, targeted changes.\n");
    printf("  Perfect integration with no overwrites.\n\n");
    
    return 0;
}