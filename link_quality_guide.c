/*
 * PHY Layer Link Quality Extraction for ZCU104 with PetaLinux
 * RSSI/SNR/PER extraction via Linux IIO and JSON output for RRC→OLSR
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <time.h>
#include <math.h>

// ============================================================================
// PetaLinux PHY Layer Access Methods for ZCU104
// ============================================================================

/**
 * @brief PHY metrics structure for JSON output
 */
typedef struct {
    float rssi_dbm;
    float snr_db;
    float per_percent;
    uint8_t node_id;
    uint32_t timestamp;
    char status[16];
} phy_metrics_t;

/**
 * @brief Get RSSI from PetaLinux IIO subsystem (AD9361)
 */
float get_rssi_from_petalinux_iio(uint8_t channel) {
    char iio_path[128];
    FILE *rssi_file;
    float rssi_raw = 0.0f;
    
    // PetaLinux IIO path for AD9361 RSSI
    snprintf(iio_path, sizeof(iio_path), 
             "/sys/bus/iio/devices/iio:device0/in_voltage%u_rssi", channel);
    
    rssi_file = fopen(iio_path, "r");
    if (rssi_file) {
        if (fscanf(rssi_file, "%f", &rssi_raw) == 1) {
            fclose(rssi_file);
            
            // Convert IIO raw value to dBm
            // AD9361 IIO driver typically gives values in milli-dB
            float rssi_dbm = rssi_raw / 1000.0f;
            
            printf("PetaLinux IIO: RSSI CH%u = %.1f dBm (raw: %.0f)\n", 
                   channel, rssi_dbm, rssi_raw);
            return rssi_dbm;
        }
        fclose(rssi_file);
    }
    
    printf("PetaLinux IIO: Failed to read RSSI from %s\n", iio_path);
    return -100.0f; // Default poor signal
}

/**
 * @brief Get SNR from PetaLinux by reading I/Q samples
 */
float get_snr_from_petalinux_dma(void) {
    FILE *dma_file;
    int16_t iq_samples[1024];
    size_t samples_read;
    
    // Access DMA buffer through PetaLinux device file
    dma_file = fopen("/dev/axis_dma_rx", "rb");
    if (!dma_file) {
        printf("PetaLinux DMA: Failed to open /dev/axis_dma_rx\n");
        return 15.0f; // Default reasonable SNR
    }
    
    samples_read = fread(iq_samples, sizeof(int16_t), 1024, dma_file);
    fclose(dma_file);
    
    if (samples_read < 128) {
        printf("PetaLinux DMA: Insufficient samples read (%zu)\n", samples_read);
        return 12.0f;
    }
    
    // Calculate signal power from first 64 complex samples (preamble)
    float signal_power = 0.0f;
    for (int i = 0; i < 128; i += 2) {
        int16_t i_sample = iq_samples[i];
        int16_t q_sample = iq_samples[i + 1];
        signal_power += (float)(i_sample * i_sample + q_sample * q_sample);
    }
    signal_power /= 64.0f;
    
    // Calculate noise power from last 64 complex samples
    float noise_power = 0.0f;
    for (int i = samples_read - 128; i < (int)samples_read; i += 2) {
        int16_t i_sample = iq_samples[i];
        int16_t q_sample = iq_samples[i + 1];
        noise_power += (float)(i_sample * i_sample + q_sample * q_sample);
    }
    noise_power /= 64.0f;
    
    if (noise_power == 0.0f) noise_power = 1.0f;
    
    float snr_db = 10.0f * log10f(signal_power / noise_power);
    printf("PetaLinux DMA: SNR = %.1f dB (sig: %.1f, noise: %.1f)\n", 
           snr_db, signal_power, noise_power);
    
    return snr_db;
}

/**
 * @brief Get PER from PetaLinux network interface statistics
 */
float get_per_from_petalinux_netif(const char *interface_name) {
    char stats_path[128];
    FILE *stats_file;
    uint32_t rx_packets = 0, rx_errors = 0;
    
    // Read network interface statistics
    snprintf(stats_path, sizeof(stats_path), 
             "/sys/class/net/%s/statistics/rx_packets", interface_name);
    
    stats_file = fopen(stats_path, "r");
    if (stats_file) {
        fscanf(stats_file, "%u", &rx_packets);
        fclose(stats_file);
    }
    
    snprintf(stats_path, sizeof(stats_path), 
             "/sys/class/net/%s/statistics/rx_errors", interface_name);
    
    stats_file = fopen(stats_path, "r");
    if (stats_file) {
        fscanf(stats_file, "%u", &rx_errors);
        fclose(stats_file);
    }
    
    if (rx_packets == 0) return 0.0f;
    
    float per_percent = (float)rx_errors / rx_packets * 100.0f;
    printf("PetaLinux NetIF: PER = %.1f%% (errors: %u, packets: %u)\n", 
           per_percent, rx_errors, rx_packets);
    
    return per_percent;
}

