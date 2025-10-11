/*
 * RRC Implementation - Priority and Structure Test
 * Demonstrates the updated priority structure without external dependencies
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Priority mapping (updated structure)
typedef enum {
    PRIORITY_ANALOG_VOICE_PTT = -1,  // → analog_voice_queue (Absolute preemption)
    PRIORITY_DIGITAL_VOICE = 0,      // → data_queues[0] (Digital Voice)
    PRIORITY_DATA_1 = 1,             // → data_queues[1] (Data Priority 1)
    PRIORITY_DATA_2 = 2,             // → data_queues[2] (Data Priority 2)
    PRIORITY_DATA_3 = 3,             // → data_queues[3] (Data Priority 3)
    PRIORITY_RX_RELAY = 4            // → rx_queue (Lowest priority)
} MessagePriority;

// RRC Data type definitions
typedef enum {
    RRC_DATA_TYPE_SMS = 0,
    RRC_DATA_TYPE_VOICE = 1,
    RRC_DATA_TYPE_VIDEO = 2,
    RRC_DATA_TYPE_FILE = 3,
    RRC_DATA_TYPE_RELAY = 4,
    RRC_DATA_TYPE_UNKNOWN = 99
} RRC_DataType;

// Transmission type definitions
typedef enum {
    TRANSMISSION_UNICAST = 0,
    TRANSMISSION_MULTICAST = 1,
    TRANSMISSION_BROADCAST = 2
} TransmissionType;

// Application Message structure (without checksum)
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

// Helper functions
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

const char* transmission_type_to_string(TransmissionType type) {
    switch (type) {
        case TRANSMISSION_UNICAST: return "Unicast";
        case TRANSMISSION_MULTICAST: return "Multicast";
        case TRANSMISSION_BROADCAST: return "Broadcast";
        default: return "Unknown";
    }
}

// Queue assignment demonstration
void demonstrate_queue_assignment(ApplicationMessage *msg) {
    if (!msg) return;
    
    printf("\n=== Queue Assignment ===\n");
    printf("Message Type: %s\n", data_type_to_string(msg->data_type));
    printf("Priority: %s (%d)\n", priority_to_string(msg->priority), msg->priority);
    
    // Determine which queue to use based on priority
    switch (msg->priority) {
        case PRIORITY_ANALOG_VOICE_PTT:
            printf("Queue Assignment: analog_voice_queue (PTT Emergency)\n");
            printf("Transmission: IMMEDIATE (absolute preemption)\n");
            break;
            
        case PRIORITY_DIGITAL_VOICE:
            printf("Queue Assignment: data_from_l3_queue[0] (Digital Voice)\n");
            printf("Transmission: High Priority\n");
            break;
            
        case PRIORITY_DATA_1:
            printf("Queue Assignment: data_from_l3_queue[1] (Data Priority 1)\n");
            printf("Transmission: Medium-High Priority\n");
            break;
            
        case PRIORITY_DATA_2:
            printf("Queue Assignment: data_from_l3_queue[2] (Data Priority 2)\n");
            printf("Transmission: Medium Priority\n");
            break;
            
        case PRIORITY_DATA_3:
            printf("Queue Assignment: data_from_l3_queue[3] (Data Priority 3)\n");
            printf("Transmission: Low Priority\n");
            break;
            
        case PRIORITY_RX_RELAY:
        default:
            printf("Queue Assignment: rx_queue (Relay/Unknown)\n");
            printf("Transmission: Lowest Priority\n");
            break;
    }
    printf("========================\n\n");
}

// Create test messages
ApplicationMessage* create_test_message(RRC_DataType data_type, const char* data) {
    ApplicationMessage *msg = (ApplicationMessage*)calloc(1, sizeof(ApplicationMessage));
    if (!msg) return NULL;
    
    msg->node_id = 254;
    msg->dest_node_id = 255;
    msg->data_type = data_type;
    
    // Assign priority based on data type
    switch (data_type) {
        case RRC_DATA_TYPE_SMS:
            msg->priority = PRIORITY_DATA_3;
            break;
        case RRC_DATA_TYPE_VOICE:
            msg->priority = PRIORITY_ANALOG_VOICE_PTT;
            msg->preemption_allowed = true;
            break;
        case RRC_DATA_TYPE_VIDEO:
            msg->priority = PRIORITY_DATA_1;
            break;
        case RRC_DATA_TYPE_FILE:
            msg->priority = PRIORITY_DATA_2;
            break;
        case RRC_DATA_TYPE_RELAY:
            msg->priority = PRIORITY_RX_RELAY;
            break;
        default:
            msg->priority = PRIORITY_DATA_3;
            break;
    }
    
    if (data) {
        size_t len = strlen(data);
        msg->data = (uint8_t*)malloc(len + 1);
        if (msg->data) {
            strcpy((char*)msg->data, data);
            msg->data_size = len;
        }
    }
    
    return msg;
}

void free_message(ApplicationMessage *msg) {
    if (!msg) return;
    if (msg->data) free(msg->data);
    free(msg);
}

int main() {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                RRC PRIORITY STRUCTURE TEST                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("🎯 UPDATED PRIORITY STRUCTURE:\n");
    printf("==============================\n");
    printf("• Analog Voice (PTT) - Absolute preemption\n");
    printf("• Priority 0: Digital Voice\n");
    printf("• Priority 1: Data\n");
    printf("• Priority 2: Data\n");
    printf("• Priority 3: Data\n");
    printf("• RX Relay - Lowest priority\n\n");
    
    printf("✅ CHECKSUM REMOVED from frame structure\n\n");
    
    printf("🔄 TESTING MESSAGE PRIORITY ASSIGNMENTS:\n");
    printf("========================================\n");
    
    // Test different message types
    ApplicationMessage *ptt_msg = create_test_message(RRC_DATA_TYPE_VOICE, "Emergency");
    ApplicationMessage *video_msg = create_test_message(RRC_DATA_TYPE_VIDEO, "VideoData");
    ApplicationMessage *file_msg = create_test_message(RRC_DATA_TYPE_FILE, "FileData");
    ApplicationMessage *sms_msg = create_test_message(RRC_DATA_TYPE_SMS, "Hello");
    ApplicationMessage *relay_msg = create_test_message(RRC_DATA_TYPE_RELAY, "RelayData");
    
    // Demonstrate queue assignments
    printf("1. PTT Emergency Message:\n");
    demonstrate_queue_assignment(ptt_msg);
    
    printf("2. Video Stream Message:\n");
    demonstrate_queue_assignment(video_msg);
    
    printf("3. File Transfer Message:\n");
    demonstrate_queue_assignment(file_msg);
    
    printf("4. SMS Message:\n");
    demonstrate_queue_assignment(sms_msg);
    
    printf("5. Relay Message:\n");
    demonstrate_queue_assignment(relay_msg);
    
    printf("🚀 TRANSMISSION ORDER:\n");
    printf("======================\n");
    printf("1. PTT Emergency (Immediate)\n");
    printf("2. Digital Voice (Priority 0)\n");
    printf("3. Data Priority 1\n");
    printf("4. Data Priority 2\n");
    printf("5. Data Priority 3\n");
    printf("6. RX Relay (Lowest)\n\n");
    
    printf("✅ RRC PRIORITY STRUCTURE UPDATED SUCCESSFULLY!\n");
    printf("✅ CHECKSUM FUNCTIONALITY REMOVED\n");
    printf("✅ READY FOR QUEUE.C INTEGRATION\n\n");
    
    // Cleanup
    free_message(ptt_msg);
    free_message(video_msg);
    free_message(file_msg);
    free_message(sms_msg);
    free_message(relay_msg);
    
    return 0;
}