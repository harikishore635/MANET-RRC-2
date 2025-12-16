/**
 * RRC Core with PHY Metrics Integration Example
 * Shows how RRC accesses PHY link quality metrics for routing decisions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "rrc_posix_mq_defs.h"
#include "rrc_shm_pool.h"
#include "rrc_mq_adapters.h"
#include "rrc_phy_metrics.h"

// ============================================================================
// GLOBAL STATE
// ============================================================================

static bool g_running = true;
static uint8_t g_node_id = 1;

// PHY metrics context
static PhyMetricsContext g_phy_ctx;

// ============================================================================
// PHY METRICS INTEGRATION FUNCTIONS
// ============================================================================

/**
 * Check if neighbor link is good enough for routing
 * Uses PHY metrics to validate link quality
 */
bool rrc_check_neighbor_link_quality(uint8_t neighbor_id) {
    PhyLinkMetrics metrics;
    
    // Read PHY metrics from DRAM
    if (phy_read_link_metrics(&g_phy_ctx, neighbor_id, &metrics) < 0) {
        printf("[RRC] WARNING: Failed to read PHY metrics for neighbor %u\n", 
               neighbor_id);
        return false;
    }
    
    // Check data freshness (reject if > 500ms old)
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t now_ns = now.tv_sec * 1000000000ULL + now.tv_nsec;
    uint64_t age_ms = (now_ns - metrics.last_update_ns) / 1000000;
    
    if (age_ms > 500) {
        printf("[RRC] WARNING: Stale PHY metrics for neighbor %u (age=%lu ms)\n",
               neighbor_id, age_ms);
        return false;
    }
    
    // RRC link quality criteria:
    // - Link must be UP
    // - RSSI >= -85 dBm (sufficient signal strength)
    // - SNR >= 12 dB (good signal quality)
    // - PER < 5% (acceptable error rate)
    bool is_usable = phy_is_link_usable(&metrics, -85, 12);
    
    if (!is_usable) {
        printf("[RRC] Neighbor %u link quality insufficient:\n", neighbor_id);
        printf("      RSSI=%d dBm, SNR=%d dB, PER=%.2f%%\n",
               metrics.rssi_dbm, metrics.snr_db, 
               metrics.packet_error_rate / 1e4);
    }
    
    return is_usable;
}

/**
 * Select best neighbor based on PHY metrics
 * Used for multi-path routing decisions
 */
uint8_t rrc_select_best_neighbor(uint8_t* candidates, uint8_t num_candidates) {
    if (num_candidates == 0) return 0;
    
    uint8_t best_neighbor = 0;
    uint8_t best_score = 0;
    
    printf("[RRC] Evaluating %u candidate neighbors...\n", num_candidates);
    
    for (uint8_t i = 0; i < num_candidates; i++) {
        uint8_t neighbor_id = candidates[i];
        PhyLinkMetrics metrics;
        
        if (phy_read_link_metrics(&g_phy_ctx, neighbor_id, &metrics) < 0) {
            continue;
        }
        
        // Calculate link quality score
        uint8_t score = phy_calculate_link_score(&metrics);
        
        printf("  Neighbor %u: score=%u/100, RSSI=%d dBm, SNR=%d dB, PER=%.2f%%\n",
               neighbor_id, score, metrics.rssi_dbm, metrics.snr_db,
               metrics.packet_error_rate / 1e4);
        
        if (score > best_score) {
            best_score = score;
            best_neighbor = neighbor_id;
        }
    }
    
    if (best_neighbor > 0) {
        printf("[RRC] Selected neighbor %u (score=%u/100)\n", 
               best_neighbor, best_score);
    } else {
        printf("[RRC] No suitable neighbor found\n");
    }
    
    return best_neighbor;
}

/**
 * Monitor PHY metrics for all neighbors
 * Periodic task to track link quality changes
 */
void rrc_monitor_phy_metrics(uint8_t* neighbors, uint8_t num_neighbors) {
    static uint32_t monitor_count = 0;
    PhyRfStatus rf_status;
    
    printf("\n[RRC] ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("[RRC] PHY Metrics Monitor - Cycle %u\n", monitor_count++);
    printf("[RRC] ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    // Check RF status
    if (phy_read_rf_status(&g_phy_ctx, &rf_status) == 0) {
        printf("[RRC] RF Status: Power=%s, Temp=%u°C, PLL=%s\n",
               rf_status.rf_power_state == 1 ? "ON" : "OFF",
               rf_status.rf_temperature_c,
               rf_status.pll_lock ? "LOCKED" : "UNLOCKED");
        
        if (rf_status.rf_power_state != 1 || !rf_status.pll_lock) {
            printf("[RRC] WARNING: RF module not operational!\n");
        }
    }
    
    // Check each neighbor's link quality
    for (uint8_t i = 0; i < num_neighbors; i++) {
        uint8_t neighbor_id = neighbors[i];
        PhyLinkMetrics metrics;
        
        if (phy_read_link_metrics(&g_phy_ctx, neighbor_id, &metrics) == 0) {
            uint8_t score = phy_calculate_link_score(&metrics);
            bool usable = phy_is_link_usable(&metrics, -85, 12);
            
            printf("[RRC] Neighbor %u: %s | Score=%u | RSSI=%d dBm | SNR=%d dB | PER=%.2f%%\n",
                   neighbor_id,
                   usable ? "✓ USABLE  " : "✗ UNUSABLE",
                   score,
                   metrics.rssi_dbm,
                   metrics.snr_db,
                   metrics.packet_error_rate / 1e4);
            
            // Alert on link degradation
            if (metrics.link_state == 2) {  // DEGRADED
                printf("[RRC] ⚠ WARNING: Link to neighbor %u is degraded!\n", 
                       neighbor_id);
            }
            
            // Alert on high error rates
            if (metrics.packet_error_rate > 100000) {  // > 10%
                printf("[RRC] ⚠ WARNING: High PER (%.2f%%) to neighbor %u\n",
                       metrics.packet_error_rate / 1e4, neighbor_id);
            }
        }
    }
    
    printf("[RRC] ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
}

/**
 * RRC routing decision with PHY metrics validation
 * Example: Before sending frame, validate next-hop link quality
 */
int rrc_validate_route_and_send(uint8_t dest_node, uint8_t next_hop, 
                                 void* frame_data, size_t frame_size) {
    printf("[RRC] Route validation for dest=%u via next_hop=%u\n", 
           dest_node, next_hop);
    
    // Step 1: Check PHY link quality to next hop
    if (!rrc_check_neighbor_link_quality(next_hop)) {
        printf("[RRC] ✗ Route rejected: Poor link quality to next_hop %u\n", 
               next_hop);
        
        // Could trigger alternate route discovery here
        printf("[RRC] TODO: Request alternate route from OLSR\n");
        return -1;
    }
    
    printf("[RRC] ✓ Route validated: Link to next_hop %u is good\n", next_hop);
    
    // Step 2: Read detailed metrics for logging
    PhyLinkMetrics metrics;
    if (phy_read_link_metrics(&g_phy_ctx, next_hop, &metrics) == 0) {
        printf("[RRC] Link metrics: RSSI=%d dBm, SNR=%d dB, Throughput=%u B/s\n",
               metrics.rssi_dbm, metrics.snr_db, metrics.tx_throughput);
    }
    
    // Step 3: Send frame (actual implementation would use message queues)
    printf("[RRC] ✓ Sending %zu-byte frame to next_hop %u\n", 
           frame_size, next_hop);
    
    return 0;
}

// ============================================================================
// SIGNAL HANDLER
// ============================================================================

void signal_handler(int signum) {
    printf("\n[RRC] Received signal %d, shutting down...\n", signum);
    g_running = false;
}

// ============================================================================
// MAIN DEMO
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc > 1) {
        g_node_id = atoi(argv[1]);
    }
    
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  RRC Core with PHY Metrics Integration\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Node ID: %u\n", g_node_id);
    printf("Press Ctrl+C to exit\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize PHY metrics access
    printf("[RRC] Initializing PHY metrics access...\n");
    if (phy_metrics_init(&g_phy_ctx, 0) < 0) {
        fprintf(stderr, "[RRC] ERROR: Failed to initialize PHY metrics\n");
        fprintf(stderr, "Note: For simulation, run phy_metrics_simulator first\n");
        fprintf(stderr, "      For real hardware, ensure /dev/mem access\n");
        return 1;
    }
    printf("[RRC] ✓ PHY metrics initialized\n\n");
    
    // Define neighbors for this node
    uint8_t neighbors[] = {2, 3};  // Node 1 has neighbors 2 and 3
    uint8_t num_neighbors = 2;
    
    // Demo: Various RRC operations using PHY metrics
    sleep(2);  // Wait for PHY simulator to populate data
    
    // Example 1: Monitor all neighbor links
    rrc_monitor_phy_metrics(neighbors, num_neighbors);
    sleep(2);
    
    // Example 2: Select best neighbor for routing
    printf("[RRC] Example: Selecting best neighbor for routing...\n");
    uint8_t best = rrc_select_best_neighbor(neighbors, num_neighbors);
    if (best > 0) {
        printf("[RRC] ✓ Best neighbor selected: %u\n\n", best);
    }
    sleep(2);
    
    // Example 3: Validate route before sending
    printf("[RRC] Example: Validating route before frame transmission...\n");
    uint8_t frame[100] = {0};
    rrc_validate_route_and_send(3, 2, frame, sizeof(frame));
    printf("\n");
    sleep(2);
    
    // Main loop: Periodic monitoring
    printf("[RRC] Entering main monitoring loop...\n\n");
    int loop_count = 0;
    while (g_running && loop_count < 5) {  // Run 5 cycles for demo
        rrc_monitor_phy_metrics(neighbors, num_neighbors);
        
        // Every 2 cycles, re-evaluate best neighbor
        if (loop_count % 2 == 0) {
            printf("[RRC] Re-evaluating best neighbor...\n");
            rrc_select_best_neighbor(neighbors, num_neighbors);
            printf("\n");
        }
        
        sleep(3);
        loop_count++;
    }
    
    // Cleanup
    printf("[RRC] Shutting down...\n");
    phy_metrics_cleanup(&g_phy_ctx);
    printf("[RRC] ✓ PHY metrics cleaned up\n");
    printf("[RRC] Exiting\n");
    
    return 0;
}
