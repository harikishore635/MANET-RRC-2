/*
 * COMPLETE JSON FLOW DEMONSTRATION
 * L7 Application → RRC → TDMA Integration
 * 
 * This shows the complete flow:
 * 1. Application Layer sends JSON to RRC
 * 2. RRC parses JSON (using YOUR existing functions)
 * 3. RRC sends parsed data to TDMA (clean interface, no JSON duplication)
 * 4. TDMA queues based on RRC's priority mapping
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// ============================================================================
// RRC DATA STRUCTURES (From your rrcimplemtation.c)
// ============================================================================

#define PAYLOAD_SIZE_BYTES 16
#define NUM_PRIORITY 4

typedef enum {
    RRC_DATA_TYPE_SMS = 0,
    RRC_DATA_TYPE_VOICE = 1,
    RRC_DATA_TYPE_VIDEO = 2,
    RRC_DATA_TYPE_FILE = 3,
    RRC_DATA_TYPE_RELAY = 4,
    RRC_DATA_TYPE_UNKNOWN = 99
} RRC_DataType;

typedef enum {
    PRIORITY_ANALOG_VOICE_PTT = -1,
    PRIORITY_DIGITAL_VOICE = 0,
    PRIORITY_VIDEO = 1,
    PRIORITY_FILE = 2,
    PRIORITY_SMS = 3,
    PRIORITY_RX_RELAY = 4
} MessagePriority;

typedef enum {
    TRANSMISSION_UNICAST = 0,
    TRANSMISSION_MULTICAST = 1,
    TRANSMISSION_BROADCAST = 2
} TransmissionType;

typedef struct {
    uint8_t node_id;
    uint8_t dest_node_id;
    RRC_DataType data_type;
    MessagePriority priority;
    TransmissionType transmission_type;
    uint8_t *data;
    size_t data_size;
    bool preemption_allowed;
} ApplicationMessage;

// ============================================================================
// TDMA DATA STRUCTURES (From queue.c)
// ============================================================================

typedef enum{
    DATA_TYPE_DIGITAL_VOICE,
    DATA_TYPE_SMS,
    DATA_TYPE_FILE_TRANSFER,
    DATA_TYPE_VIDEO_STREAM,
    DATA_TYPE_ANALOG_VOICE
} DATATYPE;

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
    uint16_t checksum;
};

struct queue{
    struct frame item[10];
    int front;
    int back;
};

// ============================================================================
// RRC JSON PARSING FUNCTIONS (From your rrcimplemtation.c)
// ============================================================================

/**
 * @brief Extract string value from JSON (YOUR RRC FUNCTION)
 */
char* extract_json_string_value(const char *json, const char *key) {
    char search_pattern[100];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    char *key_pos = strstr(json, search_pattern);
    if (!key_pos) return NULL;
    
    char *value_start = strchr(key_pos + strlen(search_pattern), '"');
    if (!value_start) return NULL;
    value_start++;
    
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
 * @brief Extract integer value from JSON (YOUR RRC FUNCTION)
 */
int extract_json_int_value(const char *json, const char *key) {
    char search_pattern[100];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    char *key_pos = strstr(json, search_pattern);
    if (!key_pos) return -1;
    
    char *value_start = key_pos + strlen(search_pattern);
    while (*value_start == ' ' || *value_start == '\t') value_start++;
    
    if (strncmp(value_start, "0x", 2) == 0 || strncmp(value_start, "0X", 2) == 0) {
        return (int)strtol(value_start, NULL, 16);
    }
    
    return atoi(value_start);
}

/**
 * @brief Parse JSON string into ApplicationMessage (YOUR RRC FUNCTION)
 */
ApplicationMessage* parse_json_message(const char *json_string) {
    if (!json_string) return NULL;
    
    ApplicationMessage *message = (ApplicationMessage*)calloc(1, sizeof(ApplicationMessage));
    if (!message) return NULL;
    
    // Parse node_id
    int node_id = extract_json_int_value(json_string, "node_id");
    if (node_id >= 0) {
        message->node_id = (uint8_t)(node_id & 0xFF);
    }
    
    // Parse dest_node_id
    int dest_node_id = extract_json_int_value(json_string, "dest_node_id");
    if (dest_node_id >= 0) {
        message->dest_node_id = (uint8_t)(dest_node_id & 0xFF);
    }
    
    // Parse data_type and assign priority automatically
    char *data_type_str = extract_json_string_value(json_string, "data_type");
    if (data_type_str) {
        if (strcmp(data_type_str, "sms") == 0) {
            message->data_type = RRC_DATA_TYPE_SMS;
            message->priority = PRIORITY_SMS;
        } else if (strcmp(data_type_str, "voice") == 0 || strcmp(data_type_str, "ptt") == 0) {
            message->data_type = RRC_DATA_TYPE_VOICE;
            message->priority = PRIORITY_ANALOG_VOICE_PTT;
            message->preemption_allowed = true;
        } else if (strcmp(data_type_str, "voice_digital") == 0) {
            message->data_type = RRC_DATA_TYPE_VOICE;
            message->priority = PRIORITY_DIGITAL_VOICE;
        } else if (strcmp(data_type_str, "video") == 0) {
            message->data_type = RRC_DATA_TYPE_VIDEO;
            message->priority = PRIORITY_VIDEO;
        } else if (strcmp(data_type_str, "file") == 0) {
            message->data_type = RRC_DATA_TYPE_FILE;
            message->priority = PRIORITY_FILE;
        } else if (strcmp(data_type_str, "relay") == 0) {
            message->data_type = RRC_DATA_TYPE_RELAY;
            message->priority = PRIORITY_RX_RELAY;
        } else {
            message->data_type = RRC_DATA_TYPE_UNKNOWN;
            message->priority = PRIORITY_SMS;
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
    
    // Parse data and size
    char *data_str = extract_json_string_value(json_string, "data");
    int data_size = extract_json_int_value(json_string, "data_size");
    
    if (data_str && data_size > 0) {
        if (data_size > PAYLOAD_SIZE_BYTES) {
            data_size = PAYLOAD_SIZE_BYTES;
        }
        
        message->data_size = data_size;
        message->data = (uint8_t*)malloc(data_size + 1);
        if (message->data) {
            strncpy((char*)message->data, data_str, data_size);
            message->data[data_size] = '\0';
        }
        free(data_str);
    }
    
    return message;
}

void free_message(ApplicationMessage *message) {
    if (!message) return;
    if (message->data) free(message->data);
    free(message);
}

// ============================================================================
// TDMA QUEUE FUNCTIONS
// ============================================================================

uint16_t calculate_checksum(const char* data, size_t length) {
    uint32_t sum = 0;
    const uint16_t* ptr = (const uint16_t*)data;

    while(length > 1) {
        sum += *ptr++;
        length -= 2;
    }

    if(length == 1)
        sum += *(uint8_t*)ptr;

    while(sum>>16)
        sum = (sum & 0xFFFF) + (sum>>16);
    
    return (uint16_t)~sum;
}

bool is_full(struct queue *q) {
    return (q->back == 9);
}

bool is_empty(struct queue *q) {
    return (q->front == -1 || q->front > q->back);
}

void enqueue(struct queue *q, struct frame rx_f) {
    if(is_full(q)) {
        printf("[TDMA] Queue full. Frame dropped.\n");
        return;
    }
    
    if(q->front == -1) q->front = 0;

    q->back++;
    q->item[q->back] = rx_f;
    printf("[TDMA] Frame queued (Front: %d, Back: %d).\n", q->front, q->back);
}

struct frame dequeue(struct queue *q) {
    if(is_empty(q)) {
        struct frame empty_frame = {0};
        return empty_frame;
    }

    struct frame dequeued_frame = q->item[q->front];
    q->front++;

    if (q->front > q->back) {
        q->front = -1;
        q->back = -1;
    }

    return dequeued_frame;
}

// ============================================================================
// CLEAN RRC → TDMA INTERFACE (No JSON duplication!)
// ============================================================================

/**
 * @brief Interface function: RRC sends already-parsed data to TDMA
 * This function receives data that RRC already parsed from JSON
 * NO JSON PARSING HAPPENS HERE - RRC already did that!
 */
void rrc_to_tdma_interface(ApplicationMessage *app_msg,
                          struct queue *analog_voice_queue,
                          struct queue data_queues[],
                          struct queue *rx_queue) {
    
    if (!app_msg) return;
    
    printf("\n[RRC→TDMA INTERFACE] Received parsed data from RRC:\n");
    printf("  Source: %u → Dest: %u\n", app_msg->node_id, app_msg->dest_node_id);
    printf("  Priority: %d, Type: %d\n", app_msg->priority, app_msg->data_type);
    printf("  Data: \"%s\" (%zu bytes)\n", (char*)app_msg->data, app_msg->data_size);
    
    // Create TDMA frame from RRC's already-parsed data
    struct frame new_frame = {0};
    new_frame.source_add = app_msg->node_id;
    new_frame.dest_add = app_msg->dest_node_id;
    new_frame.next_hop_add = app_msg->dest_node_id; // Simple routing
    new_frame.rx_or_l3 = false;
    new_frame.TTL = 10;
    new_frame.priority = (app_msg->priority == -1) ? 0 : app_msg->priority;
    
    // Map RRC data types to TDMA data types
    switch (app_msg->data_type) {
        case RRC_DATA_TYPE_SMS:
            new_frame.data_type = DATA_TYPE_SMS;
            break;
        case RRC_DATA_TYPE_VOICE:
            new_frame.data_type = (app_msg->priority == -1) ? 
                                 DATA_TYPE_ANALOG_VOICE : DATA_TYPE_DIGITAL_VOICE;
            break;
        case RRC_DATA_TYPE_VIDEO:
            new_frame.data_type = DATA_TYPE_VIDEO_STREAM;
            break;
        case RRC_DATA_TYPE_FILE:
            new_frame.data_type = DATA_TYPE_FILE_TRANSFER;
            break;
        default:
            new_frame.data_type = DATA_TYPE_SMS;
            break;
    }
    
    // Copy payload
    if (app_msg->data && app_msg->data_size > 0) {
        size_t copy_size = (app_msg->data_size > PAYLOAD_SIZE_BYTES) ? 
                          PAYLOAD_SIZE_BYTES : app_msg->data_size;
        memcpy(new_frame.payload, app_msg->data, copy_size);
        new_frame.payload_length_bytes = copy_size;
    }
    
    new_frame.checksum = calculate_checksum(new_frame.payload, new_frame.payload_length_bytes);
    
    // Queue based on RRC priority
    if (app_msg->priority == PRIORITY_ANALOG_VOICE_PTT) {
        enqueue(analog_voice_queue, new_frame);
        printf("  → Queued to analog_voice_queue (PTT/Emergency)\n");
    } else if (app_msg->data_type == RRC_DATA_TYPE_VOICE && app_msg->priority == PRIORITY_DIGITAL_VOICE) {
        enqueue(&data_queues[0], new_frame);
        printf("  → Queued to data_queues[0] (Digital Voice)\n");
    } else if (app_msg->priority >= 0 && app_msg->priority < NUM_PRIORITY) {
        enqueue(&data_queues[app_msg->priority], new_frame);
        printf("  → Queued to data_queues[%d] (Priority %d)\n", app_msg->priority, app_msg->priority);
    } else {
        enqueue(rx_queue, new_frame);
        printf("  → Queued to rx_queue (Relay)\n");
    }
}

// ============================================================================
// TDMA TRANSMISSION FUNCTION
// ============================================================================

void tx(struct queue *analog_voice_queue, struct queue data_queues[], struct queue *rx_queue) {
    struct frame tx_frame;
    
    // 1. Check Analog Voice Queue (Highest Priority - includes PTT)
    if(!is_empty(analog_voice_queue)) {
        tx_frame = dequeue(analog_voice_queue);
        printf("[TDMA TX] Transmitted from Analog Voice Queue (PTT/Emergency)\n");
        return; 
    }

    // 2. Check Data Queues (Priority 0 → 1 → 2 → 3)
    for(int i = 0; i < NUM_PRIORITY; i++) {
        if(!is_empty(&data_queues[i])) {
            tx_frame = dequeue(&data_queues[i]);
            printf("[TDMA TX] Transmitted from Data Queue (Priority %d)\n", i);
            return; 
        }
    }

    // 3. Check RX Queue (Relay Queue - Lowest Priority)
    if(!is_empty(rx_queue)) {
        tx_frame = dequeue(rx_queue);
        printf("[TDMA TX] Transmitted from RX Relay Queue\n");
        return; 
    }
    
    printf("[TDMA TX] No data available for transmission\n");
}

// ============================================================================
// COMPLETE FLOW DEMONSTRATION
// ============================================================================

int main() {
    printf("==========================================\n");
    printf("COMPLETE JSON FLOW: L7 → RRC → TDMA\n");
    printf("==========================================\n\n");
    
    // Initialize TDMA queues
    struct queue analog_voice_queue = {{0}, -1, -1};
    struct queue data_queues[NUM_PRIORITY];
    struct queue rx_queue = {{0}, -1, -1};
    
    for(int i = 0; i < NUM_PRIORITY; i++) {
        data_queues[i].front = -1;
        data_queues[i].back = -1;
    }
    
    // Application Layer JSON messages (L7 → RRC)
    const char *json_messages[] = {
        "{\"node_id\":254, \"dest_node_id\":255, \"data_type\":\"ptt\", \"transmission_type\":\"broadcast\", \"data\":\"Emergency\", \"data_size\":9, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":1, \"data_type\":\"sms\", \"transmission_type\":\"unicast\", \"data\":\"Hello\", \"data_size\":5, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":2, \"data_type\":\"voice_digital\", \"transmission_type\":\"unicast\", \"data\":\"VoiceData\", \"data_size\":9, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":3, \"data_type\":\"video\", \"transmission_type\":\"unicast\", \"data\":\"VideoStream\", \"data_size\":11, \"TTL\":10}",
        "{\"node_id\":254, \"dest_node_id\":4, \"data_type\":\"file\", \"transmission_type\":\"unicast\", \"data\":\"FileData\", \"data_size\":8, \"TTL\":10}"
    };
    
    int num_messages = sizeof(json_messages) / sizeof(json_messages[0]);
    
    printf("STEP 1: APPLICATION LAYER (L7) SENDS JSON TO RRC\n");
    printf("================================================\n");
    
    for (int i = 0; i < num_messages; i++) {
        printf("\n[L7→RRC] Message %d:\n", i + 1);
        printf("JSON: %s\n", json_messages[i]);
        
        printf("\nSTEP 2: RRC PARSES JSON (using YOUR parse_json_message function)\n");
        printf("=================================================================\n");
        
        // RRC parses JSON using YOUR existing function
        ApplicationMessage *parsed_msg = parse_json_message(json_messages[i]);
        
        if (parsed_msg) {
            printf("[RRC] ✅ JSON parsed successfully:\n");
            printf("  Node: %u → %u\n", parsed_msg->node_id, parsed_msg->dest_node_id);
            printf("  Type: %d, Priority: %d\n", parsed_msg->data_type, parsed_msg->priority);
            printf("  Data: \"%s\" (%zu bytes)\n", (char*)parsed_msg->data, parsed_msg->data_size);
            
            printf("\nSTEP 3: RRC SENDS PARSED DATA TO TDMA (Clean Interface)\n");
            printf("=======================================================\n");
            
            // RRC sends already-parsed data to TDMA (NO JSON parsing in TDMA!)
            rrc_to_tdma_interface(parsed_msg, &analog_voice_queue, data_queues, &rx_queue);
            
            free_message(parsed_msg);
        } else {
            printf("[RRC] ❌ Failed to parse JSON\n");
        }
        
        printf("\n" + (i < num_messages - 1 ? 1 : 0)); // Add separator except for last
    }
    
    printf("\nSTEP 4: TDMA TRANSMISSION (Priority Order)\n");
    printf("==========================================\n");
    
    for(int cycle = 1; cycle <= 6; cycle++) {
        printf("\nTX Cycle %d: ", cycle);
        tx(&analog_voice_queue, data_queues, &rx_queue);
    }
    
    printf("\n\n==========================================\n");
    printf("✅ COMPLETE FLOW DEMONSTRATION FINISHED\n");
    printf("==========================================\n\n");
    
    printf("FLOW SUMMARY:\n");
    printf("=============\n");
    printf("1. L7 Application → sends JSON → RRC\n");
    printf("2. RRC → parses JSON using YOUR functions → ApplicationMessage\n");
    printf("3. RRC → sends parsed data → TDMA (clean interface)\n");
    printf("4. TDMA → queues by priority → transmits\n\n");
    
    printf("KEY BENEFITS:\n");
    printf("=============\n");
    printf("• No JSON parsing duplication\n");
    printf("• Your RRC code remains unchanged\n");
    printf("• Clean separation of concerns\n");
    printf("• Easy to maintain and debug\n");
    printf("• Proper priority handling preserved\n\n");
    
    printf("INTEGRATION POINTS:\n");
    printf("===================\n");
    printf("• RRC uses YOUR existing parse_json_message()\n");
    printf("• TDMA receives already-parsed data via interface\n");
    printf("• No function duplication or overwriting\n");
    printf("• Both layers work independently\n\n");
    
    return 0;
}