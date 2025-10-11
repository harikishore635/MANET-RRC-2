/*
 * RRC Implementation - JSON Application Layer Handler
 * Interfaces with queue[1].c L2 Data Link Layer
 * 
 * Purpose: Parse JSON messages from application layer and hand over data 
 *          to appropriate priority queues in queue[1].c
 * 
 * Priority Mapping:
 * - Analog Voice (PTT): Absolute preemption → analog_voice_queue
 * - Priority 0: Digital Voice → data_queues[0] 
 * - Priority 1: Data → data_queues[1]
 * - Priority 2: Data → data_queues[2]
 * - Priority 3: Data → data_queues[3]
 * - RX Relay: Lowest priority → rx_queue
 * 
 * Integration: Converts JSON to queue[1].c frame structure
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

// Compatibility constants for queue.c
#define PAYLOAD_SIZE_BYTES 16   // From queue.c
#define NUM_PRIORITY 4          // From queue.c
#define QUEUE_SIZE 10           // From queue.c

// Data types from queue.c
typedef enum{
    DATA_TYPE_DIGITAL_VOICE,
    DATA_TYPE_SMS,
    DATA_TYPE_FILE_TRANSFER,
    DATA_TYPE_VIDEO_STREAM,
    DATA_TYPE_ANALOG_VOICE
} DATATYPE;

// Frame structure from queue.c
struct frame{
    uint8_t source_add;
    uint8_t dest_add;
    uint8_t next_hop_add;
    bool rx_or_l3;
    int TTL;
    int priority;
    DATATYPE data_type;
    char payload[PAYLOAD_SIZE_BYTES];
    int payload_length_bytes;
};

// Queue structure from queue.c
struct queue{
    struct frame item[QUEUE_SIZE];
    int front;
    int back;
};


// RRC Data type definitions (Application Layer)
typedef enum {
    RRC_DATA_TYPE_SMS = 0,           // SMS messages
    RRC_DATA_TYPE_VOICE = 1,         // Voice (analog/digital)
    RRC_DATA_TYPE_VIDEO = 2,         // Video stream
    RRC_DATA_TYPE_FILE = 3,          // File transfer
    RRC_DATA_TYPE_RELAY = 4,         // Relay message
    RRC_DATA_TYPE_UNKNOWN = 99
} RRC_DataType;

// Priority mapping for queue[1].c
typedef enum {
    PRIORITY_ANALOG_VOICE_PTT = -1,  // → analog_voice_queue (Absolute preemption)
    PRIORITY_DIGITAL_VOICE = 0,      // → data_queues[0] (Digital Voice)
    PRIORITY_DATA_1 = 1,             // → data_queues[1] (Data Priority 1)
    PRIORITY_DATA_2 = 2,             // → data_queues[2] (Data Priority 2)
    PRIORITY_DATA_3 = 3,             // → data_queues[3] (Data Priority 3)
    PRIORITY_RX_RELAY = 4            // → rx_queue (Lowest priority)
} MessagePriority;

// Transmission type definitions
typedef enum {
    TRANSMISSION_UNICAST = 0,
    TRANSMISSION_MULTICAST = 1,
    TRANSMISSION_BROADCAST = 2
} TransmissionType;

// RRC Application Message structure (simplified for JSON parsing)
typedef struct {
    uint8_t node_id;                 // Source node ID (1-byte for queue[1].c)
    uint8_t dest_node_id;            // Destination node ID (1-byte for queue[1].c)
    RRC_DataType data_type;          // Type of data
    MessagePriority priority;        // Message priority 
    TransmissionType transmission_type; // Unicast/Multicast/Broadcast
    uint8_t *data;                   // Actual data payload
    size_t data_size;                // Size of data in bytes (≤16 bytes for queue[1].c)
    bool preemption_allowed;         // Can this message preempt others
} ApplicationMessage;

// Forward declarations for queue.c integration
// Note: These structures are now defined above

// OLSR Protocol Constants
#define OLSR_HELLO_MESSAGE 1
#define MAX_NEIGHBORS 10

// OLSR Hello Message structures
typedef struct {
    uint32_t neighbor_addr;   // Node ID of discovered neighbor (was neighbor_id)
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

// Simplified OLSR Route structure (for API calls only)
typedef struct {
    uint8_t dest_node_id;
    uint8_t next_hop_id;
    bool route_valid;
} OLSRRoute;

// Link Quality Metrics from PHY/MAC layers
typedef struct {
    uint8_t node_id;           // Node reporting the metrics
    float rssi_dbm;            // Received Signal Strength (dBm)
    float snr_db;              // Signal-to-Noise Ratio (dB)
    float per_percent;         // Packet Error Rate (%)
    uint32_t timestamp;        // When measurement was taken
    bool link_active;          // Is link currently usable
} LinkQualityMetrics;

// RRC Network Management (simplified - no routing table maintenance)
typedef struct {
    LinkQualityMetrics *link_metrics;  // Array of link quality data
    olsr_hello hello_msg;              // Current hello message state
    size_t num_nodes;                  // Number of nodes in network
    time_t last_hello_sent;            // Last hello message timestamp
    time_t next_nc_slot;               // Next Network Control slot time
    bool route_change_pending;         // Flag for route change events
} RRCNetworkManager;

// External queue structures from queue.c (remove custom priority queue)
extern struct queue analog_voice_queue;                 // Analog voice from handset (highest priority)
extern struct queue data_from_l3_queue[NUM_PRIORITY];   // DTE/L7 data queues (Priority 0-3)
extern struct queue rx_queue;                           // Frames received for relay/retransmission
extern struct queue data_to_l3_queue;                   // Frames going up to L3

// External queue functions from queue.c
extern void enqueue(struct queue *q, struct frame rx_f);
extern struct frame dequeue(struct queue *q);
extern bool is_empty(struct queue *q);
extern bool is_full(struct queue *q);

// ============================================================================
// Function Prototypes
// ============================================================================

// JSON Message Handling
ApplicationMessage* parse_json_message(const char *json_string);
char* create_json_message(ApplicationMessage *message);
void free_message(ApplicationMessage *message);
void print_message(ApplicationMessage *message);

// Helper Functions
const char* priority_to_string(MessagePriority priority);
const char* transmission_type_to_string(TransmissionType type);
const char* data_type_to_string(RRC_DataType type);

// RRC to queue.c Integration Functions
struct frame create_frame_from_rrc(ApplicationMessage *app_msg, uint8_t next_hop_node);
void enqueue_to_appropriate_queue(ApplicationMessage *app_msg, uint8_t next_hop_node);

// OLSR Integration Functions
RRCNetworkManager* create_network_manager(size_t num_nodes);
void destroy_network_manager(RRCNetworkManager *manager);
void update_link_quality(RRCNetworkManager *manager, uint8_t node_id, float rssi, float snr, float per);

// OLSR API Functions (simplified - no routing table maintenance)
uint8_t get_next_hop(uint8_t destination_id);
void handle_route_change(uint8_t dest_node, uint8_t new_next_hop);

// OLSR Hello Message Functions
void init_hello_message(RRCNetworkManager *manager, uint8_t node_id, uint8_t willingness);
void add_neighbor_to_hello(RRCNetworkManager *manager, uint32_t neighbor_id, uint8_t link_code);
void send_hello_to_nc_slot(RRCNetworkManager *manager);
void process_received_hello(RRCNetworkManager *manager, olsr_hello *received_hello);
bool is_nc_slot_time(RRCNetworkManager *manager);

// TDMA Network Control Integration
void send_hello_to_tdma_nc_slot(RRCNetworkManager *manager);
ApplicationMessage* create_hello_application_message(RRCNetworkManager *manager, uint8_t source_node_id);
void serialize_hello_message(olsr_hello *hello_msg, uint8_t *buffer, size_t *buffer_size);

// L2 Integration Functions (TDMA/queue.c)
void send_to_l2_queue(ApplicationMessage *app_msg);

// L3 Integration Functions (OLSR Routing)
uint8_t handle_l3_olsr_routing(ApplicationMessage *app_msg, RRCNetworkManager *network_manager);
void demonstrate_hello_message_to_tdma(RRCNetworkManager *network_manager);

// ============================================================================
// RRC to queue.c Integration Functions
// ============================================================================

/**
 * @brief Create frame structure from RRC ApplicationMessage
 * This converts RRC data to queue.c frame format
 */
struct frame create_frame_from_rrc(ApplicationMessage *app_msg, uint8_t next_hop_node) {
    struct frame new_frame = {0};
    
    if (!app_msg) return new_frame;
    
    // Set frame fields from ApplicationMessage
    new_frame.source_add = app_msg->node_id;
    new_frame.dest_add = app_msg->dest_node_id;
    new_frame.next_hop_add = next_hop_node;
    new_frame.rx_or_l3 = false;  // This is L7 data going down to L2
    new_frame.TTL = 10;  // Default TTL
    new_frame.priority = (app_msg->priority == PRIORITY_ANALOG_VOICE_PTT) ? 0 : app_msg->priority;
    
    // Map RRC data types to queue.c DATATYPE
    switch (app_msg->data_type) {
        case RRC_DATA_TYPE_SMS:
            new_frame.data_type = 1; // SMS maps to data type 1
            break;
        case RRC_DATA_TYPE_VOICE:
            new_frame.data_type = (app_msg->priority == PRIORITY_ANALOG_VOICE_PTT) ? 4 : 0; // Analog vs Digital
            break;
        case RRC_DATA_TYPE_VIDEO:
            new_frame.data_type = 3; // Video stream
            break;
        case RRC_DATA_TYPE_FILE:
            new_frame.data_type = 2; // File transfer
            break;
        default:
            new_frame.data_type = 1; // Default to SMS type
            break;
    }
    
    // Copy payload data
    if (app_msg->data && app_msg->data_size > 0) {
        size_t copy_size = (app_msg->data_size > PAYLOAD_SIZE_BYTES) ? 
                          PAYLOAD_SIZE_BYTES : app_msg->data_size;
        memcpy(new_frame.payload, app_msg->data, copy_size);
        new_frame.payload_length_bytes = copy_size;
    }
    
    return new_frame;
}

/**
 * @brief Enqueue ApplicationMessage to appropriate queue based on priority and type
 * This is the main function that replaces the custom priority queue
 */
void enqueue_to_appropriate_queue(ApplicationMessage *app_msg, uint8_t next_hop_node) {
    if (!app_msg) return;
    
    // Create frame from ApplicationMessage
    struct frame new_frame = create_frame_from_rrc(app_msg, next_hop_node);
    
    printf("RRC: Enqueuing message - Priority: %d, Type: %d, From: %u, To: %u\n",
           app_msg->priority, app_msg->data_type, app_msg->node_id, app_msg->dest_node_id);
    
    // Determine which queue to use based on priority and message type
    switch (app_msg->priority) {
        case PRIORITY_ANALOG_VOICE_PTT:
            // Analog voice (PTT) - highest priority queue
            enqueue(&analog_voice_queue, new_frame);
            printf("RRC: → Enqueued to analog_voice_queue (PTT Emergency)\n");
            break;
            
        case PRIORITY_DIGITAL_VOICE:
            // Digital voice - data_from_l3_queue[0]
            enqueue(&data_from_l3_queue[0], new_frame);
            printf("RRC: → Enqueued to data_from_l3_queue[0] (Digital Voice)\n");
            break;
            
        case PRIORITY_DATA_1:
            // Data Priority 1 - data_from_l3_queue[1]
            enqueue(&data_from_l3_queue[1], new_frame);
            printf("RRC: → Enqueued to data_from_l3_queue[1] (Data Priority 1)\n");
            break;
            
        case PRIORITY_DATA_2:
            // Data Priority 2 - data_from_l3_queue[2]
            enqueue(&data_from_l3_queue[2], new_frame);
            printf("RRC: → Enqueued to data_from_l3_queue[2] (Data Priority 2)\n");
            break;
            
        case PRIORITY_DATA_3:
            // Data Priority 3 - data_from_l3_queue[3]
            enqueue(&data_from_l3_queue[3], new_frame);
            printf("RRC: → Enqueued to data_from_l3_queue[3] (Data Priority 3)\n");
            break;
            
        case PRIORITY_RX_RELAY:
        default:
            // Relay messages - rx_queue (lowest priority)
            enqueue(&rx_queue, new_frame);
            printf("RRC: → Enqueued to rx_queue (Relay/Unknown)\n");
            break;
    }
    
    printf("RRC: Frame enqueued successfully\n\n");
}

// ============================================================================
// Simple JSON Parsing Functions (No external dependencies)
// ============================================================================

/**
 * @brief Extract string value from JSON
 */
char* extract_json_string_value(const char *json, const char *key) {
    char search_pattern[100];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    char *key_pos = strstr(json, search_pattern);
    if (!key_pos) return NULL;
    
    char *value_start = strchr(key_pos + strlen(search_pattern), '"');
    if (!value_start) return NULL;
    value_start++; // Skip opening quote
    
    char *value_end = strchr(value_start, '"');
    if (!value_end) return NULL;
    
    size_t value_len = value_end - value_start;
    char *result = (char*)malloc(value_len + 1);
    if (!result) return NULL;
    
    strncpy(result, value_start, value_len);
    result[value_len] = '\0';
    
    return result;
}

/**
 * @brief Extract integer value from JSON
 */
int extract_json_int_value(const char *json, const char *key) {
    char search_pattern[100];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    char *key_pos = strstr(json, search_pattern);
    if (!key_pos) return -1;
    
    char *value_start = key_pos + strlen(search_pattern);
    while (*value_start == ' ' || *value_start == '\t') value_start++;
    
    // Handle hex values (0x...)
    if (strncmp(value_start, "0x", 2) == 0 || strncmp(value_start, "0X", 2) == 0) {
        return (int)strtol(value_start, NULL, 16);
    }
    
    return atoi(value_start);
}

// Parse JSON string into ApplicationMessage structure (simplified)
ApplicationMessage* parse_json_message(const char *json_string) {
    if (!json_string) return NULL;
    
    ApplicationMessage *message = (ApplicationMessage*)calloc(1, sizeof(ApplicationMessage));
    if (!message) return NULL;
    
    // Parse node_id (1-byte for queue[1].c compatibility)
    int node_id = extract_json_int_value(json_string, "node_id");
    if (node_id >= 0) {
        message->node_id = (uint8_t)(node_id & 0xFF);
    }
    
    // Parse dest_node_id (1-byte for queue[1].c compatibility)
    int dest_node_id = extract_json_int_value(json_string, "dest_node_id");
    if (dest_node_id >= 0) {
        message->dest_node_id = (uint8_t)(dest_node_id & 0xFF);
    }
    
    // Parse data_type and assign priority automatically
    char *data_type_str = extract_json_string_value(json_string, "data_type");
    if (data_type_str) {
        if (strcmp(data_type_str, "sms") == 0) {
            message->data_type = RRC_DATA_TYPE_SMS;
            message->priority = PRIORITY_DATA_3; // Priority 3 for SMS
        } else if (strcmp(data_type_str, "voice") == 0 || strcmp(data_type_str, "ptt") == 0) {
            message->data_type = RRC_DATA_TYPE_VOICE;
            message->priority = PRIORITY_ANALOG_VOICE_PTT;
            message->preemption_allowed = true;
        } else if (strcmp(data_type_str, "voice_digital") == 0) {
            message->data_type = RRC_DATA_TYPE_VOICE;
            message->priority = PRIORITY_DIGITAL_VOICE;
        } else if (strcmp(data_type_str, "video") == 0) {
            message->data_type = RRC_DATA_TYPE_VIDEO;
            message->priority = PRIORITY_DATA_1; // Priority 1 for video
        } else if (strcmp(data_type_str, "file") == 0) {
            message->data_type = RRC_DATA_TYPE_FILE;
            message->priority = PRIORITY_DATA_2; // Priority 2 for file
        } else if (strcmp(data_type_str, "relay") == 0) {
            message->data_type = RRC_DATA_TYPE_RELAY;
            message->priority = PRIORITY_RX_RELAY;
        } else {
            message->data_type = RRC_DATA_TYPE_UNKNOWN;
            message->priority = PRIORITY_DATA_3; // Default to lowest data priority
        }
        free(data_type_str);
    }
    
    // Parse transmission_type
    char *transmission_type_str = extract_json_string_value(json_string, "transmission_type");
    if (transmission_type_str) {
        if (strcmp(transmission_type_str, "unicast") == 0) {
            message->transmission_type = TRANSMISSION_UNICAST;
        } else if (strcmp(transmission_type_str, "multicast") == 0) {
            message->transmission_type = TRANSMISSION_MULTICAST;
        } else if (strcmp(transmission_type_str, "broadcast") == 0) {
            message->transmission_type = TRANSMISSION_BROADCAST;
        }
        free(transmission_type_str);
    }
    
    // Parse data and size (queue[1].c limit: 16 bytes max)
    char *data_str = extract_json_string_value(json_string, "data");
    int data_size = extract_json_int_value(json_string, "data_size");
    
    if (data_str && data_size > 0) {
        // Enforce queue[1].c payload size limit
        if (data_size > PAYLOAD_SIZE_BYTES) {
            printf("Warning: Data size %d exceeds queue[1].c limit of %d bytes. Truncating.\n", 
                   data_size, PAYLOAD_SIZE_BYTES);
            data_size = PAYLOAD_SIZE_BYTES;
        }
        
        message->data_size = data_size;
        message->data = (uint8_t*)malloc(data_size + 1); // +1 for null terminator
        if (message->data) {
            strncpy((char*)message->data, data_str, data_size);
            message->data[data_size] = '\0'; // Null terminate
        }
        free(data_str);
    }
    
    return message;
}

// Message creation (no JSON dependency)
ApplicationMessage* create_message(uint8_t node_id, uint8_t dest_node_id, 
                                  RRC_DataType data_type, MessagePriority priority,
                                  const char* data, size_t data_size) {
    ApplicationMessage *message = (ApplicationMessage*)calloc(1, sizeof(ApplicationMessage));
    if (!message) return NULL;
    
    message->node_id = node_id;
    message->dest_node_id = dest_node_id;
    message->data_type = data_type;
    message->priority = priority;
    
    if (data && data_size > 0) {
        // Enforce payload size limit for queue[1].c compatibility
        if (data_size > PAYLOAD_SIZE_BYTES) {
            data_size = PAYLOAD_SIZE_BYTES;
        }
        message->data_size = data_size;
        message->data = (uint8_t*)malloc(data_size + 1);
        if (message->data) {
            memcpy(message->data, data, data_size);
            message->data[data_size] = '\0';
        }
    }
    
    return message;
}

// Free ApplicationMessage
void free_message(ApplicationMessage *message) {
    if (!message) return;
    if (message->data) free(message->data);
    free(message);
}

// Print message details
void print_message(ApplicationMessage *message) {
    if (!message) return;
    
    printf("\n=== Application Message ===\n");
    printf("Node ID: %u\n", message->node_id);
    printf("Destination Node ID: %u\n", message->dest_node_id);
    printf("Data Type: %s\n", data_type_to_string(message->data_type));
    printf("Priority: %s (%d)\n", priority_to_string(message->priority), message->priority);
    printf("Transmission Type: %s\n", transmission_type_to_string(message->transmission_type));
    printf("Data Size: %zu bytes\n", message->data_size);
    printf("Preemption Allowed: %s\n", message->preemption_allowed ? "Yes" : "No");
    printf("===========================\n\n");
}

// Helper functions to convert enums to strings
const char* priority_to_string(MessagePriority priority) {
    switch (priority) {
        case PRIORITY_ANALOG_VOICE_PTT: return "Analog Voice (PTT) - Absolute Preemption";
        case PRIORITY_DIGITAL_VOICE: return "Digital Voice (Priority 0)";
        case PRIORITY_DATA_1: return "Data Priority 1";
        case PRIORITY_DATA_2: return "Data Priority 2";
        case PRIORITY_DATA_3: return "Data Priority 3";
        case PRIORITY_RX_RELAY: return "RX Relay (Lowest Priority)";
        default: return "Unknown Priority";
    }
}

const char* transmission_type_to_string(TransmissionType type) {
    switch (type) {
        case TRANSMISSION_UNICAST: return "Unicast";
        case TRANSMISSION_MULTICAST: return "Multicast";
        case TRANSMISSION_BROADCAST: return "Broadcast";
        default: return "Unknown";
    }
}

const char* data_type_to_string(RRC_DataType type) {
    switch (type) {
        case RRC_DATA_TYPE_SMS: return "sms";
        case RRC_DATA_TYPE_VOICE: return "voice";
        case RRC_DATA_TYPE_VIDEO: return "video";
        case RRC_DATA_TYPE_FILE: return "file";
        case RRC_DATA_TYPE_RELAY: return "relay";
        default: return "unknown";
    }
}

// ============================================================================
// OLSR Integration and Network Management Functions
// ============================================================================

// Create network manager for OLSR integration
RRCNetworkManager* create_network_manager(size_t num_nodes) {
    RRCNetworkManager *manager = (RRCNetworkManager*)malloc(sizeof(RRCNetworkManager));
    if (!manager) return NULL;
    
    manager->num_nodes = num_nodes;
    
    // Allocate link metrics array
    manager->link_metrics = (LinkQualityMetrics*)calloc(num_nodes, sizeof(LinkQualityMetrics));
    if (!manager->link_metrics) {
        free(manager);
        return NULL;
    }
    
    // Initialize hello message state
    manager->last_hello_sent = 0;
    manager->next_nc_slot = 0;
    manager->route_change_pending = false;
    
    return manager;
}

// Destroy network manager
void destroy_network_manager(RRCNetworkManager *manager) {
    if (!manager) return;
    if (manager->link_metrics) free(manager->link_metrics);
    free(manager);
}

// Update link quality metrics (called by MAC layer with PHY measurements)
void update_link_quality(RRCNetworkManager *manager, uint8_t node_id, float rssi, float snr, float per) {
    if (!manager || node_id >= manager->num_nodes) return;
    
    LinkQualityMetrics *metrics = &manager->link_metrics[node_id];
    
    // Store previous values for comparison
    float prev_rssi = metrics->rssi_dbm;
    float prev_snr = metrics->snr_db;
    float prev_per = metrics->per_percent;
    
    // Update current metrics
    metrics->node_id = node_id;
    metrics->rssi_dbm = rssi;
    metrics->snr_db = snr;
    metrics->per_percent = per;
    metrics->timestamp = time(NULL);
    
    // Determine if link is active based on thresholds
    metrics->link_active = (rssi > -85.0f && snr > 10.0f && per < 10.0f);
    
    printf("RRC: Link Quality Update - Node %u: RSSI=%.1f dBm, SNR=%.1f dB, PER=%.1f%% [%s]\n",
           node_id, rssi, snr, per, metrics->link_active ? "ACTIVE" : "POOR");
    
    // Check if significant change requires route update notification
    if (fabs(rssi - prev_rssi) > 5.0f ||    // RSSI changed by >5dB
        fabs(snr - prev_snr) > 3.0f ||      // SNR changed by >3dB  
        fabs(per - prev_per) > 5.0f) {      // PER changed by >5%
        
        printf("RRC: Significant link quality change detected - Route change pending\n");
        manager->route_change_pending = true;
    }
}

// OLSR API Functions Implementation
uint8_t get_next_hop(uint8_t destination_id) {
    // API call to external OLSR daemon/service
    // Returns next hop node ID for the destination
    // Replaces traditional routing table lookup
    
    // For now, return direct routing (simplified)
    // In full implementation, this would make API call to OLSR service
    return destination_id;
}

void handle_route_change(uint8_t dest_node, uint8_t new_next_hop) {
    // Handle notification of route changes from OLSR daemon
    // No internal routing table to update - just notify application layer
    
    printf("Route change: destination %d now via next hop %d\n", dest_node, new_next_hop);
    
    // Set flag for application layer to query new routes
    // This would be handled by RRCNetworkManager if available
}

// OLSR Hello Message Functions Implementation
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

void send_hello_to_nc_slot(RRCNetworkManager *manager) {
    if (!manager) return;
    
    // Use the new TDMA integration function
    send_hello_to_tdma_nc_slot(manager);
}

void process_received_hello(RRCNetworkManager *manager, olsr_hello *received_hello) {
    if (!manager || !received_hello) return;
    
    printf("Received HELLO from node %d with %d neighbors\n", 
           received_hello->originator_addr, received_hello->neighbor_count);
    
    // Process neighbor information and update link quality
    for (int i = 0; i < received_hello->neighbor_count && i < MAX_NEIGHBORS; i++) {
        hello_neighbor *neighbor = &received_hello->neighbors[i];
        printf("  Neighbor %d with link code %d\n", neighbor->neighbor_addr, neighbor->link_code);
        
        // Update link quality metrics for this neighbor
        // This would update our local neighbor information
    }
    
    // Set flag if this hello indicates route changes
    if (received_hello->neighbor_count > 0) {
        manager->route_change_pending = true;
    }
}

bool is_nc_slot_time(RRCNetworkManager *manager) {
    if (!manager) return false;
    
    time_t current_time = time(NULL);
    return (current_time >= manager->next_nc_slot);
}

// ============================================================================
// TDMA Network Control Slot Integration
// ============================================================================

/**
 * @brief Serialize OLSR hello message into byte buffer for TDMA transmission
 */
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
    
    // Serialize neighbors (up to 16 bytes total payload limit)
    int max_neighbors = (16 - offset) / 5; // 5 bytes per neighbor
    int neighbors_to_send = (hello_msg->neighbor_count < max_neighbors) ? 
                           hello_msg->neighbor_count : max_neighbors;
    
    for (int i = 0; i < neighbors_to_send; i++) {
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
}

/**
 * @brief Create ApplicationMessage from OLSR hello for RRC processing
 */
ApplicationMessage* create_hello_application_message(RRCNetworkManager *manager, uint8_t source_node_id) {
    if (!manager) return NULL;
    
    ApplicationMessage *app_msg = (ApplicationMessage*)malloc(sizeof(ApplicationMessage));
    if (!app_msg) return NULL;
    
    // Allocate buffer for serialized hello message
    uint8_t *hello_buffer = (uint8_t*)malloc(16); // 16-byte limit for queue.c
    if (!hello_buffer) {
        free(app_msg);
        return NULL;
    }
    
    size_t buffer_size = 0;
    serialize_hello_message(&manager->hello_msg, hello_buffer, &buffer_size);
    
    // Configure ApplicationMessage for NC slot transmission
    app_msg->node_id = source_node_id;
    app_msg->dest_node_id = 0xFF;  // Broadcast to all nodes in NC slot
    app_msg->data_type = RRC_DATA_TYPE_RELAY;  // Network control message
    app_msg->priority = PRIORITY_DATA_1;       // High priority for network control
    app_msg->transmission_type = TRANSMISSION_BROADCAST;
    app_msg->data = hello_buffer;
    app_msg->data_size = buffer_size;
    app_msg->preemption_allowed = false;  // Don't preempt hello messages
    
    printf("RRC: Created hello ApplicationMessage - %zu bytes for NC slot\n", buffer_size);
    return app_msg;
}

/**
 * @brief Send OLSR hello message to TDMA Network Control slot via RRC
 */
void send_hello_to_tdma_nc_slot(RRCNetworkManager *manager) {
    if (!manager) return;
    
    printf("\n=== SENDING HELLO TO TDMA NC SLOT ===\n");
    printf("TDMA: Preparing hello message for Network Control slot\n");
    
    // Create ApplicationMessage from current hello state
    ApplicationMessage *hello_app_msg = create_hello_application_message(manager, 
                                                                        manager->hello_msg.originator_addr);
    if (!hello_app_msg) {
        printf("TDMA: Failed to create hello ApplicationMessage\n");
        return;
    }
    
    printf("TDMA: Hello message details:\n");
    printf("  Source Node: %u\n", hello_app_msg->node_id);
    printf("  Destination: Broadcast (0xFF)\n");
    printf("  Priority: %d (Network Control)\n", hello_app_msg->priority);
    printf("  Data Size: %zu bytes\n", hello_app_msg->data_size);
    printf("  Transmission: BROADCAST\n");
    
    // Send through RRC processing chain to reach TDMA
    printf("TDMA: Routing hello message through RRC to queue.c...\n");
    enqueue_to_appropriate_queue(hello_app_msg, 0xFF); // Broadcast
    
    // Update timing for next hello transmission
    manager->last_hello_sent = time(NULL);
    manager->next_nc_slot = manager->last_hello_sent + manager->hello_msg.htime;
    manager->hello_msg.msg_seq_num++;
    
    printf("TDMA: Hello message sent to NC slot successfully\n");
    printf("TDMA: Next NC slot scheduled in %u seconds\n", manager->hello_msg.htime);
    printf("=====================================\n\n");
    
    // Clean up
    free(hello_app_msg->data);
    free(hello_app_msg);
}

// ============================================================================
// OLSR Hello Message Demo Function
// ============================================================================

/**
 * @brief Demonstrate OLSR hello message transmission to TDMA NC slot
 */
void demonstrate_hello_message_to_tdma(RRCNetworkManager *network_manager) {
    if (!network_manager) return;
    
    printf("\n=== OLSR HELLO MESSAGE TO TDMA NC SLOT DEMO ===\n");
    
    // Step 1: Initialize hello message for this node
    uint8_t node_id = 1;
    uint8_t willingness = 3; // Medium willingness to be MPR
    init_hello_message(network_manager, node_id, willingness);
    
    printf("Demo: Initialized hello message for node %u\n", node_id);
    
    // Step 2: Add some discovered neighbors
    add_neighbor_to_hello(network_manager, 2, 0x01); // Symmetric link to node 2
    add_neighbor_to_hello(network_manager, 3, 0x02); // Asymmetric link to node 3  
    add_neighbor_to_hello(network_manager, 4, 0x01); // Symmetric link to node 4
    
    printf("Demo: Added %d neighbors to hello message\n", 
           network_manager->hello_msg.neighbor_count);
    
    // Step 3: Set TDMA slot reservation
    network_manager->hello_msg.reserved_slot = 5; // Reserve slot 5 for data
    
    printf("Demo: Reserved TDMA slot %d for data transmission\n", 
           network_manager->hello_msg.reserved_slot);
    
    // Step 4: Send hello message to TDMA NC slot
    printf("Demo: Sending hello message to TDMA Network Control slot...\n");
    send_hello_to_tdma_nc_slot(network_manager);
    
    // Step 5: Simulate periodic hello transmission
    printf("Demo: Simulating periodic hello transmission...\n");
    for (int cycle = 1; cycle <= 3; cycle++) {
        printf("\n--- Hello Cycle %d ---\n", cycle);
        
        // Check if it's time for NC slot
        if (is_nc_slot_time(network_manager)) {
            printf("Demo: NC slot time reached - sending hello\n");
            send_hello_to_tdma_nc_slot(network_manager);
        } else {
            printf("Demo: Not yet time for NC slot\n");
        }
        
        // Simulate time passing (force next slot time)
        network_manager->next_nc_slot = time(NULL);
    }
    
    printf("\n=== HELLO MESSAGE TO TDMA DEMO COMPLETE ===\n\n");
}

// ============================================================================
// ============================================================================
// Interface to queue[1].c 
// ============================================================================

/**
 * @brief Send ApplicationMessage to appropriate queue in queue[1].c
 * This function interfaces with the existing queue[1].c implementation
 */
// ============================================================================
// L3 Layer Functions (OLSR Routing)
// ============================================================================

/**
 * @brief Handle L3 OLSR routing for message
 * Returns next hop node ID or 0 if no route available
 */
uint8_t handle_l3_olsr_routing(ApplicationMessage *app_msg, RRCNetworkManager *network_manager) {
    if (!app_msg || !network_manager) return 0;
    
    printf("\n=== L3 OLSR ROUTING LAYER ===\n");
    printf("L3: Processing routing for message from Node %u to Node %u\n", 
           app_msg->node_id, app_msg->dest_node_id);
    
    // Check if it's time to send hello messages to NC slot
    if (is_nc_slot_time(network_manager)) {
        printf("L3: NC slot time - Sending hello message\n");
        send_hello_to_nc_slot(network_manager);
    }
    
    // Check for route changes
    if (network_manager->route_change_pending) {
        printf("L3: Route change detected - Updating routing information\n");
        handle_route_change(app_msg->dest_node_id, get_next_hop(app_msg->dest_node_id));
        network_manager->route_change_pending = false;
    }
    
    // Get next hop using API call to OLSR service
    uint8_t next_hop = get_next_hop(app_msg->dest_node_id);
    
    if (next_hop == 0) {
        printf("L3: OLSR API - No route available to destination %u\n", app_msg->dest_node_id);
        return 0;
    }
    
    printf("L3: OLSR API route selected - Next hop: %u for destination: %u\n", 
           next_hop, app_msg->dest_node_id);
    printf("===========================\n\n");
    
    return next_hop;
}

// ============================================================================
// L2 Layer Functions (Direct queue.c Integration)
// ============================================================================
/**
 * @brief Enhanced RRC message processing with separate L2/L3 handling
 * This function coordinates between OLSR (L3) and queue.c (L2) layers
 */
void send_to_queue_l2_with_routing(ApplicationMessage *app_msg, RRCNetworkManager *network_manager) {
    if (!app_msg || !app_msg->data || !network_manager) return;
    
    printf("RRC: Starting multi-layer message processing\n");
    printf("RRC: Message - Priority: %d, From: %u, To: %u, Size: %zu bytes\n",
           app_msg->priority, app_msg->node_id, app_msg->dest_node_id, app_msg->data_size);
    
    // Step 1: Handle L3 OLSR Routing
    uint8_t next_hop = handle_l3_olsr_routing(app_msg, network_manager);
    
    if (next_hop == 0) {
        printf("RRC: L3 routing failed - Message cannot be forwarded\n\n");
        return;
    }
    
    // Step 2: Enqueue to appropriate queue.c queue based on priority
    enqueue_to_appropriate_queue(app_msg, next_hop);
    
    printf("RRC: Multi-layer processing completed successfully\n");
    printf("RRC: Message ready for physical transmission\n\n");
}

/**
 * @brief Send ApplicationMessage to appropriate queue in queue.c
 * This function directly interfaces with queue.c without OLSR routing
 */
void send_to_l2_queue(ApplicationMessage *app_msg) {
    if (!app_msg || !app_msg->data) return;
    
    printf("RRC: Direct L2 queueing (no routing)\n");
    printf("     Priority: %d, Type: %d, Size: %zu bytes\n",
           app_msg->priority, app_msg->data_type, app_msg->data_size);
    printf("     From Node: %u, To Node: %u\n", app_msg->node_id, app_msg->dest_node_id);
    
    // Use destination as next hop for direct transmission
    uint8_t next_hop = app_msg->dest_node_id;
    
    // Enqueue to appropriate queue.c queue
    enqueue_to_appropriate_queue(app_msg, next_hop);
    
    printf("RRC: Direct L2 queueing completed\n\n");
}

// Example usage and testing
int main(int argc, char *argv[]) {
    printf("RRC Implementation - JSON to queue.c Integration\n");
    printf("===============================================\n\n");
    
    // Create network manager for OLSR integration (10 nodes max)
    RRCNetworkManager *network_manager = create_network_manager(10);
    if (!network_manager) {
        printf("Failed to create network manager\n");
        return 1;
    }
    
    // Simulate PHY/MAC layer sending link quality metrics to RRC
    printf("========================================\n");
    printf("PHASE 0: Simulating Link Quality Updates from PHY/MAC\n");
    printf("========================================\n");
    update_link_quality(network_manager, 1, -70.5f, 15.2f, 2.1f);   // Good link
    update_link_quality(network_manager, 2, -82.1f, 12.8f, 4.3f);   // Moderate link  
    update_link_quality(network_manager, 3, -88.9f, 8.1f, 12.7f);   // Poor link
    update_link_quality(network_manager, 4, -65.3f, 18.5f, 1.2f);   // Excellent link
    
    // Note: OLSR now uses API calls instead of routing tables
    printf("RRC: Using OLSR API-based routing (no local routing table)\n");
    
    // Example JSON messages for different priorities
    const char *json_examples[] = {
        "{\"node_id\":254, \"dest_node_id\":255, \"data_type\":\"ptt\", \"transmission_type\":\"broadcast\", \"data\":\"Emergency\", \"data_size\":9, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":2, \"data_type\":\"voice_digital\", \"transmission_type\":\"unicast\", \"data\":\"VoiceData\", \"data_size\":9, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":3, \"data_type\":\"video\", \"transmission_type\":\"unicast\", \"data\":\"VideoStream\", \"data_size\":11, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":4, \"data_type\":\"file\", \"transmission_type\":\"unicast\", \"data\":\"FileData\", \"data_size\":8, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":1, \"data_type\":\"sms\", \"transmission_type\":\"unicast\", \"data\":\"Hello\", \"data_size\":5, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":255, \"data_type\":\"relay\", \"transmission_type\":\"broadcast\", \"data\":\"RelayMsg\", \"data_size\":8, \"TTL\":10}"
    };
    
    int num_examples = sizeof(json_examples) / sizeof(json_examples[0]);
    
    printf("\n========================================\n");
    printf("PHASE 1: Parse JSON and Enqueue to queue.c\n");
    printf("========================================\n");
    
    // Parse JSON messages and send directly to queue.c queues
    for (int i = 0; i < num_examples; i++) {
        printf("\n--- Processing JSON Message %d ---\n%s\n", i + 1, json_examples[i]);
        
        ApplicationMessage *msg = parse_json_message(json_examples[i]);
        if (msg) {
            print_message(msg);
            
            // Send to queue.c with OLSR routing
            send_to_queue_l2_with_routing(msg, network_manager);
            
            free_message(msg);
        } else {
            printf("Failed to parse JSON message\n");
        }
    }
    
    printf("\n========================================\n");
    printf("PHASE 2: Demonstrate Direct L2 Queueing\n");
    printf("========================================\n");
    
    // Demonstrate direct L2 queueing without routing
    const char *direct_json = "{\"node_id\":254, \"dest_node_id\":1, \"data_type\":\"sms\", \"data\":\"DirectSMS\", \"data_size\":9}";
    printf("\nDirect L2 queueing example:\n%s\n", direct_json);
    
    ApplicationMessage *direct_msg = parse_json_message(direct_json);
    if (direct_msg) {
        print_message(direct_msg);
        send_to_l2_queue(direct_msg);
        free_message(direct_msg);
    }
    
    // Cleanup
    destroy_network_manager(network_manager);
    
    printf("\n========================================\n");
    printf("RRC to queue.c Integration Completed\n");
    printf("========================================\n");
    
    // Demonstrate OLSR Hello Message to TDMA NC Slot
    printf("\n=== OLSR HELLO TO TDMA NC SLOT DEMO ===\n");
    RRCNetworkManager *hello_network_manager = create_network_manager(10);
    demonstrate_hello_message_to_tdma(hello_network_manager);
    destroy_network_manager(hello_network_manager);
    
    printf("\nSummary:\n");
    printf("- JSON messages parsed from Application Layer\n");
    printf("- Messages prioritized and routed through OLSR\n");
    printf("- OLSR Hello messages integrated with TDMA NC slots\n");
    printf("- Direct integration with queue.c structures:\n");
    printf("  • analog_voice_queue (PTT Emergency)\n");
    printf("  • data_from_l3_queue[0-3] (Priority-based data)\n");
    printf("  • rx_queue (Relay messages)\n");
    printf("  • data_to_l3_queue (Upward data)\n");
    printf("- No custom priority queue needed\n");
    printf("- Ready for TDMA transmission scheduling\n");
    printf("- OLSR hello messages sent to TDMA NC slots\n\n");
    
    return 0;
}
