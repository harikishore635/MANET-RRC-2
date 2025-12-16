/*
 * RRC (Radio Resource Controller) - Inter-Process Communication Version
 *
 * This version implements IPC mechanisms for communication between:
 * - RRC (Radio Resource Controller)
 * - OLSR (L3 Routing)
 * - TDMA (L2 MAC)
 * - Application Layer (L7)
 * - PHY Layer
 *
 * Each component runs as a separate process and communicates via:
 * - Shared Memory (ONLY for queue data - high-throughput path)
 * - POSIX Message Queues (for control/command messages - UNIDIRECTIONAL)
 * - Semaphores (ONLY for queue synchronization)
 *
 * DESIGN PRINCIPLES:
 *
 * 1. Clear Ownership:
 *    - RRC OWNS: FSM state, connection contexts, neighbor table
 *    - All processes SHARE: Queue data structures (read/write)
 *    - Other processes QUERY RRC state via message queues (request/response)
 *
 * 2. Unidirectional Message Queues:
 *    - Each direction has dedicated queue (e.g., OLSR→RRC, RRC→OLSR)
 *    - Clear sender/receiver roles, no confusion about who writes
 *    - Prevents race conditions and ownership ambiguity
 *
 * 3. Minimal Shared Memory:
 *    - ONLY queues are shared (TDMA needs fast dequeue access)
 *    - Single semaphore protects queue operations
 *    - No shared state = no state synchronization bugs
 *
 * 4. Request/Response Pattern:
 *    - TDMA requests: "Is slot available?" → RRC responds
 *    - OLSR provides: Route updates (push) and responds to discovery requests
 *    - PHY provides: Metric updates (push) and responds to metric requests
 *    - App sends: Data packets (push) and receives delivery status
 *
 * COMMUNICATION FLOWS:
 *
 *   Application → RRC:  Data packets
 *   RRC → Application:  Delivery status, failures
 *
 *   OLSR → RRC:  Route updates, hello packets
 *   RRC → OLSR:  Route requests, discovery triggers
 *
 *   TDMA → RRC:  Dequeue requests, slot status
 *   RRC → TDMA:  Slot responses, queue status
 *
 *   PHY → RRC:  Link metrics, status updates
 *   RRC → PHY:  Metric requests
 *
 *   All ↔ Shared Memory:  Queue enqueue/dequeue operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// POSIX IPC headers
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <mqueue.h>
#include <errno.h>
#include <pthread.h>

// System headers for IPC
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>

// Compatibility constants for queue.c
#define PAYLOAD_SIZE_BYTES 2800 // Updated payload size for larger data packets
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
#define DU_GU_SLOTS_COUNT 60 // 6 slots per frame × 10 frames
#define NEIGHBOR_TIMEOUT_SUPERCYCLES 2

// RRC Node Configuration
static uint8_t rrc_node_id = 1; // Default node ID, configurable

// ============================================================================
// IPC CONFIGURATION AND SHARED MEMORY NAMES
// ============================================================================

// Shared memory names - ONLY for queues (high-throughput data)
#define SHM_RRC_QUEUES "/rrc_queues_shm"

// Message queue names - UNIDIRECTIONAL for clear ownership
// OLSR → RRC (routing updates, hello packets)
#define MQ_OLSR_TO_RRC "/mq_olsr_to_rrc"
// RRC → OLSR (route requests,other msg from rrc)
#define MQ_RRC_TO_OLSR "/mq_rrc_to_olsr"

// TDMA → RRC (slot status, dequeue requests)
#define MQ_TDMA_TO_RRC "/mq_tdma_to_rrc"
// RRC → TDMA (slot table updates)
#define MQ_RRC_TO_TDMA "/mq_rrc_to_tdma"

// Application ↔ RRC (shared memory for bidirectional communication)
#define SHM_APP_RRC "/rrc_app_shm"
#define SEM_APP_RRC_MUTEX "/rrc_app_sem"

// PHY → RRC (link metrics, updates)
#define MQ_PHY_TO_RRC "/mq_phy_to_rrc"
// RRC → PHY (metric requests)
#define MQ_RRC_TO_PHY "/mq_rrc_to_phy"

// Semaphore names - ONLY for queue synchronization
#define SEM_QUEUE_MUTEX "/rrc_queue_sem"

// Message queue configuration
#define MQ_MAX_MESSAGES 10
#define MQ_MESSAGE_SIZE 4096

// Application-RRC shared memory queue configuration
#define APP_RRC_QUEUE_SIZE 20  // Queue size for app→rrc and rrc→app

// ============================================================================
// OLSR MESSAGE STRUCTURES (from OLSR team)
// ============================================================================

// OLSR Message Types
#define OLSR_HELLO_MESSAGE 1
#define OLSR_TC_MESSAGE 2

// Hello Neighbor Structure
struct hello_neighbor
{
    uint32_t neighbor_id;
    uint8_t link_code;
};

// Two-Hop Hello Neighbor Structure
struct two_hop_hello_neighbor
{
    uint32_t neighbor_id;
    uint8_t link_code;
};

// OLSR Hello Message
struct olsr_hello
{
    uint16_t hello_interval;
    uint8_t willingness;
    uint8_t reserved;
    int reserved_slot;
    struct hello_neighbor *neighbors;
    int neighbor_count;
    uint8_t two_hop_count;
    struct two_hop_hello_neighbor *two_hop_neighbors;
};

// TC Neighbor Structure
struct tc_neighbor
{
    uint32_t neighbor_addr;
};

// OLSR TC Message
struct olsr_tc
{
    uint16_t ansn;
    struct tc_neighbor *mpr_selectors;
    int selector_count;
};

// OLSR Message (can be HELLO or TC)
struct olsr_message
{
    uint8_t msg_type; // OLSR_HELLO_MESSAGE or OLSR_TC_MESSAGE
    uint8_t vtime;
    uint16_t msg_size;
    uint32_t originator;
    uint8_t ttl;
    uint8_t hop_count;
    uint16_t msg_seq_num;
    void *body; // Points to olsr_hello or olsr_tc
};

// ============================================================================
// IPC MESSAGE STRUCTURES
// ============================================================================

// Message types for inter-process communication
typedef enum
{
    // OLSR → RRC Messages
    MSG_OLSR_ROUTE_UPDATE = 1,   // Route table update
    MSG_OLSR_MESSAGE = 2,        // OLSR protocol message (HELLO/TC/other)

    // RRC → OLSR Messages
    MSG_RRC_ROUTE_REQUEST = 10, // Request route to destination

    // TDMA → RRC Messages
    MSG_TDMA_SLOT_STATUS_UPDATE = 20, // Current slot status (push notification)
    MSG_TDMA_RX_QUEUE_DATA = 21,      // TDMA notifies RRC of data in rx_queue

    // RRC → TDMA Messages
    MSG_RRC_SLOT_TABLE_UPDATE = 30,   // RRC sends slot table to TDMA

    // Application → RRC Messages
    MSG_APP_DATA_PACKET = 40, // L7 data packet (not used - shared memory instead)

    // PHY → RRC Messages
    MSG_PHY_METRICS_UPDATE = 60,     // Link quality metrics
    MSG_PHY_LINK_STATUS_CHANGE = 61, // Link up/down

    // RRC → PHY Messages
    MSG_RRC_METRICS_REQUEST = 70, // Request metrics for node

    // Control Messages (any direction)
    MSG_CONTROL_INIT = 100,
    MSG_CONTROL_SHUTDOWN = 101,
    MSG_CONTROL_STATUS_REQUEST = 102,
    MSG_CONTROL_STATUS_RESPONSE = 103
} MessageType;

// RRC-specific OLSR control messages (lightweight, not full OLSR messages)

// RRC asks OLSR for next hop (query)
typedef struct
{
    MessageType type; // MSG_RRC_ROUTE_REQUEST
    uint8_t dest_node;
    uint32_t request_id;
} RRC_RouteRequest;

// OLSR responds with next hop (response)
typedef struct
{
    MessageType type; // MSG_OLSR_ROUTE_UPDATE
    uint8_t dest_node;
    uint8_t next_hop;
    uint8_t hop_count;
    bool route_available;
    uint32_t request_id; // Matches request
} RRC_RouteResponse;

// RRC asks OLSR to trigger route discovery
typedef struct
{
    MessageType type; // MSG_RRC_ROUTE_REQUEST (with urgent flag)
    uint8_t dest_node;
    uint32_t request_id;
    bool urgent; // Trigger immediate discovery
} RRC_DiscoveryRequest;

// RRC forwards received OLSR packet to L3 (from rx_queue uplink processing)
typedef struct
{
    MessageType type; // MSG_OLSR_MESSAGE
    uint8_t source_node; // Original source of the OLSR packet
    uint8_t received_from; // Node we received this from (previous hop)
    struct olsr_message olsr_msg; // The actual OLSR message (HELLO/TC/other)
    uint32_t timestamp; // When packet was received
    bool forwarded; // True if this is a forwarded packet (for relay)
} RRC_ForwardOLSRPacket;

// RRC sends TDMA slot table to TDMA layer (RRC → TDMA)
typedef struct
{
    MessageType type; // MSG_RRC_SLOT_TABLE_UPDATE
    TDMA_SlotInfo slot_table[8]; // Slot 0-7 allocation information
    uint32_t timestamp; // When table was updated
    uint8_t updated_slot_count; // Number of slots with updates
} RRC_SlotTableUpdate;

// TDMA notifies RRC of received data in rx_queue (TDMA → RRC)
typedef struct
{
    MessageType type; // MSG_TDMA_RX_QUEUE_DATA
    uint8_t frame_count; // Number of frames placed in rx_queue
    uint8_t source_node; // Source node of the received frame
    uint8_t dest_node; // Destination node of the received frame
    bool is_for_self; // True if dest_node == rrc_node_id
    uint32_t timestamp; // When frame was received
} TDMA_RxQueueNotification;

// PHY Metrics Update Message
typedef struct
{
    MessageType type;
    uint8_t node_id;
    float rssi_dbm;
    float snr_db;
    float per_percent;
    bool link_active;
    uint32_t packet_count;
    uint32_t timestamp;
} PHY_MetricsUpdate;

// Application Data Packet Message
typedef struct
{
    MessageType type;
    uint8_t src_id;
    uint8_t dest_id;
    uint8_t data_type;
    uint8_t transmission_type;
    uint8_t data[PAYLOAD_SIZE_BYTES];
    size_t data_size;
    bool urgent;
    uint32_t sequence_number;
} APP_DataPacket;

// Application-RRC Shared Memory Queue Structure
// Bidirectional communication using CustomApplicationPacket
// - app_to_rrc_queue: Application sends data packets to RRC (downlink)
// - rrc_to_app_queue: RRC delivers received data packets to Application (uplink)
typedef struct
{
    CustomApplicationPacket app_to_rrc_queue[APP_RRC_QUEUE_SIZE];  // App → RRC (downlink data)
    int app_to_rrc_front;
    int app_to_rrc_back;
    int app_to_rrc_count;
    
    CustomApplicationPacket rrc_to_app_queue[APP_RRC_QUEUE_SIZE];  // RRC → App (uplink received data)
    int rrc_to_app_front;
    int rrc_to_app_back;
    int rrc_to_app_count;
    
    sem_t mutex;  // Protects both queues
} APP_RRC_SharedMemory;

// Generic IPC Message (union of all message types)
// Note: Application layer uses shared memory, not message queues
typedef union
{
    MessageType type;

    // OLSR messages - use OLSR's own structures directly
    struct olsr_message olsr_msg;       // HELLO or TC message from OLSR
    RRC_RouteRequest route_request;     // RRC asks for route
    RRC_RouteResponse route_response;   // OLSR provides route
    RRC_DiscoveryRequest discovery_req; // RRC triggers discovery
    RRC_ForwardOLSRPacket forward_olsr; // RRC forwards received OLSR packet to L3

    // TDMA messages
    RRC_SlotTableUpdate slot_table;     // RRC sends slot table to TDMA
    TDMA_RxQueueNotification rx_notify; // TDMA notifies RRC of rx_queue data

    // PHY messages
    PHY_MetricsUpdate phy_metrics;
} IPC_Message;

// ============================================================================
// DATA TYPE DEFINITIONS (from original rccv3.c)
// ============================================================================

// Data types from queue.c
typedef enum
{
    DATA_TYPE_SMS = 0,
    DATA_TYPE_DIGITAL_VOICE = 1,
    DATA_TYPE_VIDEO_STREAM = 2,
    DATA_TYPE_FILE_TRANSFER = 3,
    DATA_TYPE_ANALOG_VOICE
} DATATYPE;

// Frame structure from queue.c
struct frame
{
    uint8_t source_add;
    uint8_t dest_add;
    uint8_t next_hop_add;
    bool rx_or_l3;
    int TTL;
    int priority;
    DATATYPE data_type;
    uint8_t payload[PAYLOAD_SIZE_BYTES];
    int payload_length_bytes;
};

// Queue structure from queue.c
struct queue
{
    struct frame data[QUEUE_SIZE];
    int front;
    int back;
};

// RRC Data type definitions (Application Layer)
typedef enum
{
    RRC_DATA_TYPE_SMS = 0,
    RRC_DATA_TYPE_VOICE = 1,
    RRC_DATA_TYPE_VIDEO = 2,
    RRC_DATA_TYPE_FILE = 3,
    RRC_DATA_TYPE_PTT = 4,
    RRC_DATA_TYPE_RELAY = 5,
    RRC_DATA_TYPE_UNKNOWN = 99
} RRC_DataType;

// Priority mapping for queue.c
typedef enum
{
    PRIORITY_ANALOG_VOICE_PTT = 0, // → analog_voice_queue
    PRIORITY_DIGITAL_VOICE = 1,    // → data_from_l3_queue[0]
    PRIORITY_DATA_1 = 2,           // → data_from_l3_queue[1]
    PRIORITY_DATA_2 = 3,           // → data_from_l3_queue[2]
    PRIORITY_DATA_3 = 4,           // → data_from_l3_queue[3]
    PRIORITY_RX_RELAY = 4          // → rx_queue
} MessagePriority;

// Transmission type definitions(unicast,multicast,broadcast)
typedef enum
{
    TRANSMISSION_UNICAST = 0,
    TRANSMISSION_MULTICAST = 1,
    TRANSMISSION_BROADCAST = 2
} TransmissionType;

// Custom Application Packet Structure (From L7 Application Layer)
typedef struct
{
    uint8_t src_id;
    uint8_t dest_id;
    RRC_DataType data_type;
    TransmissionType transmission_type;
    uint8_t data[PAYLOAD_SIZE_BYTES];
    size_t data_size;
    uint32_t sequence_number;
    uint32_t timestamp;
    bool urgent;
} CustomApplicationPacket;

// RRC Internal Message structure (for static pool management)
typedef struct
{
    uint8_t node_id;
    uint8_t dest_node_id;
    RRC_DataType data_type;
    TransmissionType transmission_type;
    MessagePriority priority;
    uint8_t data[PAYLOAD_SIZE_BYTES];
    size_t data_size;
    bool preemption_allowed;
    bool in_use;
} ApplicationMessage;

// ============================================================================
// PHY LAYER METRICS
// ============================================================================

// Link quality thresholds for route decisions
#define RSSI_POOR_THRESHOLD_DBM -90.0f
#define SNR_POOR_THRESHOLD_DB 10.0f
#define PER_POOR_THRESHOLD_PERCENT 50.0f
#define LINK_TIMEOUT_SECONDS 30

// PHY Metrics Structure for neighbor tracking
typedef struct
{
    float rssi_dbm;
    float snr_db;
    float per_percent;
    uint32_t packet_count;
    uint32_t last_update_time;
} PHYMetrics;

// ============================================================================
// MANET WAVEFORM STRUCTURES
// ============================================================================

// Neighbor State Structure
typedef struct
{
    uint16_t nodeID;
    bool active;
    uint64_t lastHeardTime;
    uint8_t txSlots[10];
    uint8_t rxSlots[10];
    PHYMetrics phy;
    uint8_t assignedNCSlot;
} NeighborState;

// Slot Status Structure
typedef struct
{
    uint64_t ncStatusBitmap;
    uint64_t duGuUsageBitmap;
    uint32_t lastUpdateTime;
} SlotStatus;

// Piggyback TLV Structure
typedef struct
{
    uint8_t type;
    uint8_t length;
    uint16_t sourceNodeID;
    uint8_t sourceReservations;
    uint8_t relayReservations;
    uint64_t duGuIntentionMap;
    uint64_t ncStatusBitmap;
    uint32_t timeSync;
    uint8_t myNCSlot;
    uint8_t ttl;
} PiggybackTLV;

// NC Slot Management
typedef struct
{
    uint8_t activeNodes[MAX_MONITORED_NODES];
    int activeNodeCount;
    uint8_t ncSlotAssignments[NC_SLOTS_PER_SUPERCYCLE];
    uint8_t currentRoundRobinIndex;
    uint8_t myAssignedNCSlot;
} NCSlotManager;

// TDMA slot assignment table
typedef struct
{
    uint8_t node_id;
    uint8_t slot_id;
    bool is_tx_slot;
    bool is_allocated;
    uint8_t priority;
    uint32_t last_update;
} TDMA_SlotInfo;

// ============================================================================
// NC SLOT MESSAGE STRUCTURES (for OLSR/Piggyback/Neighbor integration)
// ============================================================================

// OLSR Message Structure for NC transmission
typedef struct {
    uint8_t msg_type;           // 1=HELLO, 2=TC
    uint8_t vtime;              // Validity time
    uint16_t msg_size;          // Message size
    uint32_t originator_addr;   // Originating node
    uint8_t ttl;                // Time to live
    uint8_t hop_count;          // Hop count
    uint16_t msg_seq_num;       // Sequence number
    uint8_t payload[2048];      // OLSR message payload
    size_t payload_len;         // Actual payload length
} OLSRMessage;

// NC Slot Message - Complete unified message
typedef struct {
    uint8_t myAssignedNCSlot;        // My assigned NC slot (1-40)
    OLSRMessage olsr_message;        // OLSR HELLO/TC to transmit
    bool has_olsr_message;           // Flag: OLSR message present
    PiggybackTLV piggyback_tlv;      // Piggyback information
    bool has_piggyback;              // Flag: Piggyback present
    NeighborState my_neighbor_info;  // My neighbor state information
    bool has_neighbor_info;          // Flag: Neighbor info present
    uint32_t timestamp;              // Message creation timestamp
    uint16_t sourceNodeID;           // Source node ID
    uint32_t sequence_number;        // Message sequence number
    bool is_valid;                   // Message validity flag
} NCSlotMessage;

// NC Slot Message Queue - Queue for NC messages
#define NC_SLOT_QUEUE_SIZE 10
typedef struct {
    NCSlotMessage messages[NC_SLOT_QUEUE_SIZE];
    int front;
    int back;
    int count;
    pthread_mutex_t mutex;  // Thread-safe access
} NCSlotMessageQueue;

// NC Reservation Request - For priority-based NC slot allocation
typedef struct {
    uint16_t nodeID;
    uint8_t hopCount;
    bool isSelfReservation;
    uint8_t trafficType;
    uint32_t timestamp;
    uint8_t requestedSlot;
    uint32_t packetCount;
} NCReservationRequest;

// ============================================================================
// FSM STATES AND CONNECTION CONTEXTS
// ============================================================================

// FSM States for RRC System Management
typedef enum
{
    RRC_STATE_NULL = 0,
    RRC_STATE_IDLE,
    RRC_STATE_CONNECTION_SETUP,
    RRC_STATE_CONNECTED,
    RRC_STATE_RECONFIGURATION,
    RRC_STATE_RELEASE
} RRC_SystemState;

// Connection Context Structure
typedef struct
{
    bool active;                
    uint8_t dest_node_id;
    uint8_t next_hop_id;
    RRC_SystemState connection_state;
    MessagePriority qos_priority;
    uint32_t last_activity_time;
    uint8_t allocated_slots[4];
    bool setup_pending;
    bool reconfig_pending;
} RRC_ConnectionContext;

// ============================================================================
// SHARED MEMORY STRUCTURES
// ============================================================================

// Shared Queue Data (ONLY queues - high-throughput data path)
// RRC owns and manages, TDMA reads for dequeue operations
typedef struct
{
    struct queue analog_voice_queue;
    struct queue data_from_l3_queue[NUM_PRIORITY];
    struct queue rx_queue;
    NCSlotMessageQueue nc_slot_queue;  // Unified NC slot message queue (replaces olsr_hello_queue and rrc_olsr_nc_queue)
    struct queue rrc_relay_queue;
    sem_t queue_mutex; // Protects queue operations only
} SharedQueueData;

// RRC State - PRIVATE to RRC process (NOT shared)
// Others query via message queue requests
typedef struct
{
    RRC_SystemState current_rrc_state;
    RRC_ConnectionContext connection_pool[RRC_CONNECTION_POOL_SIZE];
    bool fsm_initialized;
    // Statistics
    struct
    {
        uint32_t packets_processed;
        uint32_t messages_enqueued_total;
        uint32_t messages_discarded_no_slots;
    } stats;
} RRC_PrivateState;

// Neighbor Table - PRIVATE to RRC process (NOT shared)
// Others query neighbor info via message queue
typedef struct
{
    NeighborState neighbor_table[MAX_MONITORED_NODES];
    int neighbor_count;
    SlotStatus current_slot_status;
    NCSlotManager nc_manager;
    PiggybackTLV current_piggyback_tlv;
} RRC_PrivateNeighborData;

// ============================================================================
// IPC HANDLES (Global for this process)
// ============================================================================

// Message queue descriptors - UNIDIRECTIONAL
static mqd_t mq_olsr_to_rrc = -1; // OLSR sends to RRC
static mqd_t mq_rrc_to_olsr = -1; // RRC sends to OLSR
static mqd_t mq_tdma_to_rrc = -1; // TDMA sends to RRC
static mqd_t mq_rrc_to_tdma = -1; // RRC sends to TDMA
static mqd_t mq_phy_to_rrc = -1;  // PHY sends to RRC
static mqd_t mq_rrc_to_phy = -1;  // RRC sends to PHY

// Shared memory descriptors
static int shm_queues_fd = -1;     // For queue data
static int shm_app_rrc_fd = -1;    // For app-rrc communication

// Shared memory pointers
static SharedQueueData *shared_queues = NULL;       // Queue data
static APP_RRC_SharedMemory *app_rrc_shm = NULL;    // App-RRC communication

// Private RRC data - NOT in shared memory
static RRC_PrivateState rrc_state = {0};
static RRC_PrivateNeighborData rrc_neighbors = {0};

// TDMA slot table (slots 0-7) - RRC manages and sends to TDMA
static TDMA_SlotInfo tdma_slot_table[8] = {0};

// IPC initialization flag
static bool ipc_initialized = false;

// ============================================================================
// IPC FUNCTION PROTOTYPES
// ============================================================================

// IPC initialization and cleanup
int rrc_ipc_init(void);
void rrc_ipc_cleanup(void);

// Message queue operations - UNIDIRECTIONAL send/receive
// OLSR communication
int rrc_send_to_olsr(const IPC_Message *msg);
int rrc_receive_from_olsr(IPC_Message *msg, bool blocking);

// TDMA communication
int rrc_send_to_tdma(const IPC_Message *msg);
int rrc_receive_from_tdma(IPC_Message *msg, bool blocking);

// Application communication (shared memory - uses CustomApplicationPacket)
int rrc_receive_from_app(CustomApplicationPacket *packet, bool blocking);  // RRC reads downlink from app
int rrc_send_to_app(const CustomApplicationPacket *packet);  // RRC sends uplink data to app
bool app_to_rrc_queue_is_empty(void);
bool app_to_rrc_queue_is_full(void);
bool rrc_to_app_queue_is_empty(void);
bool rrc_to_app_queue_is_full(void);
int app_to_rrc_queue_count(void);
int rrc_to_app_queue_count(void);
void print_app_rrc_queue_stats(void);

// PHY communication
int rrc_send_to_phy(const IPC_Message *msg);
int rrc_receive_from_phy(IPC_Message *msg, bool blocking);

// Shared memory queue operations (ONLY for queue data)
void rrc_queue_lock(void);
void rrc_queue_unlock(void);
void rrc_enqueue_shared(struct queue *q, struct frame frame);
struct frame rrc_dequeue_shared(struct queue *q);
bool rrc_is_queue_empty(struct queue *q);
bool rrc_is_queue_full(struct queue *q);

// RRC state operations - PRIVATE (not shared, query via messages)
RRC_SystemState rrc_get_current_state(void);
void rrc_set_current_state(RRC_SystemState new_state);
RRC_ConnectionContext *rrc_get_connection_ctx(uint8_t dest_node);

// Neighbor operations - PRIVATE (not shared, query via messages)
NeighborState *rrc_get_neighbor(uint16_t nodeID);
void rrc_update_neighbor(uint16_t nodeID, const NeighborState *state);

// IPC-based external API wrappers (replacing extern functions)
// These send message queue requests and wait for responses
uint8_t ipc_olsr_get_next_hop(uint8_t destination_node_id);
void ipc_olsr_trigger_route_discovery(uint8_t destination_node_id);
bool ipc_tdma_check_slot_available(uint8_t next_hop_node, int priority);
bool ipc_tdma_request_nc_slot(const uint8_t *payload, size_t payload_len, uint8_t *assigned_slot);
void ipc_phy_get_link_metrics(uint8_t node_id, float *rssi, float *snr, float *per);
bool ipc_phy_is_link_active(uint8_t node_id);
uint32_t ipc_phy_get_packet_count(uint8_t node_id);

// Message processing threads (one per external process)
void *rrc_olsr_message_handler(void *arg);
void *rrc_tdma_message_handler(void *arg);
void *rrc_app_message_handler(void *arg);
void *rrc_phy_message_handler(void *arg);
