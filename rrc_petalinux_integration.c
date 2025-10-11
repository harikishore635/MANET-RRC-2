/*
 * RRC Integration with PetaLinux PHY Layer
 * Complete example showing PHY→RRC→JSON→OLSR flow
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>

// Include the PHY metrics extraction functions
// (In real implementation, include link_quality_guide.c functions)

// ============================================================================
// RRC Configuration for PetaLinux Integration
// ============================================================================

#define MAX_NEIGHBORS 16
#define LINK_UPDATE_INTERVAL_MS 1000  // 1 second
#define JSON_OUTPUT_DIR "/tmp/rrc_phy_metrics"
#define OLSR_PIPE_PATH "/tmp/olsr_phy_input"

typedef struct {
    uint8_t node_id;
    float rssi_dbm;
    float snr_db;
    float per_percent;
    float link_quality_score;
    uint32_t last_update;
    int active;
} neighbor_info_t;

typedef struct {
    neighbor_info_t neighbors[MAX_NEIGHBORS];
    int num_neighbors;
    pthread_t monitor_thread;
    int monitoring_active;
    FILE *olsr_pipe;
} rrc_phy_manager_t;

static rrc_phy_manager_t phy_manager = {0};

// ============================================================================
// PetaLinux PHY Integration Functions
// ============================================================================

/**
 * @brief Initialize RRC PHY monitoring system
 */
int rrc_phy_init(void) {
    printf("RRC PHY: Initializing PetaLinux integration...\n");
    
    // Create JSON output directory
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", JSON_OUTPUT_DIR);
    system(cmd);
    
    // Try to open OLSR named pipe
    phy_manager.olsr_pipe = fopen(OLSR_PIPE_PATH, "w");
    if (!phy_manager.olsr_pipe) {
        printf("RRC PHY: Warning - OLSR pipe not available, using files only\n");
    } else {
        printf("RRC PHY: Connected to OLSR via named pipe\n");
    }
    
    phy_manager.monitoring_active = 1;
    printf("RRC PHY: Initialization complete\n");
    return 0;
}

/**
 * @brief Add neighbor node for monitoring
 */
int rrc_phy_add_neighbor(uint8_t node_id) {
    if (phy_manager.num_neighbors >= MAX_NEIGHBORS) {
        printf("RRC PHY: Error - Maximum neighbors reached\n");
        return -1;
    }
    
    neighbor_info_t *neighbor = &phy_manager.neighbors[phy_manager.num_neighbors];
    neighbor->node_id = node_id;
    neighbor->active = 1;
    neighbor->last_update = 0;
    
    phy_manager.num_neighbors++;
    printf("RRC PHY: Added neighbor node %u (total: %d)\n", 
           node_id, phy_manager.num_neighbors);
    
    return 0;
}

/**
 * @brief Get PHY metrics using PetaLinux methods
 */
int get_phy_metrics_for_node(uint8_t node_id, neighbor_info_t *neighbor) {
    // Simulate PetaLinux IIO/DMA access
    // In real implementation, call the functions from link_quality_guide.c
    
    // Example: Get RSSI from IIO
    neighbor->rssi_dbm = -65.0f + (rand() % 20) - 10; // -75 to -55 dBm
    
    // Example: Get SNR from DMA I/Q processing
    neighbor->snr_db = 15.0f + (rand() % 10) - 5; // 10 to 20 dB
    
    // Example: Get PER from network interface stats
    neighbor->per_percent = 2.0f + (rand() % 8); // 2 to 10%
    
    // Calculate link quality score
    float rssi_score = (neighbor->rssi_dbm + 100.0f) / 50.0f; // -100 to -50 → 0 to 1
    float snr_score = neighbor->snr_db / 30.0f; // 0 to 30 → 0 to 1
    float per_score = 1.0f - (neighbor->per_percent / 20.0f); // 0-20% → 1 to 0
    
    neighbor->link_quality_score = (rssi_score * 0.3f + snr_score * 0.4f + per_score * 0.3f);
    if (neighbor->link_quality_score > 1.0f) neighbor->link_quality_score = 1.0f;
    if (neighbor->link_quality_score < 0.0f) neighbor->link_quality_score = 0.0f;
    
    neighbor->last_update = (uint32_t)time(NULL);
    
    printf("RRC PHY: Node %u - RSSI: %.1f dBm, SNR: %.1f dB, PER: %.1f%%, Quality: %.2f\n",
           node_id, neighbor->rssi_dbm, neighbor->snr_db, 
           neighbor->per_percent, neighbor->link_quality_score);
    
    return 0;
}

/**
 * @brief Generate JSON for OLSR route update
 */
char* generate_olsr_json(neighbor_info_t *neighbor) {
    static char json_buffer[1024];
    
    // Determine routing action based on link quality
    const char *route_action;
    int route_priority;
    float link_cost;
    
    if (neighbor->link_quality_score > 0.8f) {
        route_action = "prefer_route";
        route_priority = 1;
        link_cost = 1.0f / neighbor->link_quality_score;
    } else if (neighbor->link_quality_score > 0.5f) {
        route_action = "maintain_route";
        route_priority = 3;
        link_cost = 2.0f / neighbor->link_quality_score;
    } else if (neighbor->link_quality_score > 0.2f) {
        route_action = "backup_route";
        route_priority = 7;
        link_cost = 5.0f / neighbor->link_quality_score;
    } else {
        route_action = "avoid_route";
        route_priority = 9;
        link_cost = 50.0f;
    }
    
    snprintf(json_buffer, sizeof(json_buffer),
        "{\n"
        "  \"message_type\": \"phy_link_update\",\n"
        "  \"timestamp\": %u,\n"
        "  \"source_node\": 254,\n"
        "  \"target_node\": %u,\n"
        "  \"phy_metrics\": {\n"
        "    \"rssi_dbm\": %.1f,\n"
        "    \"snr_db\": %.1f,\n"
        "    \"per_percent\": %.2f,\n"
        "    \"link_quality\": %.3f\n"
        "  },\n"
        "  \"routing_recommendation\": {\n"
        "    \"action\": \"%s\",\n"
        "    \"priority\": %d,\n"
        "    \"link_cost\": %.2f,\n"
        "    \"metric_type\": \"ETX_PHY_ENHANCED\"\n"
        "  },\n"
        "  \"rrc_info\": {\n"
        "    \"update_reason\": \"periodic_monitoring\",\n"
        "    \"confidence\": %.2f,\n"
        "    \"measurement_method\": \"petalinux_iio_dma\"\n"
        "  }\n"
        "}",
        neighbor->last_update,
        neighbor->node_id,
        neighbor->rssi_dbm,
        neighbor->snr_db,
        neighbor->per_percent,
        neighbor->link_quality_score,
        route_action,
        route_priority,
        link_cost,
        neighbor->link_quality_score * 0.9f // Confidence based on quality
    );
    
    return json_buffer;
}

/**
 * @brief Send PHY metrics to OLSR
 */
int send_to_olsr(neighbor_info_t *neighbor) {
    char *json_data = generate_olsr_json(neighbor);
    char filename[128];
    FILE *json_file;
    
    // Write to file for OLSR to read
    snprintf(filename, sizeof(filename), 
             "%s/phy_metrics_node_%u.json", JSON_OUTPUT_DIR, neighbor->node_id);
    
    json_file = fopen(filename, "w");
    if (json_file) {
        fprintf(json_file, "%s\n", json_data);
        fclose(json_file);
        printf("RRC PHY: JSON written to %s\n", filename);
    }
    
    // Also send via named pipe if available
    if (phy_manager.olsr_pipe) {
        fprintf(phy_manager.olsr_pipe, "%s\n", json_data);
        fflush(phy_manager.olsr_pipe);
        printf("RRC PHY: JSON sent to OLSR via pipe\n");
    }
    
    return 0;
}

/**
 * @brief Monitoring thread function
 */
void* phy_monitoring_thread(void *arg) {
    printf("RRC PHY: Monitoring thread started\n");
    
    while (phy_manager.monitoring_active) {
        printf("\n=== RRC PHY Monitoring Cycle ===\n");
        
        for (int i = 0; i < phy_manager.num_neighbors; i++) {
            neighbor_info_t *neighbor = &phy_manager.neighbors[i];
            
            if (!neighbor->active) continue;
            
            // Get fresh PHY measurements
            if (get_phy_metrics_for_node(neighbor->node_id, neighbor) == 0) {
                // Send to OLSR for route updates
                send_to_olsr(neighbor);
            }
        }
        
        printf("RRC PHY: Monitoring cycle complete, sleeping %d ms\n", 
               LINK_UPDATE_INTERVAL_MS);
        usleep(LINK_UPDATE_INTERVAL_MS * 1000);
    }
    
    printf("RRC PHY: Monitoring thread stopped\n");
    return NULL;
}

/**
 * @brief Start periodic PHY monitoring
 */
int rrc_phy_start_monitoring(void) {
    if (pthread_create(&phy_manager.monitor_thread, NULL, 
                       phy_monitoring_thread, NULL) != 0) {
        printf("RRC PHY: Error - Failed to create monitoring thread\n");
        return -1;
    }
    
    printf("RRC PHY: Periodic monitoring started\n");
    return 0;
}

/**
 * @brief Stop PHY monitoring
 */
void rrc_phy_stop_monitoring(void) {
    phy_manager.monitoring_active = 0;
    
    if (phy_manager.monitor_thread) {
        pthread_join(phy_manager.monitor_thread, NULL);
    }
    
    if (phy_manager.olsr_pipe) {
        fclose(phy_manager.olsr_pipe);
        phy_manager.olsr_pipe = NULL;
    }
    
    printf("RRC PHY: Monitoring stopped\n");
}

/**
 * @brief Signal handler for clean shutdown
 */
void signal_handler(int signum) {
    printf("\nRRC PHY: Received signal %d, shutting down...\n", signum);
    rrc_phy_stop_monitoring();
    exit(0);
}

// ============================================================================
// Main Integration Example
// ============================================================================

int main(void) {
    printf("=== RRC PetaLinux PHY Integration Demo ===\n\n");
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize RRC PHY system
    if (rrc_phy_init() != 0) {
        printf("Error: Failed to initialize RRC PHY system\n");
        return -1;
    }
    
    // Add neighbor nodes to monitor
    printf("Adding neighbor nodes...\n");
    rrc_phy_add_neighbor(1);  // Node 1
    rrc_phy_add_neighbor(2);  // Node 2
    rrc_phy_add_neighbor(3);  // Node 3
    rrc_phy_add_neighbor(4);  // Node 4
    
    printf("\n🚀 INTEGRATION ARCHITECTURE:\n");
    printf("   PHY Layer (PetaLinux) → RRC → JSON → OLSR\n");
    printf("   ├─ RSSI from IIO: /sys/bus/iio/devices/iio:device0/\n");
    printf("   ├─ SNR from DMA: /dev/axis_dma_rx\n");
    printf("   ├─ PER from NetIF: /sys/class/net/eth0/statistics/\n");
    printf("   ├─ JSON Output: %s/\n", JSON_OUTPUT_DIR);
    printf("   └─ OLSR Pipe: %s\n", OLSR_PIPE_PATH);
    
    // Start monitoring
    printf("\nStarting PHY monitoring (Ctrl+C to stop)...\n\n");
    if (rrc_phy_start_monitoring() != 0) {
        printf("Error: Failed to start monitoring\n");
        return -1;
    }
    
    // Keep main thread alive
    while (1) {
        sleep(5);
        printf("RRC PHY: Main thread heartbeat - %d neighbors active\n", 
               phy_manager.num_neighbors);
    }
    
    return 0;
}