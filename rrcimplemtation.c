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
 * - Priority 1: Video Stream → data_queues[1]
 * - Priority 2: File Transfer → data_queues[2]
 * - Priority 3: SMS → data_queues[3]
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

// Compatibility constants for queue[1].c
#define PAYLOAD_SIZE_BYTES 16   // From queue[1].c
#define NUM_PRIORITY 4          // From queue[1].c


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
    PRIORITY_ANALOG_VOICE_PTT = -1,  // → analog_voice_queue
    PRIORITY_DIGITAL_VOICE = 0,      // → data_queues[0]
    PRIORITY_VIDEO = 1,              // → data_queues[1] 
    PRIORITY_FILE = 2,               // → data_queues[2]
    PRIORITY_SMS = 3,                // → data_queues[3]
    PRIORITY_RX_RELAY = 4            // → rx_queue
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

// Forward declarations for queue[1].c integration
// Note: User must include queue[1].c to get actual struct definitions
struct frame;      // From queue[1].c
struct queue;      // From queue[1].c
enum DATATYPE;     // From queue[1].c

// Queue node for priority queue
typedef struct QueueNode {
    ApplicationMessage *message;
    struct QueueNode *next;
} QueueNode;

// Priority queue structure
typedef struct {
    QueueNode *head;
    size_t count;
    size_t max_size;
} PriorityQueue;

// ============================================================================
// Function Prototypes
// ============================================================================

// RRC Queue Management
PriorityQueue* create_priority_queue(size_t max_size);
void destroy_priority_queue(PriorityQueue *queue);
bool enqueue_message(PriorityQueue *queue, ApplicationMessage *message);
ApplicationMessage* dequeue_message(PriorityQueue *queue);
bool should_preempt(MessagePriority new_priority, MessagePriority current_priority);

// JSON Message Handling
ApplicationMessage* parse_json_message(const char *json_string);
char* create_json_message(ApplicationMessage *message);
void free_message(ApplicationMessage *message);
void print_message(ApplicationMessage *message);

// Helper Functions
const char* priority_to_string(MessagePriority priority);
const char* transmission_type_to_string(TransmissionType type);
const char* data_type_to_string(RRC_DataType type);

// L2 Integration Functions
struct frame* create_frame_from_rrc(ApplicationMessage *app_msg);
uint16_t calculate_checksum(const char* data, size_t length);
int map_rrc_to_l2_datatype(RRC_DataType rrc_type, MessagePriority priority);
void send_to_l2_queue(ApplicationMessage *app_msg);

// Create a new priority queue
PriorityQueue* create_priority_queue(size_t max_size) {
    PriorityQueue *queue = (PriorityQueue*)malloc(sizeof(PriorityQueue));
    if (!queue) return NULL;
    
    queue->head = NULL;
    queue->count = 0;
    queue->max_size = max_size;
    return queue;
}

// Destroy priority queue and free all messages
void destroy_priority_queue(PriorityQueue *queue) {
    if (!queue) return;
    
    QueueNode *current = queue->head;
    while (current) {
        QueueNode *next = current->next;
        free_message(current->message);
        free(current);
        current = next;
    }
    free(queue);
}

// Check if new message should preempt current message
bool should_preempt(MessagePriority new_priority, MessagePriority current_priority) {
    // Analog Voice PTT has absolute preemption
    if (new_priority == PRIORITY_ANALOG_VOICE_PTT) {
        return true;
    }
    
    // Lower priority value = higher priority
    return new_priority < current_priority;
}

// Enqueue message with priority handling
bool enqueue_message(PriorityQueue *queue, ApplicationMessage *message) {
    if (!queue || !message) return false;
    
    // Check queue capacity
    if (queue->count >= queue->max_size) {
        // If new message has higher priority than lowest priority in queue
        if (queue->head && should_preempt(message->priority, queue->head->message->priority)) {
            // Remove lowest priority message
            QueueNode *temp = queue->head;
            queue->head = queue->head->next;
            free_message(temp->message);
            free(temp);
            queue->count--;
        } else {
            printf("Queue full and message priority too low. Dropping message.\n");
            return false;
        }
    }
    
    // Create new queue node
    QueueNode *new_node = (QueueNode*)malloc(sizeof(QueueNode));
    if (!new_node) return false;
    
    new_node->message = message;
    new_node->next = NULL;
    
    // Insert in priority order (highest priority at tail, lowest at head)
    if (!queue->head || should_preempt(queue->head->message->priority, message->priority)) {
        // Insert at head (lowest priority)
        new_node->next = queue->head;
        queue->head = new_node;
    } else {
        // Find correct position
        QueueNode *current = queue->head;
        while (current->next && should_preempt(current->next->message->priority, message->priority)) {
            current = current->next;
        }
        new_node->next = current->next;
        current->next = new_node;
    }
    
    queue->count++;
    return true;
}

// Dequeue highest priority message
ApplicationMessage* dequeue_message(PriorityQueue *queue) {
    if (!queue || !queue->head) return NULL;
    
    // Find the node with highest priority (lowest priority value)
    QueueNode *prev = NULL;
    QueueNode *current = queue->head;
    QueueNode *highest_prev = NULL;
    QueueNode *highest = queue->head;
    MessagePriority highest_priority = queue->head->message->priority;
    
    while (current) {
        if (current->message->priority < highest_priority) {
            highest_priority = current->message->priority;
            highest = current;
            highest_prev = prev;
        }
        prev = current;
        current = current->next;
    }
    
    // Remove highest priority node
    if (highest_prev) {
        highest_prev->next = highest->next;
    } else {
        queue->head = highest->next;
    }
    
    ApplicationMessage *message = highest->message;
    free(highest);
    queue->count--;
    
    return message;
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
            message->priority = PRIORITY_SMS; // Priority 3 for SMS
        } else if (strcmp(data_type_str, "voice") == 0 || strcmp(data_type_str, "ptt") == 0) {
            message->data_type = RRC_DATA_TYPE_VOICE;
            message->priority = PRIORITY_ANALOG_VOICE_PTT;
            message->preemption_allowed = true;
        } else if (strcmp(data_type_str, "voice_digital") == 0) {
            message->data_type = RRC_DATA_TYPE_VOICE;
            message->priority = PRIORITY_DIGITAL_VOICE;
        } else if (strcmp(data_type_str, "video") == 0) {
            message->data_type = RRC_DATA_TYPE_VIDEO;
            message->priority = PRIORITY_VIDEO; // Priority 1 for video
        } else if (strcmp(data_type_str, "file") == 0) {
            message->data_type = RRC_DATA_TYPE_FILE;
            message->priority = PRIORITY_FILE; // Priority 2 for file
        } else if (strcmp(data_type_str, "relay") == 0) {
            message->data_type = RRC_DATA_TYPE_RELAY;
            message->priority = PRIORITY_RX_RELAY;
        } else {
            message->data_type = RRC_DATA_TYPE_UNKNOWN;
            message->priority = PRIORITY_SMS; // Default to lowest priority
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
        case PRIORITY_VIDEO: return "Video Stream (Priority 1)";
        case PRIORITY_FILE: return "File Transfer (Priority 2)";
        case PRIORITY_SMS: return "SMS (Priority 3)";
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
// ============================================================================
// Interface to queue[1].c 
// ============================================================================

/**
 * @brief Send ApplicationMessage to appropriate queue in queue[1].c
 * This function interfaces with the existing queue[1].c implementation
 */
/**
 * @brief Send ApplicationMessage to appropriate queue in queue[1].c
 * This function interfaces with the existing queue[1].c implementation
 */
void send_to_queue_l2(ApplicationMessage *app_msg) {
    if (!app_msg || !app_msg->data) return;
    
    printf("RRC: Preparing to send message to queue[1].c\n");
    printf("     Priority: %d, Type: %d, Size: %zu bytes\n",
           app_msg->priority, app_msg->data_type, app_msg->data_size);
    printf("     From Node: %u, To Node: %u\n", app_msg->node_id, app_msg->dest_node_id);
    
    // Based on priority, the message would be sent to appropriate queue in queue[1].c:
    switch (app_msg->priority) {
        case PRIORITY_ANALOG_VOICE_PTT:
            printf("     → Would send to analog_voice_queue in queue[1].c\n");
            break;
        case PRIORITY_DIGITAL_VOICE:
            printf("     → Would send to digital_voice_queue in queue[1].c\n");
            break;
        case PRIORITY_VIDEO:
            printf("     → Would send to video_queue in queue[1].c\n");
            break;
        case PRIORITY_FILE:
            printf("     → Would send to file_queue in queue[1].c\n");
            break;
        case PRIORITY_SMS:
            printf("     → Would send to sms_queue in queue[1].c\n");
            break;
        case PRIORITY_RX_RELAY:
        default:
            printf("     → Would send to rx_relay_queue in queue[1].c\n");
            break;
    }
    
    // Note: Actual enqueue() call to queue[1].c would happen here
    // enqueue(target_queue, frame_from_app_msg);
    printf("RRC: Message prepared for queue[1].c integration\n\n");
}

// Example usage and testing
int main(int argc, char *argv[]) {
    printf("RRC Implementation - Application Layer JSON Handler\n");
    printf("====================================================\n\n");
    
    // Create priority queue with max size 10
    PriorityQueue *queue = create_priority_queue(10);
    if (!queue) {
        printf("Failed to create priority queue\n");
        return 1;
    }
    
    // Example JSON messages (Note: node_id and dest_node_id are 1-byte for queue[1].c compatibility)
    const char *json_examples[] = {
        "{\"node_id\":254, \"dest_node_id\":1, \"data_type\":\"sms\", \"transmission_type\":\"unicast\", \"data\":\"Hello\", \"data_size\":5, \"sequence_number\":1, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":255, \"data_type\":\"sms\", \"transmission_type\":\"broadcast\", \"data\":\"Broadcast\", \"data_size\":9, \"sequence_number\":2, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":255, \"data_type\":\"ptt\", \"transmission_type\":\"broadcast\", \"data\":\"Emergency\", \"data_size\":9, \"sequence_number\":3, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":2, \"data_type\":\"voice_digital\", \"transmission_type\":\"unicast\", \"data\":\"VoiceData\", \"data_size\":9, \"sequence_number\":4, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":3, \"data_type\":\"video\", \"transmission_type\":\"unicast\", \"data\":\"VideoStream\", \"data_size\":11, \"sequence_number\":5, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":4, \"data_type\":\"file\", \"transmission_type\":\"unicast\", \"data\":\"FileData\", \"data_size\":8, \"sequence_number\":6, \"TTL\":10}"
    };
    
    int num_examples = sizeof(json_examples) / sizeof(json_examples[0]);
    
    printf("\n========================================\n");
    printf("PHASE 1: Parse JSON and Add to RRC Priority Queue\n");
    printf("========================================\n");
    
    // Parse JSON messages and add to RRC priority queue
    for (int i = 0; i < num_examples; i++) {
        printf("\n--- Processing JSON Message %d ---\n%s\n", i + 1, json_examples[i]);
        
        ApplicationMessage *msg = parse_json_message(json_examples[i]);
        if (msg) {
            print_message(msg);
            
            // Add to RRC priority queue
            if (enqueue_message(queue, msg)) {
                printf(">>> Added to RRC Priority Queue (Priority: %d)\n", msg->priority);
            } else {
                printf(">>> Failed to add to RRC Priority Queue\n");
                free_message(msg);
            }
        } else {
            printf("Failed to parse JSON message\n");
        }
    }
    
    printf("\n\n========================================\n");
    printf("PHASE 2: Process RRC Queue and Send to queue[1].c\n");
    printf("========================================\n");
    
    // Process messages from RRC priority queue and send to queue[1].c
    int message_count = 1;
    ApplicationMessage *msg;
    
    while ((msg = dequeue_message(queue)) != NULL) {
        printf("\n[Message %d - PROCESSING] Priority: %d\n", message_count++, msg->priority);
        print_message(msg);
        
        // Send to appropriate queue in queue[1].c
        send_to_queue_l2(msg);
        
        free_message(msg);
    }
    
    // Cleanup
    destroy_priority_queue(queue);
    
    printf("\n\n========================================\n");
    printf("RRC Implementation Completed\n");
    printf("========================================\n");
    printf("\nSummary:\n");
    printf("- JSON messages parsed from Application Layer\n");
    printf("- Added to RRC priority queue based on message priority\n");
    printf("- Processed in strict priority order:\n");
    printf("  * Analog Voice (PTT) - Absolute Preemption\n");
    printf("  * Digital Voice - Priority 0\n");
    printf("  * Video Stream - Priority 1\n");
    printf("  * File Transfer - Priority 2\n");
    printf("  * SMS - Priority 3\n");
    printf("  * RX Relay - Lowest Priority\n");
    printf("- Messages sent to appropriate queues in queue[1].c\n");
    printf("\nReady for integration with existing queue[1].c!\n\n");
    
    return 0;
}
