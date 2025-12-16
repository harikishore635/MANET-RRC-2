/*
 * RRC Implementation - Radio & Resource Controller Middle Layer
 * Static Allocation Version - Pure Middle Layer Between L7/L3/L2
 * 
 * Purpose: Pure middle layer between L7 (Struct Packets), L3 (OLSR), and L2 (TDMA/queue.c)
 * 
 * Layer Integration:
 * - L7 Application: Custom packet structure parsing
 * - L3 OLSR Team: External routing API calls (no internal routing)
 * - L2 TDMA Team: External slot checking API calls (no internal scheduling)
 * - L2 queue.c: Direct frame enqueueing to priority queues
 * - PHY Layer: Link quality metrics (RSSI, SNR, PER)
 * 
 * Constraints:
 * - Static allocation only (no malloc/calloc/free)
 * - API calls to OLSR, TDMA, and PHY teams (no algorithm implementation)
 * - 16-byte payload limit enforcement
 * - Deterministic embedded-friendly functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Compatibility constants for queue.c
#define PAYLOAD_SIZE_BYTES 2800   // Updated payload size for larger data packets
#define NUM_PRIORITY 4          // From queue.c
#define QUEUE_SIZE 10           // From queue.c

// RRC Static Pool Configuration
#define RRC_MESSAGE_POOL_SIZE 16
#define MAX_MONITORED_NODES 40
#define RRC_CONNECTION_POOL_SIZE 8
#define RRC_INACTIVITY_TIMEOUT_SEC 30
#define RRC_SETUP_TIMEOUT_SEC 10

// MANET NC Slot Configuration
#define NC_SLOTS_PER_SUPERCYCLE 40
#define FRAMES_PER_CYCLE 10
#define CYCLES_PER_SUPERCYCLE 2
#define NC_SLOT_TIMEOUT_MS 2000
#define DU_GU_SLOTS_COUNT 60  // 6 slots per frame × 10 frames
#define NEIGHBOR_TIMEOUT_SUPERCYCLES 2

// RRC Node Configuration
static uint8_t rrc_node_id = 1;  // Default node ID, configurable

// ============================================================================
// EXTERNAL TEAM API DECLARATIONS (Provided by OLSR, TDMA, and PHY Teams)
// ============================================================================

// OLSR Team API (External - no implementation in RRC)
extern uint8_t olsr_get_next_hop(uint8_t destination_node_id);
extern void olsr_trigger_route_discovery(uint8_t destination_node_id);

// TDMA Team API (External - no implementation in RRC)
extern bool tdma_check_slot_available(uint8_t next_hop_node, int priority);
extern bool tdma_request_nc_slot(const uint8_t *payload, size_t payload_len, uint8_t *assigned_slot);

// PHY Layer API (External - no implementation in RRC)
extern void phy_get_link_metrics(uint8_t node_id, float *rssi, float *snr, float *per);
extern bool phy_is_link_active(uint8_t node_id);
extern uint32_t phy_get_packet_count(uint8_t node_id);

// Data types from queue.c
typedef enum{
    DATA_TYPE_DIGITAL_VOICE,
    DATA_TYPE_SMS,
    DATA_TYPE_FILE_TRANSFER,
    DATA_TYPE_VIDEO_STREAM,
    DATA_TYPE_ANALOG_VOICE
} DATATYPE;

// Frame structure from queue.c
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
};

// Queue structure from queue.c
struct queue{
    struct frame item[QUEUE_SIZE];
    int front;
    int back;
};

// ============================================================================
// APPLICATION LAYER CUSTOM PACKET STRUCTURE (L7)
// ============================================================================

// RRC Data type definitions (Application Layer)
typedef enum {
    RRC_DATA_TYPE_SMS = 0,
    RRC_DATA_TYPE_VOICE = 1,
    RRC_DATA_TYPE_VIDEO = 2,
    RRC_DATA_TYPE_FILE = 3,
    RRC_DATA_TYPE_RELAY = 4,
    RRC_DATA_TYPE_PTT = 5,
    RRC_DATA_TYPE_UNKNOWN = 99
} RRC_DataType;

// Priority mapping for queue.c
typedef enum {
    PRIORITY_ANALOG_VOICE_PTT = -1,  // → analog_voice_queue
    PRIORITY_DIGITAL_VOICE = 0,      // → data_queues[0]
    PRIORITY_DATA_1 = 1,             // → data_queues[1]
    PRIORITY_DATA_2 = 2,             // → data_queues[2]
    PRIORITY_DATA_3 = 3,             // → data_queues[3]
    PRIORITY_RX_RELAY = 4            // → rx_queue
} MessagePriority;

// Transmission type definitions
typedef enum {
    TRANSMISSION_UNICAST = 0,
    TRANSMISSION_MULTICAST = 1,
    TRANSMISSION_BROADCAST = 2
} TransmissionType;

// Custom Application Packet Structure (From L7 Application Layer)
typedef struct {
    uint8_t src_id;                          // Source node ID
    uint8_t dest_id;                         // Destination node ID
    RRC_DataType data_type;                  // Data type for priority mapping
    TransmissionType transmission_type;       // Unicast/Broadcast/Multicast
    uint8_t data[PAYLOAD_SIZE_BYTES];        // Actual data payload
    size_t data_size;                        // Size of data payload
    uint32_t sequence_number;                // Packet sequence number
    bool urgent;                             // Urgent/high priority flag
} CustomApplicationPacket;

// RRC Internal Message structure (for static pool management)
typedef struct {
    uint8_t node_id;
    uint8_t dest_node_id;
    RRC_DataType data_type;
    MessagePriority priority;
    TransmissionType transmission_type;
    uint8_t data[PAYLOAD_SIZE_BYTES];
    size_t data_size;
    bool preemption_allowed;
    bool in_use;  // Pool management flag
} ApplicationMessage;

// ============================================================================
// PHY LAYER METRICS - USING MANET STRUCTURE
// ============================================================================

// Link quality thresholds for route decisions
#define RSSI_POOR_THRESHOLD_DBM -90.0f
#define SNR_POOR_THRESHOLD_DB 10.0f
#define PER_POOR_THRESHOLD_PERCENT 50.0f
#define LINK_TIMEOUT_SECONDS 30

// ============================================================================
// MANET WAVEFORM STRUCTURES AND DEFINITIONS
// ============================================================================

// PHY Metrics Structure for neighbor tracking
typedef struct {
    float rssi_dbm;
    float snr_db;
    float per_percent;
    uint32_t packet_count;
    uint32_t last_update_time;
} PHYMetrics;

// Neighbor State Structure (MANET requirements)
typedef struct {
    uint16_t nodeID;
    uint64_t lastHeardTime;
    uint8_t txSlots[10];        // Which slots neighbor will transmit
    uint8_t rxSlots[10];        // Which slots neighbor expects to receive
    PHYMetrics phy;
    uint8_t capabilities;       // TX/RX capabilities bitmask
    bool active;
    uint8_t assignedNCSlot;     // NC slot assigned to this neighbor
} NeighborState;

// Slot Status Structure (Requirement 2)
typedef struct {
    uint64_t ncStatusBitmap;        // 40-bit NC status map (using 64-bit for alignment)
    uint64_t duGuUsageBitmap;       // 60-bit DU/GU usage forecast
    uint32_t lastUpdateTime;
} SlotStatus;

// Piggyback TLV Structure (Requirement 1)
typedef struct {
    uint8_t type;                   // TLV type identifier
    uint8_t length;                 // TLV length
    uint16_t sourceNodeID;          // Source node ID
    uint8_t sourceReservations;     // Source reservations (voice/data)
    uint8_t relayReservations;      // Relay reservations (voice/data)
    uint64_t duGuIntentionMap;      // 60-bit DU/GU slot intention
    uint64_t ncStatusBitmap;        // 40-bit NC slot status
    uint32_t timeSync;              // Time synchronization info
    uint8_t myNCSlot;              // My assigned NC slot
    uint8_t ttl;                   // Time-to-live for soft state
} PiggybackTLV;

// NC Slot Management
typedef struct {
    uint8_t activeNodes[MAX_MONITORED_NODES];
    uint8_t activeNodeCount;
    uint32_t supercycleCounter;
    uint8_t currentFrame;
    uint8_t currentSlot;
    uint8_t myAssignedNCSlot;
} NCSlotManager;

// TDMA slot assignment table
typedef struct {
    uint8_t slot_id;           // Slot number (0-7)
    uint8_t assigned_node;     // Node assigned to this slot
    bool is_tx_slot;           // This is a TX slot
    bool is_rx_slot;           // This is a RX slot
    bool is_nc_slot;           // Network Control slot
    bool collision_detected;   // Potential collision in this slot
    uint32_t last_update;      // Last update timestamp
} TDMA_SlotInfo;

// Static arrays for neighbor tracking (MANET requirements)
static NeighborState neighbor_table[MAX_MONITORED_NODES];
static int neighbor_count = 0;
static SlotStatus current_slot_status = {0};
static NCSlotManager nc_manager = {0};
static PiggybackTLV current_piggyback_tlv = {0};
static bool neighbor_tracking_initialized = false;

// TDMA slot coordination table
static TDMA_SlotInfo tdma_slot_table[8];

// Statistics for neighbor tracking
static struct {
    uint32_t hello_packets_parsed;
    uint32_t capabilities_updated;
    uint32_t nc_slots_assigned;
    uint32_t slot_conflicts_detected;
    uint32_t piggyback_updates;
} neighbor_stats = {0};

// ============================================================================
// RRC SYSTEM MANAGEMENT - FSM STATES AND CONNECTION CONTEXTS
// ============================================================================

// FSM States for RRC System Management
typedef enum {
    RRC_STATE_NULL = 0,           // Initial state, no RRC context
    RRC_STATE_IDLE,              // Registered but no active radio connection  
    RRC_STATE_CONNECTION_SETUP,   // Establishing radio resources
    RRC_STATE_CONNECTED,         // Active radio connection with allocated slots
    RRC_STATE_RECONFIGURATION,   // Handling mobility (route changes)
    RRC_STATE_RELEASE            // Releasing radio resources
} RRC_SystemState;

// Connection Context Structure (static allocation)
typedef struct {
    uint8_t dest_node_id;                    // Destination node for this connection
    uint8_t next_hop_id;                     // Current next hop via OLSR
    uint8_t allocated_slots[4];              // TDMA slots allocated for this connection
    uint32_t connection_start_time;          // When connection was established
    uint32_t last_activity_time;             // Last packet activity timestamp
    RRC_SystemState connection_state;        // State of this specific connection
    MessagePriority qos_priority;            // QoS requirements for this connection
    bool active;                             // Connection context in use
    bool setup_pending;                      // Waiting for setup completion
    bool reconfig_pending;                   // Reconfiguration in progress
} RRC_ConnectionContext;

// Static FSM state variables
static RRC_SystemState current_rrc_state = RRC_STATE_NULL;
static RRC_ConnectionContext connection_pool[RRC_CONNECTION_POOL_SIZE];
static bool fsm_initialized = false;

// FSM Statistics
static struct {
    uint32_t state_transitions;
    uint32_t connection_setups;
    uint32_t connection_releases;
    uint32_t reconfigurations;
    uint32_t setup_timeouts;
    uint32_t inactivity_releases;
    uint32_t power_on_events;
    uint32_t power_off_events;
} rrc_fsm_stats = {0};

// ============================================================================
// RRC EXTENSION – PIGGYBACK SUPPORT - USING MANET TLV STRUCTURE
// ============================================================================

// Piggyback state management (using MANET TLV structure)
static bool piggyback_active = false;
static uint32_t piggyback_last_update = 0;

// ============================================================================
// STATIC ALLOCATION POOLS AND MANAGEMENT
// ============================================================================

// Static message pool - no dynamic allocation
static ApplicationMessage message_pool[RRC_MESSAGE_POOL_SIZE];
static bool pool_initialized = false;

// Static application packet pool for notifications - no dynamic allocation
#define RRC_APP_PACKET_POOL_SIZE 10
typedef struct {
    CustomApplicationPacket packet;
    bool in_use;
} AppPacketPoolEntry;

static AppPacketPoolEntry app_packet_pool[RRC_APP_PACKET_POOL_SIZE];
static bool app_packet_pool_initialized = false;

// RRC Statistics (static counters)
static struct {
    uint32_t packets_processed;
    uint32_t messages_discarded_no_slots;
    uint32_t messages_enqueued_total;
    uint32_t nc_slot_requests;
    uint32_t route_discoveries_triggered;
    uint32_t phy_metrics_updates;
    uint32_t poor_links_detected;
} rrc_stats = {0};

// External queue structures from queue.c
extern struct queue analog_voice_queue;
extern struct queue data_from_l3_queue[NUM_PRIORITY];
extern struct queue rx_queue;
extern struct queue olsr_hello_queue;

// RRC-managed OLSR NC queue (separate from queue.c queues)
static struct queue rrc_olsr_nc_queue = {0};

// RRC-managed Relay queue for multi-hop data forwarding
static struct queue rrc_relay_queue = {0};

// OLSR NC Queue Statistics
static struct {
    uint32_t olsr_packets_received;
    uint32_t olsr_packets_enqueued;
    uint32_t olsr_packets_dequeued;
    uint32_t olsr_queue_full_drops;
    uint32_t tdma_nc_requests;
} olsr_nc_stats = {0};

// Relay Queue Statistics
static struct {
    uint32_t relay_packets_received;
    uint32_t relay_packets_enqueued;
    uint32_t relay_packets_dequeued;
    uint32_t relay_packets_discarded;
    uint32_t relay_queue_full_drops;
    uint32_t relay_packets_to_self;
} relay_stats = {0};

// External queue functions from queue.c
extern void enqueue(struct queue *q, struct frame rx_f);
extern struct frame dequeue(struct queue *q);
extern bool is_empty(struct queue *q);
extern bool is_full(struct queue *q);

// ============================================================================
// MANET WAVEFORM FUNCTION PROTOTYPES
// ============================================================================

// Static pool management
void init_message_pool(void);
ApplicationMessage* get_free_message(void);
void release_message(ApplicationMessage* msg);

// Static application packet pool management  
void init_app_packet_pool(void);
CustomApplicationPacket* get_free_app_packet(void);
void release_app_packet(CustomApplicationPacket* packet);

// PHY layer integration
void update_phy_metrics_for_node(uint8_t node_id);
bool is_link_quality_good(uint8_t node_id);

// OLSR NC queue management
void init_olsr_nc_queue(void);
bool enqueue_olsr_nc_packet(uint8_t *olsr_payload, size_t payload_len, uint8_t source_node, uint8_t assigned_slot);

// Relay queue management functions
void init_relay_queue(void);
bool enqueue_relay_packet(struct frame *relay_frame);
struct frame dequeue_relay_packet(void);
bool should_relay_packet(struct frame *frame);
bool is_packet_for_self(struct frame *frame);
void print_relay_stats(void);

// TDMA API functions for relay queue access
struct frame rrc_tdma_dequeue_relay_packet(void);
bool rrc_has_relay_packets(void);

// RRC configuration functions
void rrc_set_node_id(uint8_t node_id);
uint8_t rrc_get_node_id(void);

// FSM functions
void init_rrc_fsm(void);
const char* rrc_state_to_string(RRC_SystemState state);
void rrc_transition_to_state(RRC_SystemState new_state, uint8_t dest_node);
RRC_ConnectionContext* rrc_get_connection_context(uint8_t dest_node);
RRC_ConnectionContext* rrc_create_connection_context(uint8_t dest_node);
void rrc_release_connection_context(uint8_t dest_node);
void rrc_update_connection_activity(uint8_t dest_node);

// FSM event handlers
int rrc_handle_power_on(void);
int rrc_handle_power_off(void);
int rrc_handle_data_request(uint8_t dest_node, MessagePriority qos);
int rrc_handle_route_and_slots_allocated(uint8_t dest_node, uint8_t next_hop);
int rrc_handle_route_change(uint8_t dest_node, uint8_t new_next_hop);
int rrc_handle_reconfig_success(uint8_t dest_node, uint8_t new_next_hop);
int rrc_handle_inactivity_timeout(uint8_t dest_node);
int rrc_handle_release_complete(uint8_t dest_node);

// System management
void rrc_periodic_system_management(void);
void print_rrc_fsm_stats(void);

// MANET Waveform Core Functions (New Requirements)
// NC Slot Management (Section A.1)
void init_nc_slot_manager(void);
uint8_t rrc_get_my_nc_slot(void);
bool rrc_is_my_nc_slot(uint8_t slot);
uint8_t rrc_map_slot_to_nc_index(uint8_t frame, uint8_t slot);
void rrc_update_active_nodes(uint16_t nodeID);
uint8_t rrc_assign_nc_slot(uint16_t nodeID);

// Neighbor State Management (Section A.4)
void init_neighbor_state_table(void);
NeighborState* rrc_get_neighbor_state(uint16_t nodeID);
NeighborState* rrc_create_neighbor_state(uint16_t nodeID);
void rrc_update_neighbor_slots(uint16_t nodeID, uint8_t *txSlots, uint8_t *rxSlots);
bool rrc_is_neighbor_tx(uint16_t nodeID, uint8_t slot);
bool rrc_is_neighbor_rx(uint16_t nodeID, uint8_t slot);

// Slot Status Management (Section A.3)
void rrc_init_slot_status(void);
void rrc_update_nc_status_bitmap(uint8_t ncSlot, bool active);
void rrc_update_du_gu_usage_bitmap(uint8_t slot, bool willTx);
void rrc_generate_slot_status(SlotStatus *out);

// Piggyback TLV Management (Section A.2)
void rrc_init_piggyback_tlv(void);
void rrc_build_piggyback_tlv(PiggybackTLV *tlv);
bool rrc_parse_piggyback_tlv(const uint8_t *data, size_t len, PiggybackTLV *tlv);
void rrc_update_piggyback_ttl(void);

// NC Frame Building (Section A.2)
size_t rrc_build_nc_frame(uint8_t *buffer, size_t maxLen);

// Relay Handling (Section A.5)
bool rrc_should_relay(struct frame *frame);
void rrc_enqueue_relay_packet(struct frame *frame);

// Uplink processing functions
int rrc_process_uplink_frame(struct frame *received_frame);
int forward_olsr_packet_to_l3(struct frame *l3_frame);
int deliver_data_packet_to_l7(struct frame *app_frame);
int rrc_deliver_to_application_layer(const CustomApplicationPacket *packet);
CustomApplicationPacket* convert_frame_to_app_packet(const struct frame *frame);
void generate_slot_assignment_failure_message(uint8_t node_id);

// Application feedback functions
void notify_application_of_failure(uint8_t dest_node, const char* reason);
void notify_successful_delivery(uint8_t dest_node, uint32_t sequence_number);

// ============================================================================
// RRC EXTENSION FUNCTION PROTOTYPES (REQUIREMENTS 1, 2, 3)
// ============================================================================

// Requirement 1: Piggyback support functions
void rrc_initialize_piggyback_system(void);
void rrc_initialize_piggyback(uint8_t node_id, uint8_t session_id, uint8_t traffic_type, uint8_t reserved_slot);
void rrc_clear_piggyback(void);
void rrc_update_piggyback_ttl(void);
bool rrc_should_attach_piggyback(void);
PiggybackTLV* rrc_get_piggyback_data(void);
void rrc_check_start_end_packets(const CustomApplicationPacket *packet);

// Requirement 2: Slot status reporting functions
// Structure for slot status reporting
typedef struct {
    uint8_t slot_number;
    uint8_t usage_status;    // 0=FREE, 1=ALLOCATED, 2=RESERVED, 3=COLLISION
    uint8_t assigned_node;   // Node currently using the slot (0 if free)
    uint8_t traffic_type;    // 1=Voice, 2=Video, 3=Data
    uint8_t priority;        // 1=High, 2=Medium, 3=Low
} SlotStatusInfo;

void rrc_generate_slot_status(SlotStatusInfo slot_status[10]);

// Requirement 3: NC slot allocation functions
void rrc_update_nc_schedule(void);
uint8_t rrc_get_current_nc_slot(void);

// ============================================================================
// RRC FSM STATE MANAGEMENT FUNCTIONS
// ============================================================================

// Initialize FSM system
// ============================================================================
// MANET WAVEFORM CORE IMPLEMENTATIONS
// ============================================================================

// Initialize NC Slot Manager (Section A.1)
void init_nc_slot_manager(void) {
    nc_manager.activeNodeCount = 0;
    nc_manager.supercycleCounter = 0;
    nc_manager.currentFrame = 0;
    nc_manager.currentSlot = 0;
    nc_manager.myAssignedNCSlot = rrc_assign_nc_slot(rrc_node_id);
    
    for (int i = 0; i < MAX_MONITORED_NODES; i++) {
        nc_manager.activeNodes[i] = 0;
    }
    
    printf("RRC: NC Slot Manager initialized - My NC slot: %u\n", nc_manager.myAssignedNCSlot);
}

// Get my assigned NC slot (Section A.1)
uint8_t rrc_get_my_nc_slot(void) {
    return nc_manager.myAssignedNCSlot;
}

// Check if given slot is my NC slot (Section A.1)
bool rrc_is_my_nc_slot(uint8_t slot) {
    return (slot == nc_manager.myAssignedNCSlot);
}

// Map frame and slot to NC index (Section A.1)
uint8_t rrc_map_slot_to_nc_index(uint8_t frame, uint8_t slot) {
    if (slot < 8 || slot > 9) return 0; // Invalid NC slot
    
    // NC slots are in slots 8-9 of each frame
    // Frame 0-9, each has 2 NC slots = 20 NC slots per cycle
    // 2 cycles = 40 NC slots total
    uint8_t cycle = nc_manager.supercycleCounter % 2;
    uint8_t nc_index = (cycle * 20) + (frame * 2) + (slot - 8) + 1;
    
    return (nc_index > 40) ? (nc_index % 40) + 1 : nc_index;
}

// Update active nodes list (Section A.1)
void rrc_update_active_nodes(uint16_t nodeID) {
    // Check if node already in active list
    for (int i = 0; i < nc_manager.activeNodeCount; i++) {
        if (nc_manager.activeNodes[i] == nodeID) {
            return; // Already active
        }
    }
    
    // Add new active node
    if (nc_manager.activeNodeCount < MAX_MONITORED_NODES) {
        nc_manager.activeNodes[nc_manager.activeNodeCount] = nodeID;
        nc_manager.activeNodeCount++;
        printf("RRC: Added active node %u (total: %u)\n", nodeID, nc_manager.activeNodeCount);
    }
}

// Assign NC slot using MANET algorithm (Section A.1)
uint8_t rrc_assign_nc_slot(uint16_t nodeID) {
    if (nc_manager.activeNodeCount < 40) {
        // Compact assignment NC1..NCn
        return (nodeID % nc_manager.activeNodeCount) + 1;
    } else {
        // Use modulo 40 (avoid slot 0)
        uint8_t slot = nodeID % 40;
        return (slot == 0) ? 40 : slot;
    }
}

// Initialize Neighbor State Table (Section A.4)
void init_neighbor_state_table(void) {
    for (int i = 0; i < MAX_MONITORED_NODES; i++) {
        neighbor_table[i].nodeID = 0;
        neighbor_table[i].lastHeardTime = 0;
        neighbor_table[i].active = false;
        neighbor_table[i].assignedNCSlot = 0;
        neighbor_table[i].capabilities = 0;
        
        for (int j = 0; j < 10; j++) {
            neighbor_table[i].txSlots[j] = 0;
            neighbor_table[i].rxSlots[j] = 0;
        }
        
        neighbor_table[i].phy.rssi_dbm = 0.0f;
        neighbor_table[i].phy.snr_db = 0.0f;
        neighbor_table[i].phy.per_percent = 0.0f;
        neighbor_table[i].phy.packet_count = 0;
        neighbor_table[i].phy.last_update_time = 0;
    }
    
    neighbor_count = 0;
    printf("RRC: Neighbor state table initialized\n");
}

// Get neighbor state by node ID (Section A.4)
NeighborState* rrc_get_neighbor_state(uint16_t nodeID) {
    for (int i = 0; i < neighbor_count; i++) {
        if (neighbor_table[i].nodeID == nodeID && neighbor_table[i].active) {
            return &neighbor_table[i];
        }
    }
    return NULL;
}

// Create new neighbor state entry (Section A.4)
NeighborState* rrc_create_neighbor_state(uint16_t nodeID) {
    // Check if already exists
    NeighborState* existing = rrc_get_neighbor_state(nodeID);
    if (existing) return existing;
    
    // Find free slot
    if (neighbor_count < MAX_MONITORED_NODES) {
        NeighborState* new_neighbor = &neighbor_table[neighbor_count];
        new_neighbor->nodeID = nodeID;
        new_neighbor->active = true;
        new_neighbor->lastHeardTime = (uint64_t)time(NULL);
        new_neighbor->assignedNCSlot = rrc_assign_nc_slot(nodeID);
        
        neighbor_count++;
        rrc_update_active_nodes(nodeID);
        
        printf("RRC: Created neighbor state for node %u (NC slot %u)\n", 
               nodeID, new_neighbor->assignedNCSlot);
        
        return new_neighbor;
    }
    
    return NULL;
}

// Update neighbor slot assignments (Section A.4)
void rrc_update_neighbor_slots(uint16_t nodeID, uint8_t *txSlots, uint8_t *rxSlots) {
    NeighborState* neighbor = rrc_get_neighbor_state(nodeID);
    if (!neighbor) {
        neighbor = rrc_create_neighbor_state(nodeID);
        if (!neighbor) return;
    }
    
    // Update TX slots
    if (txSlots) {
        for (int i = 0; i < 10; i++) {
            neighbor->txSlots[i] = txSlots[i];
        }
    }
    
    // Update RX slots  
    if (rxSlots) {
        for (int i = 0; i < 10; i++) {
            neighbor->rxSlots[i] = rxSlots[i];
        }
    }
    
    neighbor->lastHeardTime = (uint64_t)time(NULL);
    
    printf("RRC: Updated slot assignments for neighbor %u\n", nodeID);
}

// Check if neighbor will transmit in given slot (Section A.4)
bool rrc_is_neighbor_tx(uint16_t nodeID, uint8_t slot) {
    if (slot >= 10) return false;
    
    NeighborState* neighbor = rrc_get_neighbor_state(nodeID);
    if (!neighbor) return false;
    
    return (neighbor->txSlots[slot] != 0);
}

// Check if neighbor expects to receive in given slot (Section A.4)
bool rrc_is_neighbor_rx(uint16_t nodeID, uint8_t slot) {
    if (slot >= 10) return false;
    
    NeighborState* neighbor = rrc_get_neighbor_state(nodeID);
    if (!neighbor) return false;
    
    return (neighbor->rxSlots[slot] != 0);
}

// Initialize Slot Status System (Section A.3)
void rrc_init_slot_status(void) {
    current_slot_status.ncStatusBitmap = 0;
    current_slot_status.duGuUsageBitmap = 0;
    current_slot_status.lastUpdateTime = (uint32_t)time(NULL);
    
    printf("RRC: Slot status system initialized\n");
}

// Update NC status bitmap (Section A.3)
void rrc_update_nc_status_bitmap(uint8_t ncSlot, bool active) {
    if (ncSlot == 0 || ncSlot > 40) return;
    
    uint64_t mask = 1ULL << (ncSlot - 1);
    
    if (active) {
        current_slot_status.ncStatusBitmap |= mask;
    } else {
        current_slot_status.ncStatusBitmap &= ~mask;
    }
    
    current_slot_status.lastUpdateTime = (uint32_t)time(NULL);
}

// Update DU/GU usage bitmap (Section A.3)
void rrc_update_du_gu_usage_bitmap(uint8_t slot, bool willTx) {
    if (slot >= 60) return;
    
    uint64_t mask = 1ULL << slot;
    
    if (willTx) {
        current_slot_status.duGuUsageBitmap |= mask;
    } else {
        current_slot_status.duGuUsageBitmap &= ~mask;
    }
}

// Generate slot status (Section A.3)
void rrc_generate_slot_status(SlotStatus *out) {
    if (!out) return;
    
    *out = current_slot_status;
    
    printf("RRC: Generated slot status - NC bitmap: 0x%016llX, DU/GU bitmap: 0x%016llX\n",
           (unsigned long long)out->ncStatusBitmap, (unsigned long long)out->duGuUsageBitmap);
}

// Initialize Piggyback TLV System (Section A.2)
void rrc_init_piggyback_tlv(void) {
    current_piggyback_tlv.type = 0x01;  // RRC Piggyback TLV type
    current_piggyback_tlv.length = sizeof(PiggybackTLV) - 2;
    current_piggyback_tlv.sourceNodeID = rrc_node_id;
    current_piggyback_tlv.sourceReservations = 0;
    current_piggyback_tlv.relayReservations = 0;
    current_piggyback_tlv.duGuIntentionMap = 0;
    current_piggyback_tlv.ncStatusBitmap = 0;
    current_piggyback_tlv.timeSync = (uint32_t)time(NULL);
    current_piggyback_tlv.myNCSlot = nc_manager.myAssignedNCSlot;
    current_piggyback_tlv.ttl = 10;  // 10 frame TTL
    
    printf("RRC: Piggyback TLV system initialized\n");
}

// Build piggyback TLV (Section A.2)
void rrc_build_piggyback_tlv(PiggybackTLV *tlv) {
    if (!tlv) return;
    
    *tlv = current_piggyback_tlv;
    
    // Update dynamic fields
    tlv->timeSync = (uint32_t)time(NULL);
    tlv->ncStatusBitmap = current_slot_status.ncStatusBitmap;
    tlv->duGuIntentionMap = current_slot_status.duGuUsageBitmap;
    
    printf("RRC: Built piggyback TLV for NC slot %u\n", tlv->myNCSlot);
}

// Parse received piggyback TLV (Section A.2)
bool rrc_parse_piggyback_tlv(const uint8_t *data, size_t len, PiggybackTLV *tlv) {
    if (!data || !tlv || len < sizeof(PiggybackTLV)) {
        return false;
    }
    
    if (data[0] != 0x01) {  // Check TLV type
        return false;
    }
    
    memcpy(tlv, data, sizeof(PiggybackTLV));
    
    // Update neighbor state from TLV
    NeighborState* neighbor = rrc_create_neighbor_state(tlv->sourceNodeID);
    if (neighbor) {
        neighbor->lastHeardTime = (uint64_t)time(NULL);
        neighbor->assignedNCSlot = tlv->myNCSlot;
    }
    
    // Update NC status bitmap
    rrc_update_nc_status_bitmap(tlv->myNCSlot, true);
    
    printf("RRC: Parsed piggyback TLV from node %u (NC slot %u)\n", 
           tlv->sourceNodeID, tlv->myNCSlot);
    
    return true;
}

// Update piggyback TTL (Section A.2)
void rrc_update_piggyback_ttl(void) {
    if (current_piggyback_tlv.ttl > 0) {
        current_piggyback_tlv.ttl--;
        
        if (current_piggyback_tlv.ttl == 0) {
            printf("RRC: Piggyback TLV expired\n");
        }
    }
}

// Build complete NC frame (Section A.2)
size_t rrc_build_nc_frame(uint8_t *buffer, size_t maxLen) {
    if (!buffer || maxLen < sizeof(PiggybackTLV)) {
        return 0;
    }
    
    PiggybackTLV tlv;
    rrc_build_piggyback_tlv(&tlv);
    
    // Copy TLV to buffer
    memcpy(buffer, &tlv, sizeof(PiggybackTLV));
    
    printf("RRC: Built NC frame with piggyback TLV (%zu bytes)\n", sizeof(PiggybackTLV));
    
    return sizeof(PiggybackTLV);
}

// Check if packet should be relayed (Section A.5)
bool rrc_should_relay(struct frame *frame) {
    if (!frame) return false;
    
    // Drop if TTL expired
    if (frame->TTL <= 0) return false;
    
    // Don't relay if packet is for this node
    if (frame->dest_add == rrc_node_id) return false;
    
    // Check if OLSR has route
    uint8_t next_hop = olsr_get_next_hop(frame->dest_add);
    if (next_hop == 0) return false;
    
    return true;
}

// Enqueue packet for relay (Section A.5)
void rrc_enqueue_relay_packet(struct frame *frame) {
    if (!frame || !rrc_should_relay(frame)) {
        relay_stats.relay_packets_discarded++;
        return;
    }
    
    // Get new next hop from OLSR
    uint8_t new_next_hop = olsr_get_next_hop(frame->dest_add);
    frame->next_hop_add = new_next_hop;
    
    // Decrement TTL
    frame->TTL--;
    
    // Enqueue to relay queue
    if (!is_full(&rrc_relay_queue)) {
        enqueue(&rrc_relay_queue, *frame);
        relay_stats.relay_packets_enqueued++;
        
        printf("RRC: Relayed packet - Dest: %u, Next hop: %u, TTL: %d\n",
               frame->dest_add, frame->next_hop_add, frame->TTL);
    } else {
        relay_stats.relay_queue_full_drops++;
        printf("RRC: Relay queue full, dropped packet\n");
    }
}

// Initialize RRC FSM system  
void init_rrc_fsm(void) {
    if (fsm_initialized) return;
    
    current_rrc_state = RRC_STATE_NULL;
    
    // Initialize connection pool
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        connection_pool[i].active = false;
        connection_pool[i].dest_node_id = 0;
        connection_pool[i].next_hop_id = 0;
        connection_pool[i].connection_state = RRC_STATE_NULL;
        connection_pool[i].setup_pending = false;
        connection_pool[i].reconfig_pending = false;
        memset(connection_pool[i].allocated_slots, 0, sizeof(connection_pool[i].allocated_slots));
    }
    
    fsm_initialized = true;
    
    // MANET: Initialize all subsystems
    init_nc_slot_manager();
    init_neighbor_state_table();
    rrc_init_slot_status();
    rrc_init_piggyback_tlv();
    
    printf("RRC: FSM system initialized with MANET waveform extensions\n");
}

// State to string conversion
const char* rrc_state_to_string(RRC_SystemState state) {
    switch (state) {
        case RRC_STATE_NULL: return "NULL";
        case RRC_STATE_IDLE: return "IDLE";
        case RRC_STATE_CONNECTION_SETUP: return "CONNECTION_SETUP";
        case RRC_STATE_CONNECTED: return "CONNECTED";
        case RRC_STATE_RECONFIGURATION: return "RECONFIGURATION";
        case RRC_STATE_RELEASE: return "RELEASE";
        default: return "UNKNOWN";
    }
}

// State transition with logging
void rrc_transition_to_state(RRC_SystemState new_state, uint8_t dest_node) {
    RRC_SystemState old_state = current_rrc_state;
    
    printf("RRC: FSM State transition: %s → %s (Node %u)\n",
           rrc_state_to_string(old_state), rrc_state_to_string(new_state), dest_node);
    
    current_rrc_state = new_state;
    rrc_fsm_stats.state_transitions++;
}

// Validate state transition
bool rrc_is_state_transition_valid(RRC_SystemState from, RRC_SystemState to) {
    switch (from) {
        case RRC_STATE_NULL:
            return (to == RRC_STATE_IDLE);
        case RRC_STATE_IDLE:
            return (to == RRC_STATE_CONNECTION_SETUP || to == RRC_STATE_NULL);
        case RRC_STATE_CONNECTION_SETUP:
            return (to == RRC_STATE_CONNECTED || to == RRC_STATE_IDLE);
        case RRC_STATE_CONNECTED:
            return (to == RRC_STATE_RECONFIGURATION || to == RRC_STATE_RELEASE);
        case RRC_STATE_RECONFIGURATION:
            return (to == RRC_STATE_CONNECTED || to == RRC_STATE_IDLE);
        case RRC_STATE_RELEASE:
            return (to == RRC_STATE_IDLE);
        default:
            return false;
    }
}

// ============================================================================
// CONNECTION CONTEXT MANAGEMENT (Static Pool)
// ============================================================================

// Get connection context for destination node
RRC_ConnectionContext* rrc_get_connection_context(uint8_t dest_node) {
    if (!fsm_initialized) init_rrc_fsm();
    
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (connection_pool[i].active && connection_pool[i].dest_node_id == dest_node) {
            return &connection_pool[i];
        }
    }
    return NULL;
}

// Create new connection context
RRC_ConnectionContext* rrc_create_connection_context(uint8_t dest_node) {
    if (!fsm_initialized) init_rrc_fsm();
    
    // Check if context already exists
    RRC_ConnectionContext* existing = rrc_get_connection_context(dest_node);
    if (existing) {
        printf("RRC: Connection context already exists for node %u\n", dest_node);
        return existing;
    }
    
    // Find free slot
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (!connection_pool[i].active) {
            connection_pool[i].active = true;
            connection_pool[i].dest_node_id = dest_node;
            connection_pool[i].next_hop_id = 0;
            connection_pool[i].connection_start_time = (uint32_t)time(NULL);
            connection_pool[i].last_activity_time = (uint32_t)time(NULL);
            connection_pool[i].connection_state = RRC_STATE_CONNECTION_SETUP;
            connection_pool[i].setup_pending = true;
            connection_pool[i].reconfig_pending = false;
            memset(connection_pool[i].allocated_slots, 0, sizeof(connection_pool[i].allocated_slots));
            
            printf("RRC: Created connection context for node %u (slot %d)\n", dest_node, i);
            return &connection_pool[i];
        }
    }
    
    printf("RRC: ERROR - Connection pool exhausted, cannot create context for node %u\n", dest_node);
    return NULL;
}

// Release connection context
void rrc_release_connection_context(uint8_t dest_node) {
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (ctx) {
        printf("RRC: Releasing connection context for node %u\n", dest_node);
        ctx->active = false;
        ctx->dest_node_id = 0;
        ctx->setup_pending = false;
        ctx->reconfig_pending = false;
    }
}

// Update connection activity timestamp
void rrc_update_connection_activity(uint8_t dest_node) {
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (ctx) {
        ctx->last_activity_time = (uint32_t)time(NULL);
    }
}

// ============================================================================
// FSM EVENT HANDLERS
// ============================================================================

// NULL → IDLE: Power on and register with OLSR
int rrc_handle_power_on(void) {
    if (!fsm_initialized) init_rrc_fsm();
    
    if (current_rrc_state != RRC_STATE_NULL) {
        printf("RRC: WARNING - Power on event in state %s\n", rrc_state_to_string(current_rrc_state));
        return -1;
    }
    
    printf("RRC: Power on - Initializing RRC and registering with OLSR\n");
    
    // Initialize message pool and other components
    init_message_pool();
    init_app_packet_pool();
    init_neighbor_tracking();
    
    // Transition to IDLE state
    rrc_transition_to_state(RRC_STATE_IDLE, 0);
    rrc_fsm_stats.power_on_events++;
    
    printf("RRC: System ready - Node registered and waiting for data requests\n");
    return 0;
}

// IDLE → NULL: Power off and cleanup
int rrc_handle_power_off(void) {
    if (current_rrc_state != RRC_STATE_IDLE) {
        printf("RRC: WARNING - Power off event in state %s\n", rrc_state_to_string(current_rrc_state));
        return -1;
    }
    
    printf("RRC: Power off - Cleaning up all connections and resources\n");
    
    // Release all active connections
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (connection_pool[i].active) {
            rrc_release_connection_context(connection_pool[i].dest_node_id);
        }
    }
    
    // Transition to NULL state
    rrc_transition_to_state(RRC_STATE_NULL, 0);
    rrc_fsm_stats.power_off_events++;
    
    return 0;
}

// IDLE → CONNECTION_SETUP: Data request triggers setup
int rrc_handle_data_request(uint8_t dest_node, MessagePriority qos) {
    if (current_rrc_state != RRC_STATE_IDLE) {
        printf("RRC: WARNING - Data request in state %s\n", rrc_state_to_string(current_rrc_state));
        return -1;
    }
    
    printf("RRC: Data request for node %u with QoS priority %d\n", dest_node, qos);
    
    // Create connection context
    RRC_ConnectionContext* ctx = rrc_create_connection_context(dest_node);
    if (!ctx) {
        printf("RRC: ERROR - Cannot create connection context for node %u\n", dest_node);
        return -1;
    }
    
    ctx->qos_priority = qos;
    
    // Query OLSR for route (external API call)
    uint8_t next_hop = olsr_get_next_hop(dest_node);
    if (next_hop == 0) {
        printf("RRC: No route available, triggering route discovery\n");
        olsr_trigger_route_discovery(dest_node);
        rrc_stats.route_discoveries_triggered++;
        // Keep context in CONNECTION_SETUP state waiting for route
    } else {
        ctx->next_hop_id = next_hop;
        printf("RRC: Route found via next hop %u\n", next_hop);
    }
    
    // Transition to CONNECTION_SETUP
    rrc_transition_to_state(RRC_STATE_CONNECTION_SETUP, dest_node);
    rrc_fsm_stats.connection_setups++;
    
    return 0;
}

// CONNECTION_SETUP → CONNECTED: Route and slots allocated
int rrc_handle_route_and_slots_allocated(uint8_t dest_node, uint8_t next_hop) {
    if (current_rrc_state != RRC_STATE_CONNECTION_SETUP) {
        printf("RRC: WARNING - Route allocation in state %s\n", rrc_state_to_string(current_rrc_state));
        return -1;
    }
    
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (!ctx) {
        printf("RRC: ERROR - No connection context for node %u\n", dest_node);
        return -1;
    }
    
    // Check TDMA slot availability (external API call)
    if (!tdma_check_slot_available(next_hop, ctx->qos_priority)) {
        printf("RRC: ERROR - No TDMA slots available for node %u\n", dest_node);
        // Return to IDLE state
        rrc_transition_to_state(RRC_STATE_IDLE, dest_node);
        rrc_release_connection_context(dest_node);
        return -1;
    }
    
    // Update connection context
    ctx->next_hop_id = next_hop;
    ctx->setup_pending = false;
    ctx->connection_state = RRC_STATE_CONNECTED;
    
    // Transition to CONNECTED
    rrc_transition_to_state(RRC_STATE_CONNECTED, dest_node);
    
    printf("RRC: Connection established - Node %u via next hop %u\n", dest_node, next_hop);
    return 0;
}

// CONNECTED → RECONFIGURATION: Route change detected
int rrc_handle_route_change(uint8_t dest_node, uint8_t new_next_hop) {
    if (current_rrc_state != RRC_STATE_CONNECTED) {
        printf("RRC: WARNING - Route change in state %s\n", rrc_state_to_string(current_rrc_state));
        return -1;
    }
    
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (!ctx) {
        printf("RRC: ERROR - No connection context for node %u\n", dest_node);
        return -1;
    }
    
    printf("RRC: Route change detected - Node %u: %u → %u\n", 
           dest_node, ctx->next_hop_id, new_next_hop);
    
    // Trigger route discovery for verification (external API call)
    olsr_trigger_route_discovery(dest_node);
    rrc_stats.route_discoveries_triggered++;
    
    // Set reconfiguration pending
    ctx->reconfig_pending = true;
    ctx->connection_state = RRC_STATE_RECONFIGURATION;
    
    // Transition to RECONFIGURATION
    rrc_transition_to_state(RRC_STATE_RECONFIGURATION, dest_node);
    rrc_fsm_stats.reconfigurations++;
    
    return 0;
}

// RECONFIGURATION → CONNECTED: Successful reconfiguration
int rrc_handle_reconfig_success(uint8_t dest_node, uint8_t new_next_hop) {
    if (current_rrc_state != RRC_STATE_RECONFIGURATION) {
        printf("RRC: WARNING - Reconfig success in state %s\n", rrc_state_to_string(current_rrc_state));
        return -1;
    }
    
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (!ctx) {
        printf("RRC: ERROR - No connection context for node %u\n", dest_node);
        return -1;
    }
    
    // Update connection with new route
    ctx->next_hop_id = new_next_hop;
    ctx->reconfig_pending = false;
    ctx->connection_state = RRC_STATE_CONNECTED;
    
    // Transition back to CONNECTED
    rrc_transition_to_state(RRC_STATE_CONNECTED, dest_node);
    
    printf("RRC: Reconfiguration successful - Node %u now via next hop %u\n", 
           dest_node, new_next_hop);
    return 0;
}

// Any state → IDLE: Handle inactivity timeout
int rrc_handle_inactivity_timeout(uint8_t dest_node) {
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (!ctx) {
        return 0; // No active connection
    }
    
    uint32_t current_time = (uint32_t)time(NULL);
    uint32_t inactivity_time = current_time - ctx->last_activity_time;
    
    if (inactivity_time >= RRC_INACTIVITY_TIMEOUT_SEC) {
        printf("RRC: Inactivity timeout for node %u (%u seconds)\n", dest_node, inactivity_time);
        
        // Transition to RELEASE then IDLE
        rrc_transition_to_state(RRC_STATE_RELEASE, dest_node);
        rrc_transition_to_state(RRC_STATE_IDLE, dest_node);
        
        rrc_release_connection_context(dest_node);
        rrc_fsm_stats.inactivity_releases++;
        
        return 1; // Released
    }
    
    return 0; // Still active
}

// CONNECTED/RELEASE → IDLE: Release complete
int rrc_handle_release_complete(uint8_t dest_node) {
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (ctx) {
        printf("RRC: Release complete for node %u\n", dest_node);
        rrc_release_connection_context(dest_node);
        rrc_fsm_stats.connection_releases++;
    }
    
    // Transition to IDLE
    rrc_transition_to_state(RRC_STATE_IDLE, dest_node);
    return 0;
}

// ============================================================================
// PERIODIC SYSTEM MANAGEMENT
// ============================================================================

// Periodic cleanup and timeout checking
void rrc_periodic_system_management(void) {
    if (!fsm_initialized) return;
    
    uint32_t current_time = (uint32_t)time(NULL);
    
    // Check all active connections for timeouts
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (connection_pool[i].active) {
            uint8_t dest_node = connection_pool[i].dest_node_id;
            
            // Check setup timeout
            if (connection_pool[i].setup_pending) {
                uint32_t setup_time = current_time - connection_pool[i].connection_start_time;
                if (setup_time >= RRC_SETUP_TIMEOUT_SEC) {
                    printf("RRC: Setup timeout for node %u (%u seconds)\n", dest_node, setup_time);
                    rrc_transition_to_state(RRC_STATE_IDLE, dest_node);
                    rrc_release_connection_context(dest_node);
                    rrc_fsm_stats.setup_timeouts++;
                    continue;
                }
            }
            
            // Check inactivity timeout
            rrc_handle_inactivity_timeout(dest_node);
        }
    }
    
    // Cleanup stale neighbors periodically
    cleanup_stale_neighbors();
    
    // EXTENSION: Periodic piggyback TTL management (Requirement 1)
    rrc_update_piggyback_ttl();
    
    // EXTENSION: Update NC slot allocation round-robin (Requirement 3)
    rrc_update_nc_schedule();
}

// ============================================================================
// TDMA TRANSFER/RECEIVE CONTROL INTEGRATION
// ============================================================================

// Request transmit slot through TDMA team API
int rrc_request_transmit_slot(uint8_t dest_node, MessagePriority priority) {
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (!ctx || ctx->connection_state != RRC_STATE_CONNECTED) {
        printf("RRC: Cannot request transmit slot - no active connection to node %u\n", dest_node);
        return -1;
    }
    
    // Check TDMA slot availability (external API call)
    if (!tdma_check_slot_available(ctx->next_hop_id, priority)) {
        printf("RRC: No transmit slots available for node %u (priority %d)\n", dest_node, priority);
        return -1;
    }
    
    printf("RRC: Transmit slot available for node %u via next hop %u\n", dest_node, ctx->next_hop_id);
    return 0;
}

// Confirm transmit slot allocation
int rrc_confirm_transmit_slot(uint8_t dest_node, uint8_t slot_id) {
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (!ctx) {
        printf("RRC: Cannot confirm transmit slot - no connection context for node %u\n", dest_node);
        return -1;
    }
    
    // Store allocated slot in connection context
    for (int i = 0; i < 4; i++) {
        if (ctx->allocated_slots[i] == 0) {
            ctx->allocated_slots[i] = slot_id;
            printf("RRC: Confirmed transmit slot %u for node %u\n", slot_id, dest_node);
            return 0;
        }
    }
    
    printf("RRC: WARNING - All slot positions used for node %u\n", dest_node);
    return -1;
}

// Release transmit slot
void rrc_release_transmit_slot(uint8_t dest_node, uint8_t slot_id) {
    RRC_ConnectionContext* ctx = rrc_get_connection_context(dest_node);
    if (ctx) {
        for (int i = 0; i < 4; i++) {
            if (ctx->allocated_slots[i] == slot_id) {
                ctx->allocated_slots[i] = 0;
                printf("RRC: Released transmit slot %u for node %u\n", slot_id, dest_node);
                return;
            }
        }
    }
}

// Setup receive slot for incoming frames
int rrc_setup_receive_slot(uint8_t source_node) {
    printf("RRC: Setting up receive slot for source node %u\n", source_node);
    
    // Update PHY metrics for source node
    update_phy_metrics_for_node(source_node);
    
    // Check link quality
    if (!is_link_quality_good(source_node)) {
        printf("RRC: WARNING - Poor link quality from source node %u\n", source_node);
    }
    
    return 0;
}

// Handle received frame
void rrc_handle_received_frame(struct frame *received_frame) {
    if (!received_frame) return;
    
    printf("RRC: Received frame from node %u to node %u (type %d, priority %d)\n",
           received_frame->source_add, received_frame->dest_add, 
           received_frame->data_type, received_frame->priority);
    
    // Update activity for source node if we have a connection
    rrc_update_connection_activity(received_frame->source_add);
    
    // Update PHY metrics
    update_phy_metrics_for_node(received_frame->source_add);
}

// Cleanup receive resources
void rrc_cleanup_receive_resources(uint8_t source_node) {
    printf("RRC: Cleaning up receive resources for source node %u\n", source_node);
}

// ============================================================================
// FSM STATISTICS AND MONITORING
// ============================================================================

void print_rrc_fsm_stats(void) {
    printf("\n=== RRC FSM Statistics ===\n");
    printf("Current state: %s\n", rrc_state_to_string(current_rrc_state));
    printf("State transitions: %u\n", rrc_fsm_stats.state_transitions);
    printf("Connection setups: %u\n", rrc_fsm_stats.connection_setups);
    printf("Connection releases: %u\n", rrc_fsm_stats.connection_releases);
    printf("Reconfigurations: %u\n", rrc_fsm_stats.reconfigurations);
    printf("Setup timeouts: %u\n", rrc_fsm_stats.setup_timeouts);
    printf("Inactivity releases: %u\n", rrc_fsm_stats.inactivity_releases);
    printf("Power events: %u on, %u off\n", rrc_fsm_stats.power_on_events, rrc_fsm_stats.power_off_events);
    
    printf("\nActive connections:\n");
    int active_count = 0;
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (connection_pool[i].active) {
            printf("  Slot %d: Node %u → %u (state: %s)\n", i,
                   connection_pool[i].dest_node_id, connection_pool[i].next_hop_id,
                   rrc_state_to_string(connection_pool[i].connection_state));
            active_count++;
        }
    }
    if (active_count == 0) {
        printf("  No active connections\n");
    }
    printf("==========================\n\n");
}

void init_message_pool(void) {
    if (pool_initialized) return;
    
    for (int i = 0; i < RRC_MESSAGE_POOL_SIZE; i++) {
        message_pool[i].in_use = false;
        message_pool[i].node_id = 0;
        message_pool[i].dest_node_id = 0;
        message_pool[i].data_size = 0;
        message_pool[i].preemption_allowed = false;
    }
    pool_initialized = true;
    
    // Initialize OLSR NC queue
    init_olsr_nc_queue();
    
    // Initialize Relay queue
    init_relay_queue();
    
    printf("RRC: Message pool initialized (%d messages)\n", RRC_MESSAGE_POOL_SIZE);
}

ApplicationMessage* get_free_message(void) {
    if (!pool_initialized) init_message_pool();
    
    for (int i = 0; i < RRC_MESSAGE_POOL_SIZE; i++) {
        if (!message_pool[i].in_use) {
            message_pool[i].in_use = true;
            memset(message_pool[i].data, 0, PAYLOAD_SIZE_BYTES);
            message_pool[i].data_size = 0;
            return &message_pool[i];
        }
    }
    printf("RRC: ERROR - Message pool exhausted\n");
    rrc_stats.messages_discarded_no_slots++;
    return NULL;
}

void release_message(ApplicationMessage* msg) {
    if (!msg) return;
    
    // Verify message is from our pool
    if (msg >= message_pool && msg < (message_pool + RRC_MESSAGE_POOL_SIZE)) {
        msg->in_use = false;
        msg->data_size = 0;
    }
}

// Application packet pool management
void init_app_packet_pool(void) {
    if (app_packet_pool_initialized) return;
    
    for (int i = 0; i < RRC_APP_PACKET_POOL_SIZE; i++) {
        app_packet_pool[i].in_use = false;
        memset(&app_packet_pool[i].packet, 0, sizeof(CustomApplicationPacket));
    }
    app_packet_pool_initialized = true;
    
    printf("RRC: Application packet pool initialized (%d packets)\n", RRC_APP_PACKET_POOL_SIZE);
}

CustomApplicationPacket* get_free_app_packet(void) {
    if (!app_packet_pool_initialized) init_app_packet_pool();
    
    for (int i = 0; i < RRC_APP_PACKET_POOL_SIZE; i++) {
        if (!app_packet_pool[i].in_use) {
            app_packet_pool[i].in_use = true;
            memset(&app_packet_pool[i].packet, 0, sizeof(CustomApplicationPacket));
            return &app_packet_pool[i].packet;
        }
    }
    printf("RRC: ERROR - Application packet pool exhausted\n");
    return NULL;
}

void release_app_packet(CustomApplicationPacket* packet) {
    if (!packet) return;
    
    // Find and release the packet in our pool
    for (int i = 0; i < RRC_APP_PACKET_POOL_SIZE; i++) {
        if (&app_packet_pool[i].packet == packet) {
            app_packet_pool[i].in_use = false;
            memset(&app_packet_pool[i].packet, 0, sizeof(CustomApplicationPacket));
            break;
        }
    }
}

// ============================================================================
// PHY LAYER INTEGRATION FUNCTIONS
// ============================================================================

void update_phy_metrics_for_node(uint8_t node_id) {
    if (node_id == 0) return;
    
    float rssi, snr, per;
    
    // Get metrics from PHY layer (external API call)
    phy_get_link_metrics(node_id, &rssi, &snr, &per);
    bool link_active = phy_is_link_active(node_id);
    uint32_t packet_count = phy_get_packet_count(node_id);
    
    // Use MANET neighbor state instead of separate PHY array
    NeighborState* neighbor = rrc_create_neighbor_state(node_id);
    if (neighbor) {
        neighbor->phy.rssi_dbm = rssi;
        neighbor->phy.snr_db = snr;
        neighbor->phy.per_percent = per;
        neighbor->phy.packet_count = packet_count;
        neighbor->phy.last_update_time = (uint32_t)time(NULL);
        neighbor->active = link_active;
        
        rrc_stats.phy_metrics_updates++;
        
        // Check for poor link quality
        if (rssi < RSSI_POOR_THRESHOLD_DBM || snr < SNR_POOR_THRESHOLD_DB || 
            per > PER_POOR_THRESHOLD_PERCENT) {
            printf("RRC: WARNING - Poor link quality for node %u (RSSI: %.1f, SNR: %.1f, PER: %.1f%%)\n",
                   node_id, rssi, snr, per);
            rrc_stats.poor_links_detected++;
        }
        
        printf("RRC: PHY metrics updated for node %u - RSSI: %.1f dBm, SNR: %.1f dB, PER: %.1f%%\n",
               node_id, rssi, snr, per);
    }
}

// Get link quality for routing decisions
bool is_link_quality_good(uint8_t node_id) {
    NeighborState* neighbor = rrc_get_neighbor_state(node_id);
    if (!neighbor) return false; // No neighbor data
    
    uint32_t age = (uint32_t)time(NULL) - neighbor->phy.last_update_time;
    if (age > LINK_TIMEOUT_SECONDS) return false; // Stale data
    
    return (neighbor->active &&
           neighbor->phy.per_percent <= PER_POOR_THRESHOLD_PERCENT &&
           neighbor->phy.rssi_dbm >= RSSI_POOR_THRESHOLD_DBM &&
           neighbor->phy.snr_db >= SNR_POOR_THRESHOLD_DB);
}

// ============================================================================
// OLSR NC QUEUE MANAGEMENT FUNCTIONS
// ============================================================================

// Initialize OLSR NC queue
void init_olsr_nc_queue(void) {
    rrc_olsr_nc_queue.front = 0;
    rrc_olsr_nc_queue.back = 0;
    olsr_nc_stats.olsr_packets_received = 0;
    olsr_nc_stats.olsr_packets_enqueued = 0;
    olsr_nc_stats.olsr_packets_dequeued = 0;
    olsr_nc_stats.olsr_queue_full_drops = 0;
    olsr_nc_stats.tdma_nc_requests = 0;
    printf("RRC: OLSR NC queue initialized\n");
}

// Enqueue OLSR packet to NC queue
bool enqueue_olsr_nc_packet(uint8_t *olsr_payload, size_t payload_len, uint8_t source_node, uint8_t assigned_slot) {
    if (!olsr_payload || payload_len == 0) {
        printf("RRC: ERROR - Invalid OLSR packet for NC queue\n");
        return false;
    }
    
    if (is_full(&rrc_olsr_nc_queue)) {
        printf("RRC: ERROR - OLSR NC queue is full, dropping packet from node %u\n", source_node);
        olsr_nc_stats.olsr_queue_full_drops++;
        return false;
    }
    
    // Create frame for OLSR packet
    struct frame olsr_frame = {0};
    olsr_frame.source_add = source_node;
    olsr_frame.dest_add = 0;                     // Broadcast/controller
    olsr_frame.next_hop_add = source_node;       // Original sender
    olsr_frame.rx_or_l3 = true;                  // From L3 (OLSR)
    olsr_frame.TTL = (int)assigned_slot;         // Store assigned NC slot
    olsr_frame.priority = PRIORITY_RX_RELAY;     // NC packet priority
    olsr_frame.data_type = DATA_TYPE_SMS;        // Control packet type
    
    // Copy payload with size limit
    size_t copy_size = (payload_len > PAYLOAD_SIZE_BYTES) ? PAYLOAD_SIZE_BYTES : payload_len;
    memcpy(olsr_frame.payload, olsr_payload, copy_size);
    olsr_frame.payload_length_bytes = (int)copy_size;
    
    // EXTENSION: Attach piggyback TLV data if available (Requirement 1)
    PiggybackTLV* piggyback_data = rrc_get_piggyback_data();
    if (piggyback_data) {
        printf("RRC EXTENSION: Attaching piggyback TLV to NC packet from node %u\n", source_node);
        // Note: In real implementation, piggyback would go in frame header or dedicated TLV field
        // For now, we demonstrate the concept by logging the piggyback TLV attachment
        printf("RRC EXTENSION: Piggyback TLV - Source: %u, NC slot: %u, TTL: %u\n",
               piggyback_data->sourceNodeID, piggyback_data->myNCSlot, piggyback_data->ttl);
    }
    
    // Enqueue to NC queue
    enqueue(&rrc_olsr_nc_queue, olsr_frame);
    olsr_nc_stats.olsr_packets_enqueued++;
    
    printf("RRC: OLSR packet from node %u enqueued to NC queue (slot %u, size %u)\n", 
           source_node, assigned_slot, (unsigned int)copy_size);
    
    return true;
}

// API function for TDMA team to dequeue from NC queue
struct frame rrc_tdma_dequeue_nc_packet(uint8_t target_slot) {
    struct frame empty_frame = {0};
    
    if (is_empty(&rrc_olsr_nc_queue)) {
        printf("RRC: TDMA requested NC packet for slot %u, but NC queue is empty\n", target_slot);
        return empty_frame;
    }
    
    // Simple dequeue - returns next available frame
    // For slot-specific dequeue, we'd need to search the queue
    struct frame nc_frame = dequeue(&rrc_olsr_nc_queue);
    olsr_nc_stats.olsr_packets_dequeued++;
    olsr_nc_stats.tdma_nc_requests++;
    
    printf("RRC: TDMA dequeued NC packet for slot %u (assigned slot was %d)\n", 
           target_slot, nc_frame.TTL);
    
    return nc_frame;
}

// Check if NC queue has packets for specific slot
bool rrc_has_nc_packet_for_slot(uint8_t target_slot) {
    (void)target_slot; // Suppress unused parameter warning
    
    if (is_empty(&rrc_olsr_nc_queue)) {
        return false;
    }
    
    // Simple check - just return if queue has any packets
    // For slot-specific check, we'd need to peek into the queue
    return true;
}

// Get NC queue statistics
void print_olsr_nc_stats(void) {
    printf("\n=== OLSR NC Queue Statistics ===\n");
    printf("Packets received: %u\n", olsr_nc_stats.olsr_packets_received);
    printf("Packets enqueued: %u\n", olsr_nc_stats.olsr_packets_enqueued);
    printf("Packets dequeued: %u\n", olsr_nc_stats.olsr_packets_dequeued);
    printf("Queue full drops: %u\n", olsr_nc_stats.olsr_queue_full_drops);
    printf("TDMA NC requests: %u\n", olsr_nc_stats.tdma_nc_requests);
    printf("Queue status: %s\n", is_empty(&rrc_olsr_nc_queue) ? "EMPTY" : "HAS_PACKETS");
    printf("===============================\n\n");
}

// ============================================================================
// RELAY QUEUE MANAGEMENT FUNCTIONS
// ============================================================================

// Initialize Relay queue
void init_relay_queue(void) {
    rrc_relay_queue.front = 0;
    rrc_relay_queue.back = 0;
    relay_stats.relay_packets_received = 0;
    relay_stats.relay_packets_enqueued = 0;
    relay_stats.relay_packets_dequeued = 0;
    relay_stats.relay_packets_discarded = 0;
    relay_stats.relay_queue_full_drops = 0;
    relay_stats.relay_packets_to_self = 0;
    printf("RRC: Relay queue initialized\n");
}

// Check if packet is destined for this node
bool is_packet_for_self(struct frame *frame) {
    if (!frame) return false;
    
    return (frame->dest_add == rrc_node_id);
}

// Determine if packet should be relayed
bool should_relay_packet(struct frame *frame) {
    if (!frame) return false;
    
    // Don't relay if packet is for this node
    if (is_packet_for_self(frame)) return false;
    
    // Don't relay if TTL is expired
    if (frame->TTL <= 1) return false;
    
    // Don't relay OLSR control packets (they go to NC queue)
    if (frame->rx_or_l3 == true) return false;
    
    // Don't relay broadcast packets with high hop count
    if (frame->dest_add == 0 && frame->TTL < 3) return false;
    
    return true;
}

// Enqueue packet to relay queue
bool enqueue_relay_packet(struct frame *relay_frame) {
    if (!relay_frame) {
        printf("RRC: ERROR - NULL relay frame\n");
        return false;
    }
    
    relay_stats.relay_packets_received++;
    
    if (is_full(&rrc_relay_queue)) {
        printf("RRC: ERROR - Relay queue full, dropping packet\n");
        relay_stats.relay_queue_full_drops++;
        relay_stats.relay_packets_discarded++;
        return false;
    }
    
    // Decrement TTL for relay
    relay_frame->TTL--;
    
    // Update next hop for relay (get from OLSR)
    uint8_t new_next_hop = olsr_get_next_hop(relay_frame->dest_add);
    if (new_next_hop == 0) {
        printf("RRC: ERROR - No route available for relay destination %u\n", relay_frame->dest_add);
        relay_stats.relay_packets_discarded++;
        return false;
    }
    
    // Update next hop
    relay_frame->next_hop_add = new_next_hop;
    
    // Enqueue to relay queue
    enqueue(&rrc_relay_queue, *relay_frame);
    relay_stats.relay_packets_enqueued++;
    
    printf("RRC: Packet relayed - Dest: %u, Next hop: %u, TTL: %d\n", 
           relay_frame->dest_add, new_next_hop, relay_frame->TTL);
    
    return true;
}

// Dequeue packet from relay queue
struct frame dequeue_relay_packet(void) {
    struct frame empty_frame = {0};
    
    if (is_empty(&rrc_relay_queue)) {
        return empty_frame;
    }
    
    struct frame relay_frame = dequeue(&rrc_relay_queue);
    relay_stats.relay_packets_dequeued++;
    
    printf("RRC: Relay packet dequeued for transmission (dest: %u, next_hop: %u)\n", 
           relay_frame.dest_add, relay_frame.next_hop_add);
    
    return relay_frame;
}

// Check if relay queue has packets
bool relay_queue_has_packets(void) {
    return !is_empty(&rrc_relay_queue);
}

// Get relay queue statistics
void print_relay_stats(void) {
    printf("\n=== Relay Queue Statistics ===\n");
    printf("Packets received: %u\n", relay_stats.relay_packets_received);
    printf("Packets enqueued: %u\n", relay_stats.relay_packets_enqueued);
    printf("Packets dequeued: %u\n", relay_stats.relay_packets_dequeued);
    printf("Packets to self: %u\n", relay_stats.relay_packets_to_self);
    printf("Packets discarded: %u\n", relay_stats.relay_packets_discarded);
    printf("Queue full drops: %u\n", relay_stats.relay_queue_full_drops);
    printf("Queue status: %s\n", is_empty(&rrc_relay_queue) ? "EMPTY" : "HAS_PACKETS");
    printf("==============================\n\n");
}

// API function for TDMA team to dequeue from relay queue
struct frame rrc_tdma_dequeue_relay_packet(void) {
    struct frame empty_frame = {0};
    
    if (is_empty(&rrc_relay_queue)) {
        return empty_frame;
    }
    
    struct frame relay_frame = dequeue(&rrc_relay_queue);
    relay_stats.relay_packets_dequeued++;
    
    printf("RRC: TDMA dequeued relay packet (dest: %u, next_hop: %u, TTL: %d)\n", 
           relay_frame.dest_add, relay_frame.next_hop_add, relay_frame.TTL);
    
    return relay_frame;
}

// API function for TDMA team to check relay queue status
bool rrc_has_relay_packets(void) {
    return !is_empty(&rrc_relay_queue);
}

// ============================================================================
// RRC CONFIGURATION FUNCTIONS
// ============================================================================

// Set current node ID
void rrc_set_node_id(uint8_t node_id) {
    rrc_node_id = node_id;
    printf("RRC: Node ID set to %u\n", node_id);
}

// Get current node ID
uint8_t rrc_get_node_id(void) {
    return rrc_node_id;
}

// ============================================================================
// CUSTOM PACKET PROCESSING (STRUCT FORMAT - NO JSON)
// ============================================================================

// Priority mapping based on data type
MessagePriority map_data_type_to_priority(RRC_DataType data_type, bool urgent) {
    switch (data_type) {
        case RRC_DATA_TYPE_PTT:
            return PRIORITY_ANALOG_VOICE_PTT;  // Highest priority
        case RRC_DATA_TYPE_VOICE:
            return PRIORITY_DIGITAL_VOICE;     // Second highest
        case RRC_DATA_TYPE_VIDEO:
            return PRIORITY_DATA_1;            // Third priority
        case RRC_DATA_TYPE_FILE:
            return urgent ? PRIORITY_DATA_1 : PRIORITY_DATA_2;  // Variable based on urgency
        case RRC_DATA_TYPE_SMS:
            return urgent ? PRIORITY_DATA_2 : PRIORITY_DATA_3;  // Variable based on urgency
        case RRC_DATA_TYPE_RELAY:
        default:
            return PRIORITY_RX_RELAY;          // Lowest priority
    }
}

const char* data_type_to_string(RRC_DataType type) {
    switch (type) {
        case RRC_DATA_TYPE_SMS: return "SMS";
        case RRC_DATA_TYPE_VOICE: return "VOICE";
        case RRC_DATA_TYPE_VIDEO: return "VIDEO";
        case RRC_DATA_TYPE_FILE: return "FILE";
        case RRC_DATA_TYPE_PTT: return "PTT";
        case RRC_DATA_TYPE_RELAY: return "RELAY";
        default: return "UNKNOWN";
    }
}

// Main packet processing function (replaces JSON parsing)
ApplicationMessage* process_custom_packet(const CustomApplicationPacket *packet) {
    if (!packet) {
        printf("RRC: ERROR - NULL CustomApplicationPacket\n");
        return NULL;
    }
    
    ApplicationMessage *message = get_free_message();
    if (!message) {
        printf("RRC: ERROR - No free message slots\n");
        return NULL;
    }
    
    rrc_stats.packets_processed++;
    
    // Copy packet data to internal message structure
    message->node_id = packet->src_id;
    message->dest_node_id = packet->dest_id;
    message->data_type = packet->data_type;
    message->transmission_type = packet->transmission_type;
    
    // Map data type to priority
    message->priority = map_data_type_to_priority(packet->data_type, packet->urgent);
    message->preemption_allowed = (packet->urgent || packet->data_type == RRC_DATA_TYPE_PTT);
    
    // Copy payload with size limit enforcement
    size_t data_len = packet->data_size;
    if (data_len > PAYLOAD_SIZE_BYTES) {
        data_len = PAYLOAD_SIZE_BYTES; // Truncate to 16 bytes
        printf("RRC: WARNING - Payload truncated to %d bytes\n", PAYLOAD_SIZE_BYTES);
    }
    
    memcpy(message->data, packet->data, data_len);
    message->data_size = data_len;
    
    printf("RRC: Packet processed - Node %u->%u, Type: %s, Priority: %d, Size: %u, Urgent: %s\n",
           message->node_id, message->dest_node_id, data_type_to_string(message->data_type),
           message->priority, (unsigned int)message->data_size, packet->urgent ? "YES" : "NO");
    
    return message;
}

// ============================================================================
// FRAME CREATION AND QUEUE INTEGRATION
// ============================================================================

struct frame create_frame_from_rrc(ApplicationMessage *app_msg, uint8_t next_hop_node) {
    struct frame new_frame = {0}; // Zero-initialize all fields
    
    if (!app_msg) {
        printf("RRC: ERROR - Cannot create frame from NULL ApplicationMessage\n");
        return new_frame;
    }
    
    // Set frame addressing fields
    new_frame.source_add = app_msg->node_id;
    new_frame.dest_add = app_msg->dest_node_id;
    new_frame.next_hop_add = next_hop_node;
    new_frame.rx_or_l3 = false;  // L7 data going down to L2
    new_frame.TTL = 10;          // Default TTL
    new_frame.priority = app_msg->priority;
    
    // Map data type
    switch (app_msg->data_type) {
        case RRC_DATA_TYPE_SMS:
            new_frame.data_type = DATA_TYPE_SMS;
            break;
        case RRC_DATA_TYPE_VOICE:
        case RRC_DATA_TYPE_PTT:
            if (app_msg->priority == PRIORITY_ANALOG_VOICE_PTT) {
                new_frame.data_type = DATA_TYPE_ANALOG_VOICE;
            } else {
                new_frame.data_type = DATA_TYPE_DIGITAL_VOICE;
            }
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
    } else {
        new_frame.payload_length_bytes = 0;
    }
    
    return new_frame;
}

void enqueue_to_appropriate_queue(ApplicationMessage *app_msg, uint8_t next_hop_node) {
    if (!app_msg) {
        printf("RRC: ERROR - Cannot enqueue NULL message\n");
        return;
    }
    
    // Check TDMA slot availability (external API call)
    if (app_msg->priority != PRIORITY_ANALOG_VOICE_PTT) {
        if (!tdma_check_slot_available(next_hop_node, app_msg->priority)) {
            printf("RRC: ERROR - No TDMA slots available for priority %d\n", app_msg->priority);
            
            // Generate slot assignment failure message
            generate_slot_assignment_failure_message(app_msg->dest_node_id);
            
            rrc_stats.messages_discarded_no_slots++;
            release_message(app_msg);
            return;
        }
    }
    
    // Create frame
    struct frame new_frame = create_frame_from_rrc(app_msg, next_hop_node);
    
    // Enqueue to appropriate queue
    switch (app_msg->priority) {
        case PRIORITY_ANALOG_VOICE_PTT:
            enqueue(&analog_voice_queue, new_frame);
            printf("RRC: → Enqueued to analog_voice_queue (PTT)\n");
            break;
        case PRIORITY_DIGITAL_VOICE:
            enqueue(&data_from_l3_queue[0], new_frame);
            printf("RRC: → Enqueued to data_from_l3_queue[0] (Digital Voice)\n");
            break;
        case PRIORITY_DATA_1:
            enqueue(&data_from_l3_queue[1], new_frame);
            printf("RRC: → Enqueued to data_from_l3_queue[1] (Video)\n");
            break;
        case PRIORITY_DATA_2:
            enqueue(&data_from_l3_queue[2], new_frame);
            printf("RRC: → Enqueued to data_from_l3_queue[2] (File)\n");
            break;
        case PRIORITY_DATA_3:
            enqueue(&data_from_l3_queue[3], new_frame);
            printf("RRC: → Enqueued to data_from_l3_queue[3] (SMS)\n");
            break;
        case PRIORITY_RX_RELAY:
        default:
            enqueue(&rx_queue, new_frame);
            printf("RRC: → Enqueued to rx_queue (Relay)\n");
            break;
    }
    
    rrc_stats.messages_enqueued_total++;
    release_message(app_msg);
}

// ============================================================================
// MAIN RRC API FUNCTIONS
// ============================================================================

int send_to_queue_l2_with_routing_and_phy(ApplicationMessage *app_msg) {
    if (!app_msg) {
        printf("RRC: ERROR - NULL ApplicationMessage\n");
        return -1;
    }
    
    // Check FSM state before routing
    if (current_rrc_state == RRC_STATE_NULL) {
        printf("RRC: ERROR - Cannot route in NULL state\n");
        release_message(app_msg);
        return -1;
    }
    
    uint8_t next_hop = 0;
    
    // Get next hop from OLSR team (external API call)
    if (app_msg->transmission_type == TRANSMISSION_UNICAST) {
        next_hop = olsr_get_next_hop(app_msg->dest_node_id);
        
        if (next_hop == 0) {
            printf("RRC: No route to destination %u, triggering route discovery\n", 
                   app_msg->dest_node_id);
            olsr_trigger_route_discovery(app_msg->dest_node_id);
            rrc_stats.route_discoveries_triggered++;
            
            // Notify application of routing failure
            notify_application_of_failure(app_msg->dest_node_id, "No route available");
            
            // Check if we have a connection context and handle route failure
            RRC_ConnectionContext* ctx = rrc_get_connection_context(app_msg->dest_node_id);
            if (ctx && ctx->connection_state == RRC_STATE_CONNECTION_SETUP) {
                // Setup failed due to no route - return to IDLE
                rrc_transition_to_state(RRC_STATE_IDLE, app_msg->dest_node_id);
                rrc_release_connection_context(app_msg->dest_node_id);
            }
            
            release_message(app_msg);
            return -1;
        }
        
        // Check for route changes in existing connections
        RRC_ConnectionContext* ctx = rrc_get_connection_context(app_msg->dest_node_id);
        if (ctx && ctx->connection_state == RRC_STATE_CONNECTED && ctx->next_hop_id != next_hop) {
            printf("RRC: Route change detected for node %u: %u → %u\n", 
                   app_msg->dest_node_id, ctx->next_hop_id, next_hop);
            rrc_handle_route_change(app_msg->dest_node_id, next_hop);
        }
        
        // Update PHY metrics for next hop
        update_phy_metrics_for_node(next_hop);
        
        // Check PHY link quality to next hop
        if (!is_link_quality_good(next_hop)) {
            printf("RRC: Poor link quality to next hop %u, triggering route re-discovery\n", next_hop);
            olsr_trigger_route_discovery(app_msg->dest_node_id);
            rrc_stats.route_discoveries_triggered++;
            
            // If we have a connection, trigger reconfiguration
            if (ctx && ctx->connection_state == RRC_STATE_CONNECTED) {
                rrc_handle_route_change(app_msg->dest_node_id, next_hop);
            }
            
            release_message(app_msg);
            return -1;
        }
    } else {
        // Broadcast/multicast - use destination as next hop
        next_hop = app_msg->dest_node_id;
    }
    
    printf("RRC: Routing decision - Dest: %u, Next hop: %u (PHY quality: %s)\n", 
           app_msg->dest_node_id, next_hop, 
           is_link_quality_good(next_hop) ? "GOOD" : "UNKNOWN");
    
    // Update connection context with successful routing
    RRC_ConnectionContext* ctx = rrc_get_connection_context(app_msg->dest_node_id);
    if (ctx) {
        ctx->next_hop_id = next_hop;
        rrc_update_connection_activity(app_msg->dest_node_id);
    }
    
    // Enqueue to appropriate queue
    enqueue_to_appropriate_queue(app_msg, next_hop);
    
    return 0;
}

int receive_hello_packet_and_forward_to_tdma(uint8_t *hello_payload, size_t payload_len, uint8_t source_node) {
    if (!hello_payload || payload_len == 0) {
        printf("RRC: ERROR - Invalid hello packet\n");
        return -1;
    }
    
    olsr_nc_stats.olsr_packets_received++;
    
    // ✅ IMPLICIT CAPABILITY DETECTION: If we received hello from node → it's TX capable
    printf("RRC: Hello packet from node %u - Inferring TX capability from transmission\n", source_node);
    update_neighbor_capabilities(source_node, true, true);  // TX=true, RX=assume bidirectional
    
    // Update MANET neighbor state for active nodes tracking
    rrc_create_neighbor_state(source_node);
    
    // Request NC slot for hello packet (external TDMA API call)
    uint8_t assigned_slot = 0;
    if (!tdma_request_nc_slot(hello_payload, payload_len, &assigned_slot)) {
        printf("RRC: ERROR - Cannot get NC slot for hello packet from node %u\n", source_node);
        return -1;
    }
    
    // Enqueue OLSR packet to dedicated NC queue
    if (!enqueue_olsr_nc_packet(hello_payload, payload_len, source_node, assigned_slot)) {
        printf("RRC: ERROR - Failed to enqueue OLSR packet to NC queue\n");
        return -1;
    }
    
    rrc_stats.nc_slot_requests++;
    
    printf("RRC: Hello packet from node %u processed and queued for NC slot %u\n",
           source_node, assigned_slot);
    
    return 0;
}

// Generic OLSR packet handler for NC queue
int receive_olsr_packet_for_nc(uint8_t *olsr_payload, size_t payload_len, uint8_t source_node) {
    if (!olsr_payload || payload_len == 0) {
        printf("RRC: ERROR - Invalid OLSR packet\n");
        return -1;
    }
    
    olsr_nc_stats.olsr_packets_received++;
    
    // Request NC slot for OLSR packet (external TDMA API call)
    uint8_t assigned_slot = 0;
    if (!tdma_request_nc_slot(olsr_payload, payload_len, &assigned_slot)) {
        printf("RRC: ERROR - Cannot get NC slot for OLSR packet from node %u\n", source_node);
        return -1;
    }
    
    // Enqueue OLSR packet to dedicated NC queue
    if (!enqueue_olsr_nc_packet(olsr_payload, payload_len, source_node, assigned_slot)) {
        printf("RRC: ERROR - Failed to enqueue OLSR packet to NC queue\n");
        return -1;
    }
    
    rrc_stats.nc_slot_requests++;
    
    printf("RRC: OLSR packet from node %u processed and queued for NC slot %u\n",
           source_node, assigned_slot);
    
    return 0;
}

// ============================================================================
// MAIN API FUNCTIONS FOR APPLICATION LAYER
// ============================================================================

// Main API function for L7 Application Layer
int rrc_process_application_packet(const CustomApplicationPacket *packet) {
    if (!packet) {
        printf("RRC: ERROR - NULL application packet\n");
        return -1;
    }
    
    // Initialize FSM if not already done
    if (!fsm_initialized) {
        rrc_handle_power_on();
    }
    
    // Check FSM state before processing
    if (current_rrc_state == RRC_STATE_NULL) {
        printf("RRC: ERROR - System not powered on\n");
        return -1;
    }
    
    // Trigger data request event if we're in IDLE state
    if (current_rrc_state == RRC_STATE_IDLE) {
        MessagePriority qos = map_data_type_to_priority(packet->data_type, packet->urgent);
        int result = rrc_handle_data_request(packet->dest_id, qos);
        if (result != 0) {
            printf("RRC: ERROR - Failed to handle data request for node %u\n", packet->dest_id);
            return -1;
        }
    }
    
    // Check if we have an active connection for this destination
    RRC_ConnectionContext* ctx = rrc_get_connection_context(packet->dest_id);
    if (!ctx || ctx->connection_state != RRC_STATE_CONNECTED) {
        printf("RRC: No active connection to node %u, attempting setup\n", packet->dest_id);
        // Connection setup is already triggered above, packet will be queued
    }
    
    // Process custom packet structure (no JSON parsing)
    ApplicationMessage *app_msg = process_custom_packet(packet);
    if (!app_msg) {
        printf("RRC: ERROR - Failed to process application packet\n");
        return -1;
    }
    
    // EXTENSION: Check for START/END packets for piggyback support (Requirement 1)
    rrc_check_start_end_packets(packet);
    
    // Update connection activity if context exists
    if (ctx) {
        rrc_update_connection_activity(packet->dest_id);
    }
    
    // Send through routing and TDMA with PHY quality checks
    int result = send_to_queue_l2_with_routing_and_phy(app_msg);
    
    // Handle routing results for FSM state management
    if (result == 0 && ctx) {
        // Successful transmission - ensure we're in CONNECTED state
        if (ctx->connection_state == RRC_STATE_CONNECTION_SETUP) {
            rrc_handle_route_and_slots_allocated(packet->dest_id, ctx->next_hop_id);
        }
    }
    
    return result;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void print_rrc_stats(void) {
    printf("\n=== RRC Statistics ===\n");
    printf("Packets processed: %u\n", rrc_stats.packets_processed);
    printf("Messages enqueued: %u\n", rrc_stats.messages_enqueued_total);
    printf("Messages discarded (no slots): %u\n", rrc_stats.messages_discarded_no_slots);
    printf("NC slot requests: %u\n", rrc_stats.nc_slot_requests);
    printf("Route discoveries triggered: %u\n", rrc_stats.route_discoveries_triggered);
    printf("PHY metrics updates: %u\n", rrc_stats.phy_metrics_updates);
    printf("Poor links detected: %u\n", rrc_stats.poor_links_detected);
    printf("======================\n\n");
    
    // Print FSM statistics
    print_rrc_fsm_stats();
    
    // Print OLSR NC queue statistics
    print_olsr_nc_stats();
    
    // Print Relay queue statistics
    print_relay_stats();
}

// ============================================================================
// NEIGHBOR CAPABILITY TRACKING IMPLEMENTATION
// ============================================================================

// Initialize neighbor tracking system
void init_neighbor_tracking(void) {
    if (neighbor_tracking_initialized) return;
    
    // NeighborState table is already initialized via init_neighbor_state_table()
    // Just set the flag and initialize TDMA if needed
    
    neighbor_tracking_initialized = true;
    printf("RRC: Neighbor tracking initialized (using NeighborState)\n");
}

// Update neighbor capabilities using NeighborState
void update_neighbor_capabilities(uint8_t node_id, bool tx_capable, bool rx_capable) {
    if (!neighbor_tracking_initialized) init_neighbor_tracking();
    
    // Use the MANET NeighborState system instead of undefined NodeCapability
    NeighborState* neighbor = rrc_create_neighbor_state(node_id);
    if (neighbor) {
        // Update capability bitmask (bit 0=TX, bit 1=RX)
        uint8_t old_capabilities = neighbor->capabilities;
        neighbor->capabilities = 0;
        if (tx_capable) neighbor->capabilities |= 0x01;  // TX bit
        if (rx_capable) neighbor->capabilities |= 0x02;  // RX bit
        
        neighbor->lastHeardTime = (uint64_t)time(NULL);
        neighbor->active = true;
        
        bool capability_changed = (old_capabilities != neighbor->capabilities);
        if (capability_changed) {
            printf("RRC: Node %u capabilities updated - TX: %s, RX: %s\n",
                   node_id, tx_capable ? "YES" : "NO", rx_capable ? "YES" : "NO");
            
            neighbor_stats.capabilities_updated++;
            
            // Update TDMA slot assignments if needed
            update_tdma_slot_assignments(node_id, tx_capable, rx_capable);
        }
    }
}

// Cleanup stale neighbors periodically using NeighborState
void cleanup_stale_neighbors(void) {
    if (!neighbor_tracking_initialized) return;
    
    uint64_t current_time = (uint64_t)time(NULL);
    const uint64_t NEIGHBOR_TIMEOUT_SEC = 60;  // 60 seconds timeout
    
    // Check NeighborState table instead of undefined neighbor_capabilities
    for (int i = 0; i < neighbor_count; i++) {
        NeighborState* neighbor = &neighbor_table[i];
        if (neighbor->active && neighbor->nodeID != 0) {
            uint64_t age = current_time - neighbor->lastHeardTime;
            if (age > NEIGHBOR_TIMEOUT_SEC) {
                printf("RRC: Neighbor %u timed out after %llu seconds\n", 
                       neighbor->nodeID, age);
                
                // Mark neighbor as inactive
                neighbor->active = false;
                neighbor->nodeID = 0;  // Clear node ID
                
                printf("RRC: Deactivated stale neighbor %u\n", neighbor->nodeID);
            }
        }
    }
}

// ============================================================================
// TDMA SLOT COORDINATION IMPLEMENTATION
// ============================================================================

// Initialize TDMA slot table
void init_tdma_slot_table(void) {
    for (int i = 0; i < 8; i++) {
        tdma_slot_table[i].slot_id = i;
        tdma_slot_table[i].assigned_node = 0;
        tdma_slot_table[i].is_tx_slot = false;
        tdma_slot_table[i].is_rx_slot = false;
        tdma_slot_table[i].is_nc_slot = (i == 0); // Slot 0 reserved for NC
        tdma_slot_table[i].collision_detected = false;
        tdma_slot_table[i].last_update = 0;
    }
    printf("RRC: TDMA slot table initialized (8 slots)\n");
}

// Request slot assignment from TDMA team (coordination, not assignment)
bool assign_tdma_slots(uint8_t node_id, bool tx_capable, bool rx_capable) {
    printf("RRC: Requesting slot assignment for node %u (TX=%s, RX=%s)\n",
           node_id, tx_capable ? "YES" : "NO", rx_capable ? "YES" : "NO");
    
    bool assignment_success = false;
    
    if (tx_capable) {
        // Ask TDMA team to assign TX slot
        if (tdma_check_slot_available(node_id, PRIORITY_DIGITAL_VOICE)) {
            printf("RRC: TDMA confirmed TX slot available for node %u\n", node_id);
            
            // Update our tracking table (TDMA team does actual assignment)
            for (int slot = 1; slot < 8; slot++) { // Skip slot 0 (NC slot)
                if (tdma_slot_table[slot].assigned_node == 0) {
                    tdma_slot_table[slot].assigned_node = node_id;
                    tdma_slot_table[slot].is_tx_slot = true;
                    tdma_slot_table[slot].last_update = (uint32_t)time(NULL);
                    
                    // Update neighbor's TX slots using NeighborState
                    NeighborState* neighbor = rrc_get_neighbor_state(node_id);
                    if (!neighbor) {
                        neighbor = rrc_create_neighbor_state(node_id);
                    }
                    if (neighbor) {
                        uint8_t byte_index = slot / 8;
                        uint8_t bit_offset = slot % 8;
                        neighbor->txSlots[byte_index] |= (1 << bit_offset);
                    }
                    
                    printf("RRC: Tracked TX slot %d assignment for node %u\n", slot, node_id);
                    assignment_success = true;
                    break;
                }
            }
        } else {
            printf("RRC: ⚠️ TDMA reports no TX slots available for node %u\n", node_id);
            neighbor_stats.slot_conflicts_detected++;
            generate_slot_assignment_failure_message(node_id);
        }
    }
    
    if (rx_capable) {
        printf("RRC: Node %u can receive - RX capability noted\n", node_id);
        
        // Track RX slots for this node using NeighborState
        NeighborState* neighbor = rrc_get_neighbor_state(node_id);
        if (!neighbor) {
            neighbor = rrc_create_neighbor_state(node_id);
        }
        if (neighbor) {
            for (int slot = 1; slot < 8; slot++) {
                uint8_t byte_index = slot / 8;
                uint8_t bit_offset = slot % 8;
                neighbor->rxSlots[byte_index] |= (1 << bit_offset);
                break;
            }
        }
        assignment_success = true;
    }
    
    return assignment_success;
}

// Check for slot conflicts
bool check_slot_conflicts(uint8_t node_id, uint8_t slot_id, bool is_tx_slot) {
    if (slot_id >= 8) return true; // Invalid slot
    
    TDMA_SlotInfo* slot = &tdma_slot_table[slot_id];
    
    if (is_tx_slot && slot->is_tx_slot && slot->assigned_node != 0 && slot->assigned_node != node_id) {
        printf("RRC: CONFLICT detected - Slot %u already assigned to node %u (requesting node %u)\n",
               slot_id, slot->assigned_node, node_id);
        neighbor_stats.slot_conflicts_detected++;
        return true;
    }
    
    return false;
}

// Update TDMA slot assignments based on neighbor capabilities
void update_tdma_slot_assignments(uint8_t node_id, bool tx_capable, bool rx_capable) {
    printf("RRC: Updating TDMA slot assignments for node %u\n", node_id);
    
    if (assign_tdma_slots(node_id, tx_capable, rx_capable)) {
        printf("RRC: Successfully coordinated slot assignments for node %u\n", node_id);
    } else {
        printf("RRC: Failed to coordinate slot assignments for node %u\n", node_id);
    }
}

// ============================================================================
// UPLINK PROCESSING IMPLEMENTATION
// ============================================================================

// Main uplink frame processing entry point
int rrc_process_uplink_frame(struct frame *received_frame) {
    if (!received_frame) {
        printf("RRC: ERROR - NULL received frame\n");
        return -1;
    }
    
    printf("RRC: Processing uplink frame from node %u to node %u (rx_or_l3=%s)\n",
           received_frame->source_add, received_frame->dest_add,
           received_frame->rx_or_l3 ? "L3" : "L7");
    
    // Update PHY metrics for source
    update_phy_metrics_for_node(received_frame->source_add);
    
    // Update connection activity if we have a context
    rrc_update_connection_activity(received_frame->source_add);
    
    // Route based on frame type
    if (received_frame->rx_or_l3) {
        // L3 control frame - forward to OLSR
        return forward_olsr_packet_to_l3(received_frame);
    } else {
        // Application data frame - check if for self or relay
        if (is_packet_for_self(received_frame)) {
            // Packet is for this node - deliver to L7
            relay_stats.relay_packets_to_self++;
            return deliver_data_packet_to_l7(received_frame);
        } else if (should_relay_packet(received_frame)) {
            // Packet needs to be relayed
            printf("RRC: Relaying packet from %u to %u via relay queue\n", 
                   received_frame->source_add, received_frame->dest_add);
            if (enqueue_relay_packet(received_frame)) {
                return 0; // Successfully queued for relay
            } else {
                return -1; // Failed to queue for relay
            }
        } else {
            // Packet should be discarded
            printf("RRC: Discarding packet - TTL expired or not for relay (dest: %u, TTL: %d)\n", 
                   received_frame->dest_add, received_frame->TTL);
            relay_stats.relay_packets_discarded++;
            return -1;
        }
    }
}

// Forward OLSR control packet to L3 layer
int forward_olsr_packet_to_l3(struct frame *l3_frame) {
    if (!l3_frame) {
        printf("RRC: ERROR - NULL L3 frame\n");
        return -1;
    }
    
    printf("RRC: Forwarding L3 control frame to OLSR team (source: %u, size: %d)\n",
           l3_frame->source_add, l3_frame->payload_length_bytes);
    
    // In a real system, this would call OLSR team's receive function
    // olsr_receive_control_packet(l3_frame->payload, l3_frame->payload_length_bytes);
    
    // Simulate OLSR processing
    printf("RRC: L3 frame forwarded to OLSR team for processing\n");
    
    return 0;
}

// Deliver application data packet to L7 layer
int deliver_data_packet_to_l7(struct frame *app_frame) {
    if (!app_frame) {
        printf("RRC: ERROR - NULL application frame\n");
        return -1;
    }
    
    printf("RRC: Delivering data packet to application layer (source: %u, type: %d)\n",
           app_frame->source_add, app_frame->data_type);
    
    // Convert frame to CustomApplicationPacket
    CustomApplicationPacket* app_packet = convert_frame_to_app_packet(app_frame);
    if (app_packet) {
        int result = rrc_deliver_to_application_layer(app_packet);
        release_app_packet(app_packet); // Release the allocated packet
        return result;
    }
    
    return -1;
}

// Convert network frame to application packet structure
CustomApplicationPacket* convert_frame_to_app_packet(const struct frame *frame) {
    if (!frame) return NULL;
    
    CustomApplicationPacket* packet = get_free_app_packet();
    if (!packet) {
        printf("RRC: ERROR - Cannot allocate application packet from pool\n");
        return NULL;
    }
    
    // Fill packet structure from frame data
    packet->src_id = frame->source_add;
    packet->dest_id = frame->dest_add;
    packet->sequence_number = 0; // Frame doesn't have sequence number
    packet->urgent = (frame->priority <= PRIORITY_DIGITAL_VOICE);
    packet->transmission_type = TRANSMISSION_UNICAST;
    
    // Convert frame data type to RRC data type
    switch (frame->data_type) {
        case DATA_TYPE_SMS:
            packet->data_type = RRC_DATA_TYPE_SMS;
            break;
        case DATA_TYPE_DIGITAL_VOICE:
            packet->data_type = RRC_DATA_TYPE_VOICE;
            break;
        case DATA_TYPE_ANALOG_VOICE:
            packet->data_type = RRC_DATA_TYPE_PTT;
            break;
        case DATA_TYPE_VIDEO_STREAM:
            packet->data_type = RRC_DATA_TYPE_VIDEO;
            break;
        case DATA_TYPE_FILE_TRANSFER:
            packet->data_type = RRC_DATA_TYPE_FILE;
            break;
        default:
            packet->data_type = RRC_DATA_TYPE_UNKNOWN;
            break;
    }
    
    // Copy payload
    size_t copy_size = (frame->payload_length_bytes > PAYLOAD_SIZE_BYTES) ? 
                      PAYLOAD_SIZE_BYTES : frame->payload_length_bytes;
    memcpy(packet->data, frame->payload, copy_size);
    packet->data_size = copy_size;
    
    printf("RRC: Converted frame to application packet - Type: %s, Size: %u\n",
           data_type_to_string(packet->data_type), (unsigned)packet->data_size);
    
    return packet;
}

// Deliver packet to application layer
int rrc_deliver_to_application_layer(const CustomApplicationPacket *packet) {
    if (!packet) {
        printf("RRC: ERROR - NULL application packet for delivery\n");
        return -1;
    }
    
    printf("RRC: ✅ Delivering to application - Node %u→%u, Type: %s, Size: %u, Data: \"%.*s\"\n",
           packet->src_id, packet->dest_id, 
           data_type_to_string(packet->data_type),
           (unsigned)packet->data_size,
           (int)packet->data_size, packet->data);
    
    // In a real system, this would call application callback
    // application_receive_packet(packet);
    
    // Simulate successful delivery
    notify_successful_delivery(packet->dest_id, packet->sequence_number);
    
    return 0;
}

// ============================================================================
// APPLICATION FEEDBACK IMPLEMENTATION
// ============================================================================

// Generate slot assignment failure message for application
void generate_slot_assignment_failure_message(uint8_t node_id) {
    printf("RRC: Generating slot assignment failure notification for node %u\n", node_id);
    
    CustomApplicationPacket* failure_packet = get_free_app_packet();
    if (!failure_packet) {
        printf("RRC: ERROR - Cannot allocate application packet for failure notification\n");
        return;
    }
    
    // Create failure notification packet
    failure_packet->src_id = 0;  // System message
    failure_packet->dest_id = node_id;
    failure_packet->data_type = RRC_DATA_TYPE_SMS;
    failure_packet->transmission_type = TRANSMISSION_UNICAST;
    failure_packet->urgent = true;
    failure_packet->sequence_number = 0;
    
    // Create failure message
    const char *failure_msg = "SLOT_ASSIGN_FAIL - No TDMA slots available";
    failure_packet->data_size = strlen(failure_msg);
    if (failure_packet->data_size > PAYLOAD_SIZE_BYTES) {
        failure_packet->data_size = PAYLOAD_SIZE_BYTES;
    }
    memcpy(failure_packet->data, failure_msg, failure_packet->data_size);
    
    printf("RRC: ❌ Slot assignment failure notification: %.*s\n", 
           (int)failure_packet->data_size, failure_packet->data);
    
    // Deliver notification to application layer
    rrc_deliver_to_application_layer(failure_packet);
    release_app_packet(failure_packet);
}

// Notify application of various failures
void notify_application_of_failure(uint8_t dest_node, const char* reason) {
    if (!reason) reason = "Unknown failure";
    
    printf("RRC: ❌ Notifying application of failure for node %u: %s\n", dest_node, reason);
    
    CustomApplicationPacket* failure_packet = get_free_app_packet();
    if (!failure_packet) {
        printf("RRC: ERROR - Cannot allocate application packet for failure notification\n");
        return;
    }
    
    // Create failure notification packet
    failure_packet->src_id = 0;  // System message
    failure_packet->dest_id = dest_node;
    failure_packet->data_type = RRC_DATA_TYPE_SMS;
    failure_packet->transmission_type = TRANSMISSION_UNICAST;
    failure_packet->urgent = true;
    failure_packet->sequence_number = 0;
    
    // Create failure message
    failure_packet->data_size = strlen(reason);
    if (failure_packet->data_size > PAYLOAD_SIZE_BYTES) {
        failure_packet->data_size = PAYLOAD_SIZE_BYTES;
    }
    memcpy(failure_packet->data, reason, failure_packet->data_size);
    
    // Deliver notification to application layer
    rrc_deliver_to_application_layer(failure_packet);
    release_app_packet(failure_packet);
}

// Notify application of successful delivery
void notify_successful_delivery(uint8_t dest_node, uint32_t sequence_number) {
    printf("RRC: ✅ Message successfully delivered to node %u (seq: %u)\n", 
           dest_node, sequence_number);
    
    // In a real system, this would create a delivery confirmation packet
    // and send it to the application layer
    
    CustomApplicationPacket* success_packet = get_free_app_packet();
    if (!success_packet) {
        printf("RRC: ERROR - Cannot allocate application packet for success notification\n");
        return;
    }
    
    // Create success notification packet
    success_packet->src_id = 0;  // System message
    success_packet->dest_id = dest_node;
    success_packet->data_type = RRC_DATA_TYPE_SMS;
    success_packet->transmission_type = TRANSMISSION_UNICAST;
    success_packet->urgent = false;
    success_packet->sequence_number = sequence_number;
    
    const char *success_msg = "DELIVERY_SUCCESS";
    success_packet->data_size = strlen(success_msg);
    memcpy(success_packet->data, success_msg, success_packet->data_size);
    
    printf("RRC: ✅ Delivery confirmation: %s\n", success_msg);
    
    // In production, this would be delivered to application
    // rrc_deliver_to_application_layer(success_packet);
    release_app_packet(success_packet);
}

// ============================================================================
// RRC EXTENSION – PIGGYBACK TLV FUNCTIONS (REQUIREMENT 1)
// ============================================================================

// Initialize the piggyback TLV system
void rrc_initialize_piggyback_system(void) {
    // Piggyback TLV is initialized via rrc_init_piggyback_tlv()
    piggyback_active = false;
    piggyback_last_update = 0;
    printf("RRC EXTENSION: Piggyback TLV system initialized\n");
}

// Initialize piggyback TLV on START packet from Application Layer
void rrc_initialize_piggyback(uint8_t node_id, uint8_t session_id, uint8_t traffic_type, uint8_t reserved_slot) {
    printf("RRC EXTENSION: Initializing piggyback TLV for node %u, session %u, traffic %u, slot %u\n",
           node_id, session_id, traffic_type, reserved_slot);
    
    // Update the TLV structure with piggyback data
    current_piggyback_tlv.sourceNodeID = node_id;
    current_piggyback_tlv.sourceReservations = traffic_type;  // Map traffic type to reservations
    current_piggyback_tlv.myNCSlot = reserved_slot;
    current_piggyback_tlv.ttl = 10;  // 10 frames TTL
    
    piggyback_active = true;
    piggyback_last_update = (uint32_t)time(NULL);
    
    printf("RRC EXTENSION: Piggyback TLV initialized successfully\n");
}

// Clear piggyback TLV on END packet from Application Layer
void rrc_clear_piggyback(void) {
    printf("RRC EXTENSION: Clearing piggyback TLV state\n");
    
    // Reset TLV to default values
    rrc_init_piggyback_tlv();
    piggyback_active = false;
    piggyback_last_update = 0;
    
    printf("RRC EXTENSION: Piggyback TLV cleared\n");
}

// Decrement TTL and check expiry
void rrc_update_piggyback_ttl(void) {
    if (piggyback_active && current_piggyback_tlv.ttl > 0) {
        current_piggyback_tlv.ttl--;
        piggyback_last_update = (uint32_t)time(NULL);
        
        if (current_piggyback_tlv.ttl == 0) {
            printf("RRC EXTENSION: Piggyback TTL expired, clearing\n");
            rrc_clear_piggyback();
        }
    }
}

// Check if piggyback should be attached to NC slot
bool rrc_should_attach_piggyback(void) {
    return piggyback_active && current_piggyback_tlv.ttl > 0;
}

// Get piggyback TLV data for NC slot transmission
PiggybackTLV* rrc_get_piggyback_data(void) {
    if (rrc_should_attach_piggyback()) {
        return &current_piggyback_tlv;
    }
    return NULL;
}

// Detect START/END packets in application processing
void rrc_check_start_end_packets(const CustomApplicationPacket *packet) {
    if (!packet || packet->data_size < 4) return;
    
    // Check for START packet (first 4 bytes = "STRT")
    if (memcmp(packet->data, "STRT", 4) == 0) {
        printf("RRC EXTENSION: Detected START packet from application\n");
        
        uint8_t traffic_type = 3;  // Default to data
        if (packet->data_type == RRC_DATA_TYPE_VOICE || packet->data_type == RRC_DATA_TYPE_PTT) {
            traffic_type = 1;  // Voice
        } else if (packet->data_type == RRC_DATA_TYPE_VIDEO) {
            traffic_type = 2;  // Video
        }
        
        uint8_t reserved_slot = 5;  // Default slot for data
        if (traffic_type == 1) reserved_slot = 1;  // Voice slot
        if (traffic_type == 2) reserved_slot = 3;  // Video slot
        
        rrc_initialize_piggyback(packet->src_id, packet->sequence_number, 
                               traffic_type, reserved_slot);
    }
    
    // Check for END packet (first 3 bytes = "END")
    if (packet->data_size >= 3 && memcmp(packet->data, "END", 3) == 0) {
        printf("RRC EXTENSION: Detected END packet from application\n");
        rrc_clear_piggyback();
    }
}

// ============================================================================
// RRC EXTENSION – SLOT STATUS REPORTING (REQUIREMENT 2)
// ============================================================================

// Generate current slot allocation status for TDMA team
void rrc_generate_slot_status(SlotStatusInfo slot_status[10]) {
    printf("RRC EXTENSION: Generating slot status report for TDMA team\n");
    
    // Initialize all slots as FREE
    for (int i = 0; i < 10; i++) {
        slot_status[i].slot_number = i;
        slot_status[i].usage_status = 0;  // FREE
        slot_status[i].assigned_node = 0;
        slot_status[i].traffic_type = 0;
        slot_status[i].priority = 3;  // Low priority default
    }
    
    // Update based on connection pool data
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (connection_pool[i].active) {
            for (int j = 0; j < 4; j++) {
                uint8_t slot = connection_pool[i].allocated_slots[j];
                if (slot < 10 && slot > 0) {  // Valid slot number
                    slot_status[slot].usage_status = 1;  // ALLOCATED
                    slot_status[slot].assigned_node = connection_pool[i].dest_node_id;
                    
                    // Set traffic type and priority based on QoS
                    if (connection_pool[i].qos_priority == PRIORITY_ANALOG_VOICE_PTT || 
                        connection_pool[i].qos_priority == PRIORITY_DIGITAL_VOICE) {
                        slot_status[slot].traffic_type = 1;  // Voice
                        slot_status[slot].priority = 1;     // High
                    } else if (connection_pool[i].qos_priority == PRIORITY_DATA_1) {
                        slot_status[slot].traffic_type = 2;  // Video/High Data
                        slot_status[slot].priority = 2;     // Medium
                    } else {
                        slot_status[slot].traffic_type = 3;  // Data
                        slot_status[slot].priority = 3;     // Low
                    }
                }
            }
        }
    }
    
    // Check for neighbor allocations and conflicts
    for (int i = 0; i < neighbor_count; i++) {
        if (neighbor_table[i].active) {
            for (int slot = 0; slot < 80; slot++) {
                if (rrc_is_neighbor_tx(neighbor_table[i].nodeID, slot)) {
                    if (slot < 10 && slot > 0) {
                        if (slot_status[slot].usage_status == 1 && 
                            slot_status[slot].assigned_node != neighbor_table[i].nodeID) {
                            slot_status[slot].usage_status = 3;  // COLLISION detected
                            printf("RRC EXTENSION: Collision detected on slot %u\n", slot);
                        } else if (slot_status[slot].usage_status == 0) {
                            slot_status[slot].usage_status = 2;  // RESERVED by neighbor
                            slot_status[slot].assigned_node = neighbor_table[i].nodeID;
                        }
                    }
                }
            }
        }
    }
    
    // NC slots (8 and 9) are always reserved for control
    slot_status[8].usage_status = 2;  // RESERVED for NC
    slot_status[8].traffic_type = 0;  // Control
    slot_status[8].priority = 1;      // High priority
    
    slot_status[9].usage_status = 2;  // RESERVED for NC
    slot_status[9].traffic_type = 0;  // Control
    slot_status[9].priority = 1;      // High priority
    
    printf("RRC EXTENSION: Slot status report generated for TDMA team\n");
    
    // Debug output
    for (int i = 0; i < 10; i++) {
        printf("RRC EXTENSION: Slot %u - Status: %u, Node: %u, Type: %u, Priority: %u\n",
               i, slot_status[i].usage_status, slot_status[i].assigned_node,
               slot_status[i].traffic_type, slot_status[i].priority);
    }
}

// ============================================================================
// RRC EXTENSION – NC SLOT ALLOCATION (REQUIREMENT 3)
// ============================================================================

// Static variables for NC slot round-robin
static uint8_t current_nc_slot = 8;  // Start with slot 8
static uint32_t nc_slot_counter = 0; // Frame counter for round-robin

// Update NC slot allocation in round-robin fashion
void rrc_update_nc_schedule(void) {
    nc_slot_counter++;
    
    // Switch between slots 8 and 9 every frame
    if (nc_slot_counter % 2 == 0) {
        current_nc_slot = 8;
    } else {
        current_nc_slot = 9;
    }
    
    printf("RRC EXTENSION: NC slot updated to %u (frame %u)\n", 
           current_nc_slot, nc_slot_counter);
}
// Get current NC slot number
uint8_t rrc_get_current_nc_slot(void) {
    return current_nc_slot;
}

// ============================================================================
// DEBUG AND MONITORING FUNCTIONS
// ============================================================================

// Print neighbor capabilities table
void print_neighbor_capabilities(void) {
    printf("\n=== Neighbor Capabilities ===\n");
    printf("Total neighbors: %d\n", neighbor_count);
    printf("Node | TX | RX | TX Slots | RX Slots | Age(s) | Active\n");
    printf("-----|----|----|----------|----------|--------|-------\n");
    
    uint32_t current_time = (uint32_t)time(NULL);
    
    for (int i = 0; i < neighbor_count; i++) {
        if (neighbor_table[i].active) {
            uint32_t age = current_time - (uint32_t)neighbor_table[i].lastHeardTime;
            
            printf(" %3u | %2s | %2s |", 
                   neighbor_table[i].nodeID,
                   (neighbor_table[i].capabilities & 0x01) ? "Y" : "N",
                   (neighbor_table[i].capabilities & 0x02) ? "Y" : "N");
            
            // Print TX slots
            for (int j = 0; j < 4; j++) {
                bool found_tx = false;
                for (int slot = j*20; slot < (j+1)*20 && slot < 80; slot++) {
                    if (rrc_is_neighbor_tx(neighbor_table[i].nodeID, slot)) {
                        printf(" %u", slot);
                        found_tx = true;
                        break;
                    }
                }
                if (!found_tx) printf(" -");
            }
            printf("    |");
            
            // Print RX slots  
            for (int j = 0; j < 4; j++) {
                bool found_rx = false;
                for (int slot = j*20; slot < (j+1)*20 && slot < 80; slot++) {
                    if (rrc_is_neighbor_rx(neighbor_table[i].nodeID, slot)) {
                        printf(" %u", slot);
                        found_rx = true;
                        break;
                    }
                }
                if (!found_rx) printf(" -");
            }
            printf("    | %6u | %s\n", age, 
                   neighbor_table[i].active ? "YES" : "NO");
        }
    }
    
    printf("\nNeighbor Statistics:\n");
    printf("  Hello packets parsed: %u\n", neighbor_stats.hello_packets_parsed);
    printf("  Capabilities updated: %u\n", neighbor_stats.capabilities_updated);
    printf("  NC slots assigned: %u\n", neighbor_stats.nc_slots_assigned);
    printf("  Slot conflicts detected: %u\n", neighbor_stats.slot_conflicts_detected);
    printf("  Piggyback updates: %u\n", neighbor_stats.piggyback_updates);
    printf("=============================\n\n");
}

// Print TDMA slot assignment table
void print_tdma_slot_table(void) {
    printf("\n=== TDMA Slot Assignment Table ===\n");
    printf("Slot | Assigned | TX | RX | NC | Conflict | Last Update\n");
    printf("-----|----------|----|----|----|---------|--------------\n");
    
    for (int i = 0; i < 8; i++) {
        printf(" %2d  |    %3u   | %2s | %2s | %2s |    %2s    | %u\n",
               tdma_slot_table[i].slot_id,
               tdma_slot_table[i].assigned_node,
               tdma_slot_table[i].is_tx_slot ? "Y" : "N",
               tdma_slot_table[i].is_rx_slot ? "Y" : "N",
               tdma_slot_table[i].is_nc_slot ? "Y" : "N",
               tdma_slot_table[i].collision_detected ? "Y" : "N",
               tdma_slot_table[i].last_update);
}