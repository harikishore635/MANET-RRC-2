/**
 * PHY Metrics Simulator
 * Simulates PHY layer writing metrics to DRAM for testing
 * This program acts as the PHY hardware, writing link quality data
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "rrc_phy_metrics.h"

static bool g_running = true;

void signal_handler(int signum) {
    printf("\nStopping PHY simulator...\n");
    g_running = false;
}

// Simulate realistic link quality with variations
void simulate_link_metrics(PhyLinkMetrics* metrics, uint8_t neighbor_id, int iteration) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    // Base values with some randomness
    float noise = (rand() % 100) / 100.0 - 0.5;  // -0.5 to +0.5
    
    metrics->neighbor_id = neighbor_id;
    metrics->link_state = 1;  // UP
    
    // RSSI with variations (-70 ± 10 dBm)
    metrics->rssi_dbm = -70 + (int16_t)(sin(iteration * 0.1) * 10 + noise * 5);
    
    // SNR with variations (20 ± 5 dB)
    metrics->snr_db = 20 + (int16_t)(cos(iteration * 0.15) * 5 + noise * 3);
    
    metrics->noise_floor_dbm = -95;
    metrics->signal_quality = 70 + (rand() % 30);
    
    // Error rates (low for good link)
    metrics->bit_error_rate = 1000 + (rand() % 500);      // ~1e-6
    metrics->packet_error_rate = 5000 + (rand() % 3000);  // ~0.5%
    metrics->frame_error_rate = 3000 + (rand() % 2000);   // ~0.3%
    
    // Throughput (varying)
    metrics->rx_throughput = 100000 + (rand() % 50000);   // 100-150 KB/s
    metrics->tx_throughput = 80000 + (rand() % 40000);    // 80-120 KB/s
    
    // Timing
    metrics->time_offset_ns = (rand() % 1000) - 500;      // ±500 ns
    metrics->carrier_freq_offset = (rand() % 200) - 100;  // ±100 Hz
    metrics->timing_error_samples = rand() % 10;
    
    // Frame counters (incrementing)
    metrics->frames_received += 10 + (rand() % 5);
    metrics->frames_lost += (rand() % 100 == 0) ? 1 : 0;  // Rare loss
    metrics->frames_corrupted += (rand() % 50 == 0) ? 1 : 0;
    metrics->frames_retried += metrics->frames_lost;
    
    // Channel estimation
    metrics->channel_gain_db = -5.0 + noise;
    metrics->modulation_scheme = 2;  // 16QAM
    metrics->coding_rate = 1;        // 2/3
    
    // Timestamps
    metrics->last_update_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    metrics->update_count++;
}

// Simulate RF status
void simulate_rf_status(PhyRfStatus* status, int iteration) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    status->rf_power_state = 1;       // ON
    status->rf_calibration_state = 1; // CALIBRATED
    status->rf_temperature_c = 45 + (rand() % 10);  // 45-55°C
    status->rf_alarm_flags = 0;
    
    status->carrier_freq_hz = 2400000000;  // 2.4 GHz
    status->tx_power_dbm = 20;
    status->rx_gain_db = 30;
    
    status->agc_gain_level = 32768 + (rand() % 1000);
    status->afc_correction_hz = (rand() % 200) - 100;
    
    status->pll_lock = 1;
    status->synthesizer_lock = 1;
    
    status->uptime_ms = ts.tv_sec * 1000;
    status->total_tx_frames += 5;
    status->total_rx_frames += 5;
}

// Simulate diagnostics
void simulate_diagnostics(PhyDiagnostics* diag, int iteration) {
    diag->dma_tx_transfers += 5;
    diag->dma_rx_transfers += 5;
    diag->dma_errors += (rand() % 1000 == 0) ? 1 : 0;  // Rare errors
    
    diag->tx_buffer_usage = 30 + (rand() % 40);  // 30-70%
    diag->rx_buffer_usage = 25 + (rand() % 35);  // 25-60%
    diag->buffer_overruns += (rand() % 500 == 0) ? 1 : 0;
    diag->buffer_underruns += (rand() % 500 == 0) ? 1 : 0;
    
    diag->interrupt_count += 10;
    diag->missed_interrupts += (rand() % 5000 == 0) ? 1 : 0;
    
    diag->crc_errors += (rand() % 100 == 0) ? 1 : 0;
    diag->sync_errors += (rand() % 200 == 0) ? 1 : 0;
    diag->timeout_errors += (rand() % 300 == 0) ? 1 : 0;
    diag->fifo_errors += (rand() % 1000 == 0) ? 1 : 0;
}

int main(int argc, char* argv[]) {
    int shm_fd;
    void* shm_base;
    uint8_t num_neighbors = 3;  // Simulate 3 neighbors
    
    if (argc > 1) {
        num_neighbors = atoi(argv[1]);
        if (num_neighbors > PHY_MAX_NEIGHBORS) {
            num_neighbors = PHY_MAX_NEIGHBORS;
        }
    }
    
    printf("=============================================================\n");
    printf("  PHY METRICS SIMULATOR\n");
    printf("=============================================================\n");
    printf("Simulating %u neighbors\n", num_neighbors);
    printf("Creating shared memory region for PHY metrics\n");
    printf("Press Ctrl+C to stop\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create shared memory region to simulate PHY DRAM
    // In real hardware, PHY writes directly to physical DRAM
    // For simulation, use POSIX shared memory
    const char* shm_name = "/rrc_phy_metrics_sim";
    shm_unlink(shm_name);
    
    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return 1;
    }
    
    if (ftruncate(shm_fd, PHY_METRICS_SIZE) < 0) {
        perror("ftruncate");
        return 1;
    }
    
    shm_base = mmap(NULL, PHY_METRICS_SIZE, PROT_READ | PROT_WRITE, 
                    MAP_SHARED, shm_fd, 0);
    if (shm_base == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    memset(shm_base, 0, PHY_METRICS_SIZE);
    printf("Shared memory created: %s\n", shm_name);
    printf("Size: %u bytes\n", PHY_METRICS_SIZE);
    printf("To access from test program, use: sudo ./phy_metrics_test\n\n");
    
    // Initialize structures
    PhyLinkMetrics* link_metrics = 
        (PhyLinkMetrics*)((uint8_t*)shm_base + PHY_OFFSET_LINK_QUALITY);
    PhyRfStatus* rf_status = 
        (PhyRfStatus*)((uint8_t*)shm_base + PHY_OFFSET_RF_STATUS);
    PhyDiagnostics* diag = 
        (PhyDiagnostics*)((uint8_t*)shm_base + PHY_OFFSET_DIAGNOSTICS);
    
    // Initialize metrics for each neighbor
    for (uint8_t i = 0; i < num_neighbors; i++) {
        memset(&link_metrics[i], 0, sizeof(PhyLinkMetrics));
        link_metrics[i].neighbor_id = i + 1;
    }
    
    srand(time(NULL));
    
    // Simulation loop
    int iteration = 0;
    while (g_running) {
        // Update link metrics for all neighbors
        for (uint8_t i = 0; i < num_neighbors; i++) {
            simulate_link_metrics(&link_metrics[i], i + 1, iteration);
        }
        
        // Update RF status
        simulate_rf_status(rf_status, iteration);
        
        // Update diagnostics
        simulate_diagnostics(diag, iteration);
        
        if (iteration % 10 == 0) {
            printf("[PHY_SIM] Iteration %d - Updated metrics for %u neighbors\n", 
                   iteration, num_neighbors);
            
            // Print sample metrics for neighbor 1
            PhyLinkMetrics* m = &link_metrics[0];
            printf("  Neighbor 1: RSSI=%d dBm, SNR=%d dB, PER=%.2f%%, RX=%u frames\n",
                   m->rssi_dbm, m->snr_db, m->packet_error_rate / 1e4, 
                   m->frames_received);
        }
        
        iteration++;
        usleep(100000);  // 100ms update rate (10Hz)
    }
    
    // Cleanup
    munmap(shm_base, PHY_METRICS_SIZE);
    close(shm_fd);
    shm_unlink(shm_name);
    
    printf("PHY metrics simulator stopped\n");
    return 0;
}
