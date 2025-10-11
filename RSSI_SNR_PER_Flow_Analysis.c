/*
 * RSSI, PER, SNR FLOW ANALYSIS
 * ============================
 * Complete flow from PHY → RRC → OLSR with threshold comparisons
 * Platform: ZCU104 Zynq UltraScale+
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

// ============================================================================
// STEP 1: PHY LAYER - WHERE VALUES ORIGINATE
// ============================================================================

void explain_phy_layer_source() {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║               PHY LAYER - VALUE ORIGINS                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("📡 PHY LAYER (Physical Layer) VALUES:\n");
    printf("=====================================\n");
    printf("• RSSI (Received Signal Strength Indicator)\n");
    printf("  - Measured by: Radio receiver hardware\n");
    printf("  - Units: dBm (decibel-milliwatts)\n");
    printf("  - Range: -120 dBm to -30 dBm (typical)\n");
    printf("  - Source: ADC measurements from RF front-end\n\n");
    
    printf("• SNR (Signal-to-Noise Ratio)\n");
    printf("  - Measured by: Digital signal processing\n");
    printf("  - Units: dB (decibels)\n");
    printf("  - Range: 0 dB to 30+ dB\n");
    printf("  - Source: Signal power vs noise floor calculation\n\n");
    
    printf("• PER (Packet Error Rate)\n");
    printf("  - Calculated by: MAC layer statistics\n");
    printf("  - Units: Percentage (0-100%)\n");
    printf("  - Range: 0% (perfect) to 100% (no packets)\n");
    printf("  - Source: Failed packets / Total packets ratio\n\n");
    
    printf("🔧 HOW PHY GENERATES VALUES:\n");
    printf("============================\n");
    printf("1. Radio receives RF signal\n");
    printf("2. ADC converts to digital samples\n");
    printf("3. DSP calculates RSSI from signal amplitude\n");
    printf("4. DSP calculates SNR from signal/noise ratio\n");
    printf("5. MAC counts successful/failed packet reception\n");
    printf("6. MAC calculates PER from packet statistics\n\n");
}

// ============================================================================
// STEP 2: PHY TO RRC INTERFACE
// ============================================================================

// Simulated PHY layer function that sends values to RRC
void phy_send_measurements_to_rrc(uint8_t neighbor_node_id) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                PHY → RRC INTERFACE\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("📊 PHY LAYER MEASUREMENTS:\n");
    printf("===========================\n");
    
    // Simulate real PHY measurements
    float measured_rssi = -75.3f;  // From radio hardware
    float measured_snr = 14.2f;    // From DSP processing  
    float measured_per = 2.8f;     // From MAC statistics
    
    printf("Neighbor Node %u measurements:\n", neighbor_node_id);
    printf("• RSSI: %.1f dBm (from RF receiver)\n", measured_rssi);
    printf("• SNR:  %.1f dB  (from signal processing)\n", measured_snr);
    printf("• PER:  %.1f%%   (from packet statistics)\n", measured_per);
    printf("\n");
    
    printf("🔄 CALLING RRC INTERFACE:\n");
    printf("=========================\n");
    printf("phy_to_rrc_interface(node=%u, rssi=%.1f, snr=%.1f, per=%.1f)\n\n",
           neighbor_node_id, measured_rssi, measured_snr, measured_per);
    
    // This is where PHY calls RRC (simulation)
    // rrc_update_link_quality(neighbor_node_id, measured_rssi, measured_snr, measured_per);
}

// ============================================================================
// STEP 3: RRC PROCESSING AND THRESHOLD COMPARISON
// ============================================================================

// Thresholds from your OLSR JSON specification
#define RSSI_ACTIVE_THRESHOLD -85.0f
#define SNR_ACTIVE_THRESHOLD   10.0f  
#define PER_ACTIVE_THRESHOLD   10.0f
#define RSSI_CHANGE_TRIGGER     5.0f
#define SNR_CHANGE_TRIGGER      3.0f
#define PER_CHANGE_TRIGGER      5.0f

typedef struct {
    uint8_t node_id;
    float rssi_dbm;
    float snr_db;
    float per_percent;
    uint32_t timestamp;
    bool link_active;
} LinkQualityMetrics;

// Previous values for comparison (simulated storage)
static LinkQualityMetrics prev_metrics[10] = {0};

void rrc_process_phy_measurements(uint8_t node_id, float rssi, float snr, float per) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("            RRC PROCESSING & THRESHOLD COMPARISON\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("📥 RRC RECEIVES FROM PHY:\n");
    printf("=========================\n");
    printf("Node %u: RSSI=%.1f dBm, SNR=%.1f dB, PER=%.1f%%\n\n", 
           node_id, rssi, snr, per);
    
    // Get previous values for this node
    LinkQualityMetrics *prev = &prev_metrics[node_id];
    
    printf("🔍 RRC THRESHOLD COMPARISONS:\n");
    printf("=============================\n");
    
    // 1. Link Quality Assessment
    printf("1. LINK QUALITY ASSESSMENT:\n");
    printf("   RSSI %.1f vs threshold %.1f dBm: %s\n", 
           rssi, RSSI_ACTIVE_THRESHOLD, 
           (rssi > RSSI_ACTIVE_THRESHOLD) ? "✅ GOOD" : "❌ POOR");
    printf("   SNR  %.1f vs threshold %.1f dB:  %s\n", 
           snr, SNR_ACTIVE_THRESHOLD,
           (snr > SNR_ACTIVE_THRESHOLD) ? "✅ GOOD" : "❌ POOR");
    printf("   PER  %.1f vs threshold %.1f%%:   %s\n", 
           per, PER_ACTIVE_THRESHOLD,
           (per < PER_ACTIVE_THRESHOLD) ? "✅ GOOD" : "❌ POOR");
    
    bool link_active = (rssi > RSSI_ACTIVE_THRESHOLD && 
                       snr > SNR_ACTIVE_THRESHOLD && 
                       per < PER_ACTIVE_THRESHOLD);
    
    printf("   OVERALL LINK STATUS: %s\n\n", link_active ? "🟢 ACTIVE" : "🔴 DEGRADED");
    
    // 2. Change Detection  
    printf("2. CHANGE DETECTION (triggers OLSR update):\n");
    float rssi_change = fabs(rssi - prev->rssi_dbm);
    float snr_change = fabs(snr - prev->snr_db);
    float per_change = fabs(per - prev->per_percent);
    
    printf("   RSSI change: %.1f dB (trigger: %.1f dB) %s\n",
           rssi_change, RSSI_CHANGE_TRIGGER,
           (rssi_change > RSSI_CHANGE_TRIGGER) ? "🚨 SIGNIFICANT" : "📊 Normal");
    printf("   SNR change:  %.1f dB (trigger: %.1f dB) %s\n",
           snr_change, SNR_CHANGE_TRIGGER,
           (snr_change > SNR_CHANGE_TRIGGER) ? "🚨 SIGNIFICANT" : "📊 Normal");
    printf("   PER change:  %.1f%% (trigger: %.1f%%) %s\n",
           per_change, PER_CHANGE_TRIGGER,
           (per_change > PER_CHANGE_TRIGGER) ? "🚨 SIGNIFICANT" : "📊 Normal");
    
    bool trigger_olsr = (rssi_change > RSSI_CHANGE_TRIGGER ||
                        snr_change > SNR_CHANGE_TRIGGER ||
                        per_change > PER_CHANGE_TRIGGER);
    
    printf("   OLSR TRIGGER DECISION: %s\n\n", 
           trigger_olsr ? "🔄 UPDATE NEEDED" : "⏸️ No update needed");
    
    // 3. Store current values as previous for next comparison
    prev->node_id = node_id;
    prev->rssi_dbm = rssi;
    prev->snr_db = snr;
    prev->per_percent = per;
    prev->timestamp = time(NULL);
    prev->link_active = link_active;
    
    printf("💾 RRC DECISION:\n");
    printf("================\n");
    if (trigger_olsr) {
        printf("✅ Sending update to OLSR with new measurements\n");
        printf("✅ Link quality changed significantly\n");
        rrc_send_to_olsr(node_id, rssi, snr, per, link_active);
    } else {
        printf("⏭️ No OLSR update needed - values within thresholds\n");
        printf("⏭️ Using existing routes\n");
    }
    printf("\n");
}

// ============================================================================
// STEP 4: RRC TO OLSR INTERFACE  
// ============================================================================

void rrc_send_to_olsr(uint8_t node_id, float rssi, float snr, float per, bool link_active) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                RRC → OLSR INTERFACE\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("📤 RRC SENDS TO OLSR:\n");
    printf("=====================\n");
    printf("Message Type: topology_update\n");
    printf("Reporting Node: 254 (our node)\n");
    printf("Target Node: %u\n", node_id);
    printf("Link Metrics:\n");
    printf("  • RSSI: %.1f dBm\n", rssi);
    printf("  • SNR:  %.1f dB\n", snr);
    printf("  • PER:  %.1f%%\n", per);
    printf("  • Link Active: %s\n", link_active ? "true" : "false");
    printf("  • Timestamp: %u\n", (uint32_t)time(NULL));
    printf("\n");
    
    printf("📝 JSON MESSAGE TO OLSR:\n");
    printf("========================\n");
    printf("{\n");
    printf("  \"type\": \"topology_update\",\n");
    printf("  \"reporting_node\": 254,\n");
    printf("  \"timestamp\": %u,\n", (uint32_t)time(NULL));
    printf("  \"neighbors\": [\n");
    printf("    {\n");
    printf("      \"node_id\": %u,\n", node_id);
    printf("      \"rssi_dbm\": %.1f,\n", rssi);
    printf("      \"snr_db\": %.1f,\n", snr);
    printf("      \"per_percent\": %.1f,\n", per);
    printf("      \"link_active\": %s,\n", link_active ? "true" : "false");
    printf("      \"last_seen\": %u\n", (uint32_t)time(NULL));
    printf("    }\n");
    printf("  ]\n");
    printf("}\n\n");
    
    // Simulate OLSR processing
    olsr_process_link_update(node_id, rssi, snr, per, link_active);
}

// ============================================================================
// STEP 5: OLSR PROCESSING AND ROUTE CALCULATION
// ============================================================================

void olsr_process_link_update(uint8_t node_id, float rssi, float snr, float per, bool link_active) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("              OLSR PROCESSING & ROUTE CALCULATION\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("🧠 OLSR RECEIVES FROM RRC:\n");
    printf("==========================\n");
    printf("Node %u link update received\n", node_id);
    printf("Raw measurements: RSSI=%.1f, SNR=%.1f, PER=%.1f\n", rssi, snr, per);
    printf("Link status: %s\n\n", link_active ? "ACTIVE" : "DEGRADED");
    
    printf("⚙️ OLSR INTERNAL PROCESSING:\n");
    printf("============================\n");
    
    // 1. Calculate link quality metric (0.0 to 1.0)
    float link_quality = 0.0f;
    if (link_active) {
        // Normalize values to 0-1 scale
        float rssi_normalized = (rssi + 120.0f) / 90.0f;  // -120 to -30 dBm
        float snr_normalized = snr / 30.0f;               // 0 to 30 dB
        float per_normalized = (100.0f - per) / 100.0f;   // 100% - PER
        
        // Weighted average (you can adjust weights)
        link_quality = (0.4f * rssi_normalized + 0.3f * snr_normalized + 0.3f * per_normalized);
        if (link_quality > 1.0f) link_quality = 1.0f;
        if (link_quality < 0.0f) link_quality = 0.0f;
    }
    
    printf("1. Link Quality Calculation:\n");
    printf("   RSSI factor: %.2f\n", (rssi + 120.0f) / 90.0f);
    printf("   SNR factor:  %.2f\n", snr / 30.0f);
    printf("   PER factor:  %.2f\n", (100.0f - per) / 100.0f);
    printf("   Combined Link Quality: %.2f\n\n", link_quality);
    
    printf("2. Route Table Update:\n");
    if (link_quality > 0.3f) {
        printf("   ✅ Link quality acceptable (%.2f > 0.3)\n", link_quality);
        printf("   ✅ Adding/updating route to node %u\n", node_id);
        printf("   ✅ Next hop: %u (direct link)\n", node_id);
        printf("   ✅ Hop count: 1\n");
        printf("   ✅ Route metric: %.2f\n\n", 1.0f / link_quality);
    } else {
        printf("   ❌ Link quality too poor (%.2f ≤ 0.3)\n", link_quality);
        printf("   ❌ Removing direct route to node %u\n", node_id);
        printf("   🔄 Searching for alternative multi-hop routes\n\n");
    }
    
    printf("3. Route Response to RRC:\n");
    olsr_send_route_response_to_rrc(node_id, link_quality);
}

// ============================================================================
// STEP 6: OLSR TO RRC RESPONSE
// ============================================================================

void olsr_send_route_response_to_rrc(uint8_t dest_node, float link_quality) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                OLSR → RRC RESPONSE\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("📤 OLSR SENDS ROUTE RESPONSE:\n");
    printf("=============================\n");
    
    if (link_quality > 0.3f) {
        printf("✅ Route Available:\n");
        printf("   Source: 254 (our node)\n");
        printf("   Destination: %u\n", dest_node);
        printf("   Next Hop: %u (direct)\n", dest_node);
        printf("   Hop Count: 1\n");
        printf("   Link Quality: %.2f\n", link_quality);
        printf("   Route Valid: true\n");
        printf("   Route Lifetime: 300 seconds\n\n");
        
        printf("📝 JSON RESPONSE TO RRC:\n");
        printf("========================\n");
        printf("{\n");
        printf("  \"type\": \"route_response\",\n");
        printf("  \"source_node\": 254,\n");
        printf("  \"dest_node\": %u,\n", dest_node);
        printf("  \"next_hop\": %u,\n", dest_node);
        printf("  \"hop_count\": 1,\n");
        printf("  \"link_quality\": %.2f,\n", link_quality);
        printf("  \"route_valid\": true,\n");
        printf("  \"route_lifetime\": 300\n");
        printf("}\n\n");
    } else {
        printf("❌ No Route Available:\n");
        printf("   Destination: %u\n", dest_node);
        printf("   Reason: Link quality too poor (%.2f)\n", link_quality);
        printf("   Route Valid: false\n\n");
    }
    
    printf("🔄 RRC RECEIVES OLSR RESPONSE:\n");
    printf("==============================\n");
    printf("RRC updates internal routing table\n");
    printf("RRC ready to route messages to node %u\n", dest_node);
    printf("Next message to node %u will use this route\n\n", dest_node);
}

// ============================================================================
// COMPLETE FLOW DEMONSTRATION
// ============================================================================

void demonstrate_complete_flow() {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║            COMPLETE RSSI/SNR/PER FLOW DEMONSTRATION         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("🌊 FLOW SUMMARY:\n");
    printf("================\n");
    printf("PHY → RRC → OLSR → RRC (routing response)\n\n");
    
    printf("📍 WHERE COMPARISONS HAPPEN:\n");
    printf("============================\n");
    printf("1. RRC: Threshold comparison (link active/poor)\n");
    printf("2. RRC: Change detection (trigger OLSR update)\n");
    printf("3. OLSR: Link quality calculation (route metric)\n");
    printf("4. OLSR: Route validity check (quality > 0.3)\n\n");
    
    printf("📊 VALUES ARE NOT JUST ASSIGNED - THEY ARE:\n");
    printf("============================================\n");
    printf("✅ COMPARED against thresholds in RRC\n");
    printf("✅ ANALYZED for significant changes in RRC\n");
    printf("✅ PROCESSED into route metrics in OLSR\n");
    printf("✅ VALIDATED for route quality in OLSR\n\n");
    
    printf("🔄 STARTING DEMONSTRATION:\n");
    printf("==========================\n\n");
}

int main() {
    demonstrate_complete_flow();
    
    explain_phy_layer_source();
    
    printf("🚀 DEMONSTRATION: Node 2 link quality change\n");
    printf("=============================================\n\n");
    
    // Simulate PHY sending measurements to RRC
    phy_send_measurements_to_rrc(2);
    
    // RRC processes and compares against thresholds
    rrc_process_phy_measurements(2, -75.3f, 14.2f, 2.8f);
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                    FLOW SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("🎯 KEY POINTS:\n");
    printf("==============\n");
    printf("• PHY generates RSSI/SNR/PER from hardware measurements\n");
    printf("• RRC compares values against fixed thresholds\n");
    printf("• RRC detects significant changes to trigger OLSR updates\n");
    printf("• OLSR calculates link quality metrics for routing\n");
    printf("• OLSR validates routes based on quality thresholds\n");
    printf("• Values flow through comparison logic, not simple assignment\n\n");
    
    printf("📈 THRESHOLD VALUES (from olsr_json_info.json):\n");
    printf("===============================================\n");
    printf("• RSSI Active Threshold: %.1f dBm\n", RSSI_ACTIVE_THRESHOLD);
    printf("• SNR Active Threshold:  %.1f dB\n", SNR_ACTIVE_THRESHOLD);
    printf("• PER Active Threshold:  %.1f%%\n", PER_ACTIVE_THRESHOLD);
    printf("• RSSI Change Trigger:   %.1f dB\n", RSSI_CHANGE_TRIGGER);
    printf("• SNR Change Trigger:    %.1f dB\n", SNR_CHANGE_TRIGGER);
    printf("• PER Change Trigger:    %.1f%%\n", PER_CHANGE_TRIGGER);
    printf("\n🎉 Complete flow demonstration finished!\n");
    
    return 0;
}