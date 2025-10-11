/*
 * JSON Data Extractor for ZCU104 Teams
 * Extracts JSON information from RRC dup.c for OLSR and TDMA teams
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Extract the exact data structures from dup.c for JSON generation

// From dup.c - Priority mapping
typedef enum {
    PRIORITY_ANALOG_VOICE_PTT = -1,
    PRIORITY_DIGITAL_VOICE = 0,
    PRIORITY_VIDEO = 1,
    PRIORITY_FILE = 2,
    PRIORITY_SMS = 3,
    PRIORITY_RX_RELAY = 4
} MessagePriority;

// From dup.c - Data types
typedef enum {
    RRC_DATA_TYPE_SMS = 0,
    RRC_DATA_TYPE_VOICE = 1,
    RRC_DATA_TYPE_VIDEO = 2,
    RRC_DATA_TYPE_FILE = 3,
    RRC_DATA_TYPE_RELAY = 4
} RRC_DataType;

// From dup.c - Transmission types
typedef enum {
    TRANSMISSION_UNICAST = 0,
    TRANSMISSION_MULTICAST = 1,
    TRANSMISSION_BROADCAST = 2
} TransmissionType;

/**
 * Generate OLSR JSON from RRC data
 */
void generate_olsr_json(uint8_t source, uint8_t dest, float rssi, float snr, float per) {
    printf("// OLSR JSON - Copy this to your OLSR implementation\n");
    printf("{\n");
    printf("  \"type\": \"route_request\",\n");
    printf("  \"source_node\": %u,\n", source);
    printf("  \"dest_node\": %u,\n", dest);
    printf("  \"timestamp\": %u,\n", (uint32_t)time(NULL));
    printf("  \"link_metrics\": {\n");
    printf("    \"rssi_dbm\": %.2f,\n", rssi);
    printf("    \"snr_db\": %.2f,\n", snr);
    printf("    \"per_percent\": %.2f\n", per);
    printf("  },\n");
    printf("  \"request_id\": %u\n", rand());
    printf("}\n\n");
}

/**
 * Generate TDMA JSON from RRC data
 */
void generate_tdma_json(int priority, uint8_t source, uint8_t dest, uint16_t size, int trans_type) {
    const char* priority_desc;
    const char* type_str;
    
    switch (priority) {
        case -1: priority_desc = "analog_voice_ptt"; break;
        case 0: priority_desc = "digital_voice"; break;
        case 1: priority_desc = "video_stream"; break;
        case 2: priority_desc = "file_transfer"; break;
        case 3: priority_desc = "sms"; break;
        default: priority_desc = "relay"; break;
    }
    
    switch (trans_type) {
        case 0: type_str = "unicast"; break;
        case 1: type_str = "multicast"; break;
        case 2: type_str = "broadcast"; break;
        default: type_str = "unknown"; break;
    }
    
    printf("// TDMA JSON - Copy this to your queue[1].c implementation\n");
    printf("{\n");
    printf("  \"type\": \"slot_request\",\n");
    printf("  \"priority\": %d,\n", priority);
    printf("  \"priority_desc\": \"%s\",\n", priority_desc);
    printf("  \"source_node\": %u,\n", source);
    printf("  \"dest_node\": %u,\n", dest);
    printf("  \"payload_size\": %u,\n", size);
    printf("  \"transmission_type\": \"%s\",\n", type_str);
    printf("  \"preemption_allowed\": %s,\n", (priority == -1) ? "true" : "false");
    printf("  \"timestamp\": %u,\n", (uint32_t)time(NULL));
    printf("  \"request_id\": %u\n", rand());
    printf("}\n\n");
}

/**
 * Show queue mapping for TDMA team
 */
void show_tdma_queue_mapping() {
    printf("// TDMA Queue Mapping - For queue[1].c integration\n");
    printf("/*\n");
    printf("Priority Level → Target Queue in queue[1].c:\n");
    printf("  -1 → analog_voice_queue (immediate preemption)\n");
    printf("   0 → data_queues[0] (digital voice)\n");
    printf("   1 → data_queues[1] (video stream)\n");
    printf("   2 → data_queues[2] (file transfer)\n");
    printf("   3 → data_queues[3] (SMS)\n");
    printf("   4 → rx_relay_queue (lowest priority)\n");
    printf("*/\n\n");
}

/**
 * Show OLSR thresholds for triggering route updates
 */
void show_olsr_thresholds() {
    printf("// OLSR Thresholds - When RRC triggers route requests\n");
    printf("/*\n");
    printf("Route Update Triggers:\n");
    printf("  - RSSI change > 5.0 dB\n");
    printf("  - SNR change > 3.0 dB\n");
    printf("  - PER change > 5.0%%\n");
    printf("  - No route update for 30 seconds\n");
    printf("  - Link becomes inactive (RSSI < -85 dBm, SNR < 10 dB, PER > 10%%)\n");
    printf("*/\n\n");
}

int main(void) {
    printf("=== JSON Data Extraction for ZCU104 Teams ===\n\n");
    
    // Sample data from your RRC implementation
    printf("1. OLSR TEAM - JSON Format Examples:\n");
    printf("=====================================\n\n");
    
    show_olsr_thresholds();
    generate_olsr_json(254, 1, -75.5f, 12.3f, 3.2f);
    generate_olsr_json(254, 2, -82.1f, 10.8f, 6.7f);
    
    printf("2. TDMA TEAM - JSON Format Examples:\n");
    printf("====================================\n\n");
    
    show_tdma_queue_mapping();
    generate_tdma_json(PRIORITY_ANALOG_VOICE_PTT, 254, 1, 16, TRANSMISSION_BROADCAST);
    generate_tdma_json(PRIORITY_DIGITAL_VOICE, 254, 2, 16, TRANSMISSION_UNICAST);
    generate_tdma_json(PRIORITY_VIDEO, 254, 3, 16, TRANSMISSION_UNICAST);
    generate_tdma_json(PRIORITY_SMS, 254, 4, 5, TRANSMISSION_UNICAST);
    
    printf("3. ZCU104 Integration Notes:\n");
    printf("============================\n");
    printf("/*\n");
    printf("Platform: ZCU104 Zynq UltraScale+\n");
    printf("Cores: A53 (Application) + R5 (Real-time)\n");
    printf("Memory: DDR4 for buffers, OCM for fast IPC\n");
    printf("Payload Limit: 16 bytes (embedded constraint)\n");
    printf("Node Addressing: 1 byte (0-255)\n");
    printf("Real-time Constraint: PTT preemption within 10μs\n");
    printf("*/\n\n");
    
    printf("4. For OLSR Team:\n");
    printf("================\n");
    printf("- Monitor link_metrics in JSON for route quality\n");
    printf("- Respond with route_response JSON containing next_hop\n");
    printf("- Use shared memory or message passing for A53/R5 communication\n\n");
    
    printf("5. For TDMA Team:\n");
    printf("================\n");
    printf("- Integrate slot_request JSON with queue[1].c enqueue() function\n");
    printf("- Map priority levels to appropriate queue structures\n");
    printf("- Handle preemption_allowed=true for PTT emergency traffic\n\n");
    
    return 0;
}