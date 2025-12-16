/**
 * PHY Metrics Test Program
 * Demonstrates reading PHY link quality metrics from DRAM
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "rrc_phy_metrics.h"

static bool g_running = true;

void signal_handler(int signum) {
    printf("\nShutting down...\n");
    g_running = false;
}

int main(int argc, char* argv[]) {
    PhyMetricsContext ctx;
    PhyLinkMetrics link_metrics;
    PhyRfStatus rf_status;
    PhyDiagnostics diag;
    
    uint8_t target_neighbor = 2;  // Default neighbor to monitor
    uint64_t base_addr = 0;       // 0 = use default PHY_METRICS_BASE_ADDR
    
    // Parse command line arguments
    if (argc > 1) {
        target_neighbor = atoi(argv[1]);
    }
    if (argc > 2) {
        base_addr = strtoull(argv[2], NULL, 0);  // Support hex: 0x40000000
    }
    
    printf("=============================================================\n");
    printf("  PHY METRICS TEST - Reading from DRAM\n");
    printf("=============================================================\n");
    printf("Target neighbor: %u\n", target_neighbor);
    printf("PHY base address: 0x%lx\n", base_addr ? base_addr : PHY_METRICS_BASE_ADDR);
    printf("Press Ctrl+C to exit\n\n");
    
    // Register signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize PHY metrics access
    if (phy_metrics_init(&ctx, base_addr) < 0) {
        fprintf(stderr, "ERROR: Failed to initialize PHY metrics access\n");
        fprintf(stderr, "Note: Requires root privileges to access /dev/mem\n");
        fprintf(stderr, "Run as: sudo ./phy_metrics_test [neighbor_id] [base_addr]\n");
        return 1;
    }
    
    printf("PHY metrics initialized successfully\n\n");
    
    // Main monitoring loop
    int iteration = 0;
    while (g_running) {
        printf("────────────────────────────────────────────────────────────\n");
        printf("Iteration %d - %s", iteration++, ctime(&(time_t){time(NULL)}));
        printf("────────────────────────────────────────────────────────────\n");
        
        // Read link metrics for target neighbor
        if (phy_read_link_metrics(&ctx, target_neighbor, &link_metrics) == 0) {
            phy_print_link_metrics(target_neighbor, &link_metrics);
            
            // Check link usability
            bool usable = phy_is_link_usable(&link_metrics, -90, 10);  // -90dBm, 10dB
            printf("  Link Usable (RRC criteria): %s\n", usable ? "YES" : "NO");
            
            // Check data freshness
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            uint64_t now_ns = now.tv_sec * 1000000000ULL + now.tv_nsec;
            uint64_t age_ms = (now_ns - link_metrics.last_update_ns) / 1000000;
            printf("  Data age: %lu ms (updates: %u)\n", 
                   age_ms, link_metrics.update_count);
            
            if (age_ms > 1000) {
                printf("  WARNING: Stale data (> 1 second old)\n");
            }
        } else {
            printf("ERROR: Failed to read link metrics for neighbor %u\n", 
                   target_neighbor);
        }
        
        printf("\n");
        
        // Read RF status every 5 iterations
        if (iteration % 5 == 0) {
            if (phy_read_rf_status(&ctx, &rf_status) == 0) {
                printf("[RF STATUS]\n");
                printf("  Power: %s, Calibration: %s, Temp: %u°C\n",
                       rf_status.rf_power_state == 1 ? "ON" : "OFF",
                       rf_status.rf_calibration_state == 1 ? "CAL" : "UNCAL",
                       rf_status.rf_temperature_c);
                printf("  Freq: %u Hz, TX Power: %d dBm, RX Gain: %d dB\n",
                       rf_status.carrier_freq_hz, rf_status.tx_power_dbm, 
                       rf_status.rx_gain_db);
                printf("  PLL Lock: %s, AGC: %u\n",
                       rf_status.pll_lock ? "LOCKED" : "UNLOCKED",
                       rf_status.agc_gain_level);
                printf("  Uptime: %lu ms, TX: %u, RX: %u frames\n",
                       rf_status.uptime_ms, rf_status.total_tx_frames, 
                       rf_status.total_rx_frames);
                printf("\n");
            }
        }
        
        // Read diagnostics every 10 iterations
        if (iteration % 10 == 0) {
            if (phy_read_diagnostics(&ctx, &diag) == 0) {
                printf("[PHY DIAGNOSTICS]\n");
                printf("  DMA: TX=%u, RX=%u, Errors=%u\n",
                       diag.dma_tx_transfers, diag.dma_rx_transfers, 
                       diag.dma_errors);
                printf("  Buffers: TX=%u%%, RX=%u%%, Overruns=%u, Underruns=%u\n",
                       diag.tx_buffer_usage, diag.rx_buffer_usage,
                       diag.buffer_overruns, diag.buffer_underruns);
                printf("  Interrupts: Total=%u, Missed=%u\n",
                       diag.interrupt_count, diag.missed_interrupts);
                printf("  Errors: CRC=%u, Sync=%u, Timeout=%u, FIFO=%u\n",
                       diag.crc_errors, diag.sync_errors, 
                       diag.timeout_errors, diag.fifo_errors);
                printf("\n");
            }
        }
        
        sleep(1);  // Update every 1 second
    }
    
    // Cleanup
    phy_metrics_cleanup(&ctx);
    printf("\nPHY metrics test completed\n");
    
    return 0;
}
