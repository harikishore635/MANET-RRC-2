/**
 * RRC PHY Metrics Access Module
 * Direct DRAM access to PHY-layer link quality metrics
 * PHY writes metrics to fixed memory addresses; RRC reads them
 */

#ifndef RRC_PHY_METRICS_H
#define RRC_PHY_METRICS_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

// ============================================================================
// PHY METRICS MEMORY LAYOUT
// ============================================================================

// Base address for PHY metrics region (configurable based on hardware)
#define PHY_METRICS_BASE_ADDR    0x40000000   // Example: AXI/FPGA memory region
#define PHY_METRICS_SIZE         0x10000      // 64KB region for PHY metrics

// Offset definitions within PHY metrics region
#define PHY_OFFSET_LINK_QUALITY  0x0000       // Link quality metrics per neighbor
#define PHY_OFFSET_RF_STATUS     0x4000       // RF module status
#define PHY_OFFSET_DIAGNOSTICS   0x8000       // PHY diagnostics and counters
#define PHY_OFFSET_CONFIG        0xC000       // PHY configuration registers

// Maximum neighbors for metrics tracking
#define PHY_MAX_NEIGHBORS        40

// ============================================================================
// PHY LINK QUALITY METRICS STRUCTURE
// ============================================================================

/**
 * Per-neighbor link quality metrics written by PHY
 * Located at: PHY_METRICS_BASE_ADDR + PHY_OFFSET_LINK_QUALITY + (neighbor_id * sizeof(PhyLinkMetrics))
 */
typedef struct __attribute__((packed)) {
    // Neighbor identification
    uint8_t  neighbor_id;           // Neighbor node ID (1-255)
    uint8_t  link_state;            // 0=DOWN, 1=UP, 2=DEGRADED, 3=UNKNOWN
    uint16_t reserved1;
    
    // Signal strength metrics (updated per frame)
    int16_t  rssi_dbm;              // RSSI in dBm (-120 to 0)
    int16_t  snr_db;                // SNR in dB (0 to 40)
    uint16_t noise_floor_dbm;       // Noise floor in dBm
    uint16_t signal_quality;        // Signal quality index 0-100
    
    // Error rates (updated per 100 frames)
    uint32_t bit_error_rate;        // BER × 10^9 (e.g., 1000 = 1e-6)
    uint32_t packet_error_rate;     // PER × 10^6 (e.g., 10000 = 1%)
    uint32_t frame_error_rate;      // FER × 10^6
    
    // Throughput metrics (bytes/sec)
    uint32_t rx_throughput;         // RX throughput in bytes/sec
    uint32_t tx_throughput;         // TX throughput in bytes/sec
    
    // Timing and synchronization
    uint32_t time_offset_ns;        // Time sync offset in nanoseconds
    uint16_t carrier_freq_offset;   // CFO in Hz
    uint16_t timing_error_samples;  // Timing error in samples
    
    // Frame counters
    uint32_t frames_received;       // Total frames received
    uint32_t frames_lost;           // Frames lost (seq number gaps)
    uint32_t frames_corrupted;      // Frames with CRC errors
    uint32_t frames_retried;        // Retransmission count
    
    // Channel estimation
    float    channel_gain_db;       // Channel gain in dB
    uint8_t  modulation_scheme;     // 0=BPSK, 1=QPSK, 2=16QAM, 3=64QAM
    uint8_t  coding_rate;           // 0=1/2, 1=2/3, 2=3/4, 3=5/6
    uint16_t reserved2;
    
    // Timestamps
    uint64_t last_update_ns;        // Timestamp of last PHY update (nanoseconds)
    uint32_t update_count;          // Number of updates from PHY
    uint32_t reserved3;
    
} PhyLinkMetrics;  // Total: 88 bytes per neighbor

// ============================================================================
// PHY RF STATUS STRUCTURE
// ============================================================================

/**
 * RF module overall status
 * Located at: PHY_METRICS_BASE_ADDR + PHY_OFFSET_RF_STATUS
 */
typedef struct __attribute__((packed)) {
    // RF module state
    uint8_t  rf_power_state;        // 0=OFF, 1=ON, 2=STANDBY
    uint8_t  rf_calibration_state;  // 0=UNCALIBRATED, 1=CALIBRATED
    uint8_t  rf_temperature_c;      // Temperature in Celsius
    uint8_t  rf_alarm_flags;        // Bit flags for alarms
    
    // Frequency and power
    uint32_t carrier_freq_hz;       // Current carrier frequency in Hz
    int16_t  tx_power_dbm;          // TX power in dBm
    int16_t  rx_gain_db;            // RX gain in dB
    
    // AGC and AFC status
    uint16_t agc_gain_level;        // AGC gain level (0-65535)
    int16_t  afc_correction_hz;     // AFC correction in Hz
    
    // PLL lock status
    uint8_t  pll_lock;              // 0=UNLOCKED, 1=LOCKED
    uint8_t  synthesizer_lock;      // 0=UNLOCKED, 1=LOCKED
    uint16_t reserved;
    
    // Overall statistics
    uint64_t uptime_ms;             // RF uptime in milliseconds
    uint32_t total_tx_frames;       // Total frames transmitted
    uint32_t total_rx_frames;       // Total frames received
    
} PhyRfStatus;  // Total: 32 bytes

// ============================================================================
// PHY DIAGNOSTICS STRUCTURE
// ============================================================================

/**
 * PHY diagnostics and counters
 * Located at: PHY_METRICS_BASE_ADDR + PHY_OFFSET_DIAGNOSTICS
 */
typedef struct __attribute__((packed)) {
    // DMA statistics
    uint32_t dma_tx_transfers;      // DMA TX transfer count
    uint32_t dma_rx_transfers;      // DMA RX transfer count
    uint32_t dma_errors;            // DMA error count
    
    // Buffer statistics
    uint16_t tx_buffer_usage;       // TX buffer usage (0-100%)
    uint16_t rx_buffer_usage;       // RX buffer usage (0-100%)
    uint32_t buffer_overruns;       // Buffer overrun count
    uint32_t buffer_underruns;      // Buffer underrun count
    
    // Interrupt statistics
    uint32_t interrupt_count;       // Total interrupts
    uint32_t missed_interrupts;     // Missed interrupt count
    
    // Error counters
    uint32_t crc_errors;            // Total CRC errors
    uint32_t sync_errors;           // Synchronization errors
    uint32_t timeout_errors;        // Timeout errors
    uint32_t fifo_errors;           // FIFO errors
    
} PhyDiagnostics;  // Total: 48 bytes

// ============================================================================
// PHY METRICS CONTEXT
// ============================================================================

typedef struct {
    int mem_fd;                      // File descriptor for /dev/mem
    void* phy_base;                  // Mapped base address
    bool initialized;                // Init flag
    uint64_t last_read_ns;           // Last read timestamp
} PhyMetricsContext;

// ============================================================================
// API FUNCTIONS
// ============================================================================

/**
 * Initialize PHY metrics access (map memory region)
 * @param ctx PHY metrics context
 * @param base_addr Physical base address (0 = use default)
 * @return 0 on success, -1 on error
 */
static inline int phy_metrics_init(PhyMetricsContext* ctx, uint64_t base_addr) {
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(PhyMetricsContext));
    
    if (base_addr == 0) {
        base_addr = PHY_METRICS_BASE_ADDR;
    }
    
    // Open /dev/mem for physical memory access
    ctx->mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (ctx->mem_fd < 0) {
        perror("phy_metrics_init: open /dev/mem");
        return -1;
    }
    
    // Map PHY metrics region
    ctx->phy_base = mmap(NULL, PHY_METRICS_SIZE, PROT_READ, MAP_SHARED,
                         ctx->mem_fd, base_addr);
    if (ctx->phy_base == MAP_FAILED) {
        perror("phy_metrics_init: mmap");
        close(ctx->mem_fd);
        return -1;
    }
    
    ctx->initialized = true;
    printf("[PHY_METRICS] Initialized: base=0x%lx, size=%u\n", 
           base_addr, PHY_METRICS_SIZE);
    
    return 0;
}

/**
 * Cleanup PHY metrics access
 */
static inline void phy_metrics_cleanup(PhyMetricsContext* ctx) {
    if (!ctx || !ctx->initialized) return;
    
    if (ctx->phy_base != MAP_FAILED) {
        munmap(ctx->phy_base, PHY_METRICS_SIZE);
    }
    if (ctx->mem_fd >= 0) {
        close(ctx->mem_fd);
    }
    
    ctx->initialized = false;
}

/**
 * Read link quality metrics for specific neighbor
 * @param ctx PHY metrics context
 * @param neighbor_id Neighbor node ID (1-255)
 * @param metrics Output: metrics structure
 * @return 0 on success, -1 on error
 */
static inline int phy_read_link_metrics(PhyMetricsContext* ctx, 
                                        uint8_t neighbor_id,
                                        PhyLinkMetrics* metrics) {
    if (!ctx || !ctx->initialized || !metrics || neighbor_id == 0 || 
        neighbor_id > PHY_MAX_NEIGHBORS) {
        return -1;
    }
    
    // Calculate offset: base + link_quality_offset + (neighbor_id * entry_size)
    volatile PhyLinkMetrics* phy_entry = 
        (volatile PhyLinkMetrics*)((uint8_t*)ctx->phy_base + 
                                   PHY_OFFSET_LINK_QUALITY + 
                                   ((neighbor_id - 1) * sizeof(PhyLinkMetrics)));
    
    // Read metrics (volatile ensures actual memory read, not cached)
    memcpy(metrics, (void*)phy_entry, sizeof(PhyLinkMetrics));
    
    // Update context timestamp
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ctx->last_read_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    
    return 0;
}

/**
 * Read RF status
 */
static inline int phy_read_rf_status(PhyMetricsContext* ctx, PhyRfStatus* status) {
    if (!ctx || !ctx->initialized || !status) return -1;
    
    volatile PhyRfStatus* phy_status = 
        (volatile PhyRfStatus*)((uint8_t*)ctx->phy_base + PHY_OFFSET_RF_STATUS);
    
    memcpy(status, (void*)phy_status, sizeof(PhyRfStatus));
    return 0;
}

/**
 * Read PHY diagnostics
 */
static inline int phy_read_diagnostics(PhyMetricsContext* ctx, PhyDiagnostics* diag) {
    if (!ctx || !ctx->initialized || !diag) return -1;
    
    volatile PhyDiagnostics* phy_diag = 
        (volatile PhyDiagnostics*)((uint8_t*)ctx->phy_base + PHY_OFFSET_DIAGNOSTICS);
    
    memcpy(diag, (void*)phy_diag, sizeof(PhyDiagnostics));
    return 0;
}

/**
 * Calculate link quality score (0-100) from metrics
 */
static inline uint8_t phy_calculate_link_score(const PhyLinkMetrics* metrics) {
    if (!metrics || metrics->link_state == 0) return 0;  // Link down
    
    // Weighted scoring: RSSI(40%) + SNR(40%) + PER(20%)
    int rssi_score = (metrics->rssi_dbm + 120) * 100 / 120;  // -120dBm to 0dBm
    if (rssi_score < 0) rssi_score = 0;
    if (rssi_score > 100) rssi_score = 100;
    
    int snr_score = metrics->snr_db * 100 / 40;  // 0 to 40dB
    if (snr_score < 0) snr_score = 0;
    if (snr_score > 100) snr_score = 100;
    
    int per_score = 100 - (metrics->packet_error_rate / 10000);  // PER×10^6
    if (per_score < 0) per_score = 0;
    
    int total_score = (rssi_score * 40 + snr_score * 40 + per_score * 20) / 100;
    
    return (uint8_t)total_score;
}

/**
 * Check if link is usable for RRC decisions
 */
static inline bool phy_is_link_usable(const PhyLinkMetrics* metrics, 
                                      int16_t min_rssi_dbm, 
                                      int16_t min_snr_db) {
    if (!metrics || metrics->link_state == 0) return false;
    
    return (metrics->link_state == 1 &&                    // Link UP
            metrics->rssi_dbm >= min_rssi_dbm &&           // Sufficient RSSI
            metrics->snr_db >= min_snr_db &&               // Sufficient SNR
            metrics->packet_error_rate < 100000);          // PER < 10%
}

/**
 * Print link metrics (for debugging)
 */
static inline void phy_print_link_metrics(uint8_t neighbor_id, const PhyLinkMetrics* m) {
    printf("[PHY_METRICS] Neighbor %u:\n", neighbor_id);
    printf("  State: %u, RSSI: %d dBm, SNR: %d dB, Quality: %u%%\n",
           m->link_state, m->rssi_dbm, m->snr_db, m->signal_quality);
    printf("  BER: %.2e, PER: %.2f%%, FER: %.2f%%\n",
           m->bit_error_rate / 1e9, m->packet_error_rate / 1e4, 
           m->frame_error_rate / 1e4);
    printf("  RX: %u B/s, TX: %u B/s\n", m->rx_throughput, m->tx_throughput);
    printf("  Frames: RX=%u, Lost=%u, Corrupt=%u, Retry=%u\n",
           m->frames_received, m->frames_lost, m->frames_corrupted, m->frames_retried);
    printf("  Score: %u/100\n", phy_calculate_link_score(m));
}

#endif // RRC_PHY_METRICS_H