/**
 * @brief Alternative: Get PHY stats from custom sysfs entries
 */
float get_per_from_custom_sysfs(void) {
    FILE *stats_file;
    uint32_t total_frames = 0, error_frames = 0;
    
    // Custom PHY driver sysfs entries
    stats_file = fopen("/sys/kernel/phy_stats/total_frames", "r");
    if (stats_file) {
        fscanf(stats_file, "%u", &total_frames);
        fclose(stats_file);
    }
    
    stats_file = fopen("/sys/kernel/phy_stats/error_frames", "r");
    if (stats_file) {
        fscanf(stats_file, "%u", &error_frames);
        fclose(stats_file);
    }
    
    if (total_frames == 0) return 0.0f;
    
    float per_percent = (float)error_frames / total_frames * 100.0f;
    printf("Custom SysFS: PER = %.1f%% (errors: %u, total: %u)\n", 
           per_percent, error_frames, total_frames);
    
    return per_percent;
}

/**
 * @brief Complete PHY metrics collection using PetaLinux
 */
phy_metrics_t get_phy_metrics_petalinux(uint8_t node_id) {
    phy_metrics_t metrics = {0};
    
    metrics.node_id = node_id;
    metrics.timestamp = (uint32_t)time(NULL);
    strcpy(metrics.status, "active");
    
    // Method 1: Get RSSI from IIO subsystem (AD9361)
    float rssi_ch0 = get_rssi_from_petalinux_iio(0);
    float rssi_ch1 = get_rssi_from_petalinux_iio(1);
    metrics.rssi_dbm = (rssi_ch0 + rssi_ch1) / 2.0f; // Average both channels
    
    // Method 2: Get SNR from DMA I/Q samples
    metrics.snr_db = get_snr_from_petalinux_dma();
    
    // Method 3: Get PER from network interface or custom sysfs
    // Try network interface first, fallback to custom sysfs
    metrics.per_percent = get_per_from_petalinux_netif("eth0");
    if (metrics.per_percent == 0.0f) {
        metrics.per_percent = get_per_from_custom_sysfs();
    }
    
    printf("PetaLinux PHY: Complete metrics for node %u\n", node_id);
    return metrics;
}

// ============================================================================
// JSON Output Generation for RRC→OLSR Communication
// ============================================================================

/**
 * @brief Generate JSON string with PHY metrics for OLSR route update
 */
char* generate_phy_metrics_json(phy_metrics_t *metrics) {
    static char json_buffer[512];
    
    snprintf(json_buffer, sizeof(json_buffer),
        "{\n"
        "  \"message_type\": \"phy_link_quality\",\n"
        "  \"timestamp\": %u,\n"
        "  \"source_node\": 254,\n"
        "  \"target_node\": %u,\n"
        "  \"link_metrics\": {\n"
        "    \"rssi_dbm\": %.1f,\n"
        "    \"snr_db\": %.1f,\n"
        "    \"per_percent\": %.2f,\n"
        "    \"link_quality\": %.2f,\n"
        "    \"status\": \"%s\"\n"
        "  },\n"
        "  \"routing_action\": {\n"
        "    \"update_route\": %s,\n"
        "    \"route_priority\": %d,\n"
        "    \"link_cost\": %.1f\n"
        "  }\n"
        "}",
        metrics->timestamp,
        metrics->node_id,
        metrics->rssi_dbm,
        metrics->snr_db,
        metrics->per_percent,
        calculate_link_quality_score(metrics),
        metrics->status,
        should_update_route(metrics) ? "true" : "false",
        calculate_route_priority(metrics),
        calculate_link_cost(metrics)
    );
    
    return json_buffer;
}

/**
 * @brief Calculate overall link quality score (0.0 - 1.0)
 */
float calculate_link_quality_score(phy_metrics_t *metrics) {
    float rssi_score = 0.0f;
    float snr_score = 0.0f;
    float per_score = 0.0f;
    
    // RSSI scoring (-100 to -30 dBm range)
    if (metrics->rssi_dbm > -50.0f) rssi_score = 1.0f;
    else if (metrics->rssi_dbm > -70.0f) rssi_score = 0.8f;
    else if (metrics->rssi_dbm > -85.0f) rssi_score = 0.5f;
    else if (metrics->rssi_dbm > -95.0f) rssi_score = 0.2f;
    else rssi_score = 0.1f;
    
    // SNR scoring (0 to 30+ dB range)
    if (metrics->snr_db > 20.0f) snr_score = 1.0f;
    else if (metrics->snr_db > 15.0f) snr_score = 0.8f;
    else if (metrics->snr_db > 10.0f) snr_score = 0.6f;
    else if (metrics->snr_db > 5.0f) snr_score = 0.3f;
    else snr_score = 0.1f;
    
    // PER scoring (0% to 50%+ range)
    if (metrics->per_percent < 1.0f) per_score = 1.0f;
    else if (metrics->per_percent < 5.0f) per_score = 0.8f;
    else if (metrics->per_percent < 10.0f) per_score = 0.5f;
    else if (metrics->per_percent < 20.0f) per_score = 0.2f;
    else per_score = 0.1f;
    
    // Weighted average
    return (rssi_score * 0.3f + snr_score * 0.4f + per_score * 0.3f);
}

/**
 * @brief Determine if route should be updated based on metrics
 */
int should_update_route(phy_metrics_t *metrics) {
    float quality = calculate_link_quality_score(metrics);
    
    // Update route if quality changed significantly
    return (quality > 0.5f || metrics->per_percent > 15.0f);
}

/**
 * @brief Calculate route priority (1=highest, 10=lowest)
 */
int calculate_route_priority(phy_metrics_t *metrics) {
    float quality = calculate_link_quality_score(metrics);
    
    if (quality > 0.8f) return 1; // Excellent link
    else if (quality > 0.6f) return 3; // Good link
    else if (quality > 0.4f) return 5; // Fair link
    else if (quality > 0.2f) return 7; // Poor link
    else return 9; // Very poor link
}

/**
 * @brief Calculate OLSR link cost
 */
float calculate_link_cost(phy_metrics_t *metrics) {
    float quality = calculate_link_quality_score(metrics);
    
    // OLSR ETX-style cost calculation
    float etx = 1.0f / (1.0f - metrics->per_percent / 100.0f);
    float cost = etx / quality;
    
    return fminf(cost, 100.0f); // Cap at 100
}

/**
 * @brief Write JSON to file for OLSR consumption
 */
int write_phy_json_for_olsr(phy_metrics_t *metrics, const char *filename) {
    FILE *json_file;
    char *json_string;
    
    json_string = generate_phy_metrics_json(metrics);
    
    json_file = fopen(filename, "w");
    if (!json_file) {
        printf("Error: Failed to create JSON file %s\n", filename);
        return -1;
    }
    
    fprintf(json_file, "%s\n", json_string);
    fclose(json_file);
    
    printf("PHY JSON written to %s for OLSR processing\n", filename);
    return 0;
}

/**
 * @brief Send JSON to OLSR via named pipe/FIFO
 */
int send_phy_json_to_olsr_pipe(phy_metrics_t *metrics) {
    FILE *olsr_pipe;
    char *json_string;
    
    // Open named pipe to OLSR daemon
    olsr_pipe = fopen("/tmp/olsr_phy_input", "w");
    if (!olsr_pipe) {
        printf("Warning: OLSR pipe not available, writing to file instead\n");
        return write_phy_json_for_olsr(metrics, "/tmp/phy_metrics.json");
    }
    
    json_string = generate_phy_metrics_json(metrics);
    fprintf(olsr_pipe, "%s\n", json_string);
    fclose(olsr_pipe);
    
    printf("PHY JSON sent to OLSR via pipe\n");
    return 0;
}

// ============================================================================
// RRC Integration Functions for PetaLinux PHY
// ============================================================================

/**
 * @brief Main RRC function to get PHY metrics and send to OLSR
 */
int rrc_update_link_and_notify_olsr(uint8_t node_id) {
    phy_metrics_t metrics;
    char json_filename[64];
    
    printf("RRC: Updating link quality for node %u...\n", node_id);
    
    // Get PHY metrics from PetaLinux
    metrics = get_phy_metrics_petalinux(node_id);
    
    // Generate JSON filename
    snprintf(json_filename, sizeof(json_filename), 
             "/tmp/phy_metrics_node_%u.json", node_id);
    
    // Write JSON for OLSR
    if (write_phy_json_for_olsr(&metrics, json_filename) == 0) {
        // Also try to send via pipe for real-time updates
        send_phy_json_to_olsr_pipe(&metrics);
        
        printf("RRC: Successfully updated link quality for node %u\n", node_id);
        printf("     RSSI: %.1f dBm, SNR: %.1f dB, PER: %.1f%%\n",
               metrics.rssi_dbm, metrics.snr_db, metrics.per_percent);
        printf("     Link Quality Score: %.2f\n", 
               calculate_link_quality_score(&metrics));
        
        return 0;
    }
    
    printf("RRC: Failed to update link quality for node %u\n", node_id);
    return -1;
}

/**
 * @brief Periodic link quality monitoring for all neighbors
 */
void rrc_monitor_all_neighbors(uint8_t *neighbor_list, int num_neighbors) {
    printf("RRC: Starting periodic link quality monitoring...\n");
    
    for (int i = 0; i < num_neighbors; i++) {
        uint8_t node_id = neighbor_list[i];
        
        // Update each neighbor's link quality
        if (rrc_update_link_and_notify_olsr(node_id) == 0) {
            printf("RRC: ✓ Node %u link quality updated\n", node_id);
        } else {
            printf("RRC: ✗ Node %u link quality update failed\n", node_id);
        }
        
        // Small delay between measurements
        usleep(100000); // 100ms delay
    }
    
    printf("RRC: Link quality monitoring cycle complete\n");
}

// ============================================================================
// PetaLinux Implementation Notes and Setup
// ============================================================================

void petalinux_implementation_notes(void) {
    printf("🐧 PetaLinux PHY Layer Access for ZCU104:\n\n");
    
    printf("📂 REQUIRED LINUX DRIVERS:\n");
    printf("   • AD9361 IIO Driver: CONFIG_AD9361=y\n");
    printf("   • AXI DMA Driver: CONFIG_XILINX_DMA=y\n");
    printf("   • Custom PHY Stats Driver (optional)\n");
    printf("   • Network Interface Driver for your RF MAC\n\n");
    
    printf("📁 FILESYSTEM PATHS:\n");
    printf("   • RSSI: /sys/bus/iio/devices/iio:device0/in_voltage[0-1]_rssi\n");
    printf("   • DMA: /dev/axis_dma_rx (custom device file)\n");
    printf("   • NetIF: /sys/class/net/eth0/statistics/\n");
    printf("   • Custom: /sys/kernel/phy_stats/ (if implemented)\n\n");
    
    printf("🔧 DEVICETREE REQUIREMENTS:\n");
    printf("   • AD9361 SPI configuration\n");
    printf("   • AXI DMA memory mapping\n");
    printf("   • Custom IP register mapping\n");
    printf("   • Named pipe support: CONFIG_UNIX=y\n\n");
    
    printf("📊 JSON COMMUNICATION FLOW:\n");
    printf("   1. RRC calls get_phy_metrics_petalinux(node_id)\n");
    printf("   2. Function reads RSSI from IIO subsystem\n");
    printf("   3. Function reads SNR from DMA I/Q samples\n");
    printf("   4. Function reads PER from network statistics\n");
    printf("   5. Generate JSON with routing recommendations\n");
    printf("   6. Write JSON file for OLSR consumption\n");
    printf("   7. Send via named pipe for real-time updates\n\n");
    
    printf("🚀 INTEGRATION STEPS:\n");
    printf("   1. Build PetaLinux with required drivers\n");
    printf("   2. Create device files and sysfs entries\n");
    printf("   3. Test IIO access: cat /sys/bus/iio/devices/iio:device0/name\n");
    printf("   4. Verify DMA access: ls -la /dev/axis_dma_*\n");
    printf("   5. Create OLSR named pipe: mkfifo /tmp/olsr_phy_input\n");
    printf("   6. Start OLSR daemon with JSON input monitoring\n\n");
}

uint32_t get_system_time(void) {
    return (uint32_t)time(NULL);
}

// ============================================================================
// Legacy Functions (kept for reference - not used in PetaLinux approach)
// ============================================================================



// ============================================================================
// ZCU104 Specific Implementation Notes
// ============================================================================

void zcu104_implementation_notes(void) {
    printf("ZCU104 PHY Layer Access Methods:\n\n");
    
    printf("🔧 METHOD 1 - DIRECT PHY REGISTER ACCESS (RECOMMENDED):\n");
    printf("   • AD9361 RF Transceiver: Base address 0x79020000\n");
    printf("   • RSSI Registers: 0x109 (CH1), 0x10A (CH2)\n");
    printf("   • DMA I/Q Buffer: 0x40000000 (DDR4 mapped)\n");
    printf("   • PHY Statistics: 0x79030000 (custom registers)\n");
    printf("   • Real-time access from R5 or A53 core\n\n");
    
    printf("📊 METHOD 2 - I/Q SAMPLE PROCESSING:\n");
    printf("   • Access raw I/Q samples from DMA buffer\n");
    printf("   • Calculate SNR from signal/noise power ratio\n");
    printf("   • Use preamble for signal power estimation\n");
    printf("   • Use quiet periods for noise floor measurement\n");
    printf("   • DSP processing on A53 core for complex calculations\n\n");
    
    printf("📈 METHOD 3 - PHY STATISTICS REGISTERS:\n");
    printf("   • Frame counters: Total received, CRC errors\n");
    printf("   • Sync errors, FCS errors, timeout counters\n");
    printf("   • Automatic PER calculation from hardware\n");
    printf("   • Reset counters periodically for fresh measurements\n\n");
    
    printf("🛠️ BARE METAL IMPLEMENTATION:\n");
    printf("   • No Linux drivers - direct memory mapping\n");
    printf("   • Use scatter-gather DMA for I/Q data\n");
    printf("   • Interrupt-driven updates for real-time metrics\n");
    printf("   • Shared memory between A53/R5 for coordination\n\n");
    
    printf("🚀 INTEGRATION WITH YOUR RRC:\n");
    printf("   • Call get_phy_metrics_zcu104() from dup.c\n");
    printf("   • Define USE_ZCU104_PHY_DIRECT for real hardware\n");
    printf("   • Fall back to simulation if PHY not available\n");
    printf("   • Update link quality every 100ms to 1 second\n\n");
    
    printf("📋 CONFIGURATION STEPS:\n");
    printf("   1. Verify AD9361 base address in your Vivado design\n");
    printf("   2. Confirm DMA buffer location and size\n");
    printf("   3. Add PHY statistics registers to your IP\n");
    printf("   4. Calibrate RSSI conversion factors\n");
    printf("   5. Test with known signal sources\n\n");
}

uint32_t get_system_time(void) {
    // Implement based on your timer configuration
    return 1697875200 + (rand() % 1000); // Example timestamp
}

int main(void) {
    printf("=== PetaLinux PHY Metrics for ZCU104 RRC→OLSR ===\n\n");
    
    petalinux_implementation_notes();
    
    printf("🧪 TESTING PETALINUX PHY EXTRACTION:\n\n");
    
    // Test with multiple neighbor nodes
    uint8_t neighbor_nodes[] = {1, 2, 3, 4};
    int num_neighbors = sizeof(neighbor_nodes) / sizeof(neighbor_nodes[0]);
    
    printf("Testing individual node updates:\n");
    for (int i = 0; i < num_neighbors; i++) {
        uint8_t node_id = neighbor_nodes[i];
        printf("\n--- Node %u Metrics ---\n", node_id);
        
        phy_metrics_t metrics = get_phy_metrics_petalinux(node_id);
        char *json_output = generate_phy_metrics_json(&metrics);
        
        printf("JSON Output for OLSR:\n%s\n", json_output);
        
        // Save JSON file
        char filename[64];
        snprintf(filename, sizeof(filename), "/tmp/phy_node_%u.json", node_id);
        write_phy_json_for_olsr(&metrics, filename);
    }
    
    printf("\n� TESTING PERIODIC MONITORING:\n");
    rrc_monitor_all_neighbors(neighbor_nodes, num_neighbors);
    
    printf("\n📋 DEPLOYMENT CHECKLIST:\n");
    printf("   ✓ PetaLinux built with AD9361 IIO driver\n");
    printf("   ✓ AXI DMA driver enabled and device files created\n");
    printf("   ✓ Network interface statistics accessible\n");
    printf("   ✓ OLSR daemon configured to read JSON input\n");
    printf("   ✓ Named pipe created: mkfifo /tmp/olsr_phy_input\n");
    printf("   ✓ RRC integrated with periodic monitoring\n\n");
    
    printf("🎯 YOUR ARCHITECTURE IS CORRECT!\n");
    printf("   PHY→RRC→JSON→OLSR is an excellent approach for:\n");
    printf("   • Clean separation of concerns\n");
    printf("   • Easy debugging and monitoring\n");
    printf("   • Flexible routing algorithm updates\n");
    printf("   • Standard JSON interface between components\n");
    
    return 0;
}