/*
 * RRC (Radio Resource Controller) - Integrated IPC Version
 *
 * This version combines:
 * - IPC architecture from rrcvk.c (POSIX message queues, shared memory, semaphores)
 * - Core RRC logic from rccv2.c (FSM, NC slots, message processing)
 *
 * IPC Communication:
 * - POSIX Message Queues (unidirectional control messages)
 * - Shared Memory (high-throughput frame queues)
 * - Semaphores (queue synchronization)
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

// System headers
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>

// Compatibility constants
#define PAYLOAD_SIZE_BYTES 2800
#define NUM_PRIORITY 4
#define QUEUE_SIZE 10

// RRC Configuration
#define RRC_MESSAGE_POOL_SIZE 16
#define MAX_MONITORED_NODES 40
#define MAX_NEIGHBORS 40  // Same as MAX_MONITORED_NODES
#define RRC_CONNECTION_POOL_SIZE 8
#define RRC_INACTIVITY_TIMEOUT_SEC 30
#define RRC_SETUP_TIMEOUT_SEC 10

// MANET NC Slot Configuration
#define NC_SLOTS_PER_SUPERCYCLE 40
#define FRAMES_PER_CYCLE 10
#define CYCLES_PER_SUPERCYCLE 2
#define NC_SLOT_TIMEOUT_MS 2000
#define DU_GU_SLOTS_COUNT 60
#define NEIGHBOR_TIMEOUT_SUPERCYCLES 2

// RRC Node Configuration
static uint8_t rrc_node_id = 1;

// ============================================================================
// IPC CONFIGURATION
// ============================================================================

// Shared memory names
#define SHM_RRC_QUEUES "/rrc_queues_shm"
#define SHM_APP_RRC "/rrc_app_shm"

// Message queue names (unidirectional)
#define MQ_OLSR_TO_RRC "/mq_olsr_to_rrc"
#define MQ_RRC_TO_OLSR "/mq_rrc_to_olsr"
#define MQ_TDMA_TO_RRC "/mq_tdma_to_rrc"
#define MQ_RRC_TO_TDMA "/mq_rrc_to_tdma"
#define MQ_PHY_TO_RRC "/mq_phy_to_rrc"
#define MQ_RRC_TO_PHY "/mq_rrc_to_phy"

// Semaphore names
#define SEM_QUEUE_MUTEX "/rrc_queue_sem"
#define SEM_APP_RRC_MUTEX "/rrc_app_sem"

// Message queue configuration
#define MQ_MAX_MESSAGES 10
#define MQ_MESSAGE_SIZE 8192  // Must be >= sizeof(IPC_Message), using 8192 for safety
#define APP_RRC_QUEUE_SIZE 20

// ============================================================================
// OLSR MESSAGE STRUCTURES
// ============================================================================

#define OLSR_HELLO_MESSAGE 1
#define OLSR_TC_MESSAGE 2

struct hello_neighbor {
    uint32_t neighbor_id;
    uint8_t link_code;
};

struct two_hop_hello_neighbor {
    uint32_t neighbor_id;
    uint8_t link_code;
};

struct olsr_hello {
    uint16_t hello_interval;
    uint8_t willingness;
    uint8_t reserved;
    int reserved_slot;
    struct hello_neighbor *neighbors;
    int neighbor_count;
    uint8_t two_hop_count;
    struct two_hop_hello_neighbor *two_hop_neighbors;
};

struct tc_neighbor {
    uint32_t neighbor_addr;
};

struct olsr_tc {
    uint16_t ansn;
    struct tc_neighbor *mpr_selectors;
    int selector_count;
};

struct olsr_message {
    uint8_t msg_type;
    uint8_t vtime;
    uint16_t msg_size;
    uint32_t originator;
    uint8_t ttl;
    uint8_t hop_count;
    uint16_t msg_seq_num;
    void *body;
};

// ============================================================================
// IPC MESSAGE STRUCTURES
// ============================================================================

typedef enum {
    // OLSR → RRC
    MSG_OLSR_ROUTE_UPDATE = 1,
    MSG_OLSR_MESSAGE = 2,
    
    // RRC → OLSR
    MSG_RRC_ROUTE_REQUEST = 10,
    
    // TDMA → RRC
    MSG_TDMA_SLOT_STATUS_UPDATE = 20,
    MSG_TDMA_RX_QUEUE_DATA = 21,
    
    // RRC → TDMA
    MSG_RRC_SLOT_TABLE_UPDATE = 30,
    
    // Application (shared memory only)
    MSG_APP_DATA_PACKET = 40,
    
    // PHY → RRC
    MSG_PHY_METRICS_UPDATE = 60,
    MSG_PHY_LINK_STATUS_CHANGE = 61,
    
    // RRC → PHY
    MSG_RRC_METRICS_REQUEST = 70,
    
    // Control
    MSG_CONTROL_INIT = 100,
    MSG_CONTROL_SHUTDOWN = 101,
    MSG_CONTROL_STATUS_REQUEST = 102,
    MSG_CONTROL_STATUS_RESPONSE = 103
} MessageType;

typedef struct {
    MessageType type;
    uint8_t dest_node;
    uint32_t request_id;
} RRC_RouteRequest;

typedef struct {
    MessageType type;
    uint8_t dest_node;
    uint8_t next_hop;
    uint8_t hop_count;
    bool route_available;
    uint32_t request_id;
} RRC_RouteResponse;

typedef struct {
    MessageType type;
    uint8_t dest_node;
    uint32_t request_id;
    bool urgent;
} RRC_DiscoveryRequest;

typedef struct {
    MessageType type;
    uint8_t source_node;
    uint8_t received_from;
    struct olsr_message olsr_msg;
    uint32_t timestamp;
    bool forwarded;
} RRC_ForwardOLSRPacket;

typedef struct {
    uint8_t node_id;
    uint8_t slot_id;
    bool is_tx_slot;
    bool is_allocated;
    uint8_t priority;
    uint32_t last_update;
} TDMA_SlotInfo;

typedef struct {
    MessageType type;
    TDMA_SlotInfo slot_table[8];
    uint32_t timestamp;
    uint8_t updated_slot_count;
} RRC_SlotTableUpdate;

typedef struct {
    MessageType type;
    uint8_t frame_count;
    uint8_t source_node;
    uint8_t dest_node;
    bool is_for_self;
    uint32_t timestamp;
} TDMA_RxQueueNotification;

typedef struct {
    MessageType type;
    uint8_t node_id;
    float rssi_dbm;
    float snr_db;
    float per_percent;
    bool link_active;
    uint32_t packet_count;
    uint32_t timestamp;
} PHY_MetricsUpdate;

typedef union {
    MessageType type;
    struct olsr_message olsr_msg;
    RRC_RouteRequest route_request;
    RRC_RouteResponse route_response;
    RRC_DiscoveryRequest discovery_req;
    RRC_ForwardOLSRPacket forward_olsr;
    RRC_SlotTableUpdate slot_table;
    TDMA_RxQueueNotification rx_notify;
    PHY_MetricsUpdate phy_metrics;
} IPC_Message;

// ============================================================================
// DATA TYPE DEFINITIONS
// ============================================================================

typedef enum {
    DATA_TYPE_SMS = 0,
    DATA_TYPE_DIGITAL_VOICE = 1,
    DATA_TYPE_VIDEO_STREAM = 2,
    DATA_TYPE_FILE_TRANSFER = 3,
    DATA_TYPE_ANALOG_VOICE
} DATATYPE;

struct frame {
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

struct queue {
    struct frame data[QUEUE_SIZE];
    int front;
    int back;
};

typedef enum {
    RRC_DATA_TYPE_SMS = 0,
    RRC_DATA_TYPE_VOICE = 1,
    RRC_DATA_TYPE_VIDEO = 2,
    RRC_DATA_TYPE_FILE = 3,
    RRC_DATA_TYPE_PTT = 4,
    RRC_DATA_TYPE_RELAY = 5,
    RRC_DATA_TYPE_UNKNOWN = 99
} RRC_DataType;

typedef enum {
    PRIORITY_ANALOG_VOICE_PTT = 0,
    PRIORITY_DIGITAL_VOICE = 1,
    PRIORITY_DATA_1 = 2,
    PRIORITY_DATA_2 = 3,
    PRIORITY_DATA_3 = 4,
    PRIORITY_RX_RELAY = 4
} MessagePriority;

typedef enum {
    TRANSMISSION_UNICAST = 0,
    TRANSMISSION_MULTICAST = 1,
    TRANSMISSION_BROADCAST = 2
} TransmissionType;

typedef struct {
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

typedef struct {
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

#define RSSI_POOR_THRESHOLD_DBM -90.0f
#define SNR_POOR_THRESHOLD_DB 10.0f
#define PER_POOR_THRESHOLD_PERCENT 50.0f
#define LINK_TIMEOUT_SECONDS 30

typedef struct {
    float rssi_dbm;
    float snr_db;
    float per_percent;
    uint32_t packet_count;
    uint32_t last_update_time;
} PHYMetrics;

// ============================================================================
// MANET WAVEFORM STRUCTURES
// ============================================================================

typedef struct {
    uint16_t nodeID;
    bool active;
    uint64_t lastHeardTime;
    uint8_t txSlots[10];
    uint8_t rxSlots[10];
    PHYMetrics phy;
    uint8_t assignedNCSlot;
} NeighborState;

typedef struct {
    uint64_t ncStatusBitmap;
    uint64_t duGuUsageBitmap;
    uint32_t lastUpdateTime;
} SlotStatus;

typedef struct {
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

typedef struct {
    uint8_t activeNodes[MAX_MONITORED_NODES];
    int activeNodeCount;
    uint8_t ncSlotAssignments[NC_SLOTS_PER_SUPERCYCLE];
    uint8_t currentRoundRobinIndex;
    uint8_t myAssignedNCSlot;
} NCSlotManager;

// ============================================================================
// NC SLOT MESSAGE STRUCTURES
// ============================================================================

typedef struct {
    uint8_t msg_type;
    uint8_t vtime;
    uint16_t msg_size;
    uint32_t originator_addr;
    uint8_t ttl;
    uint8_t hop_count;
    uint16_t msg_seq_num;
    uint8_t payload[2048];
    size_t payload_len;
} OLSRMessage;

typedef struct {
    uint8_t myAssignedNCSlot;
    OLSRMessage olsr_message;
    bool has_olsr_message;
    PiggybackTLV piggyback_tlv;
    bool has_piggyback;
    NeighborState my_neighbor_info;
    bool has_neighbor_info;
    uint32_t timestamp;
    uint16_t sourceNodeID;
    uint32_t sequence_number;
    bool is_valid;
} NCSlotMessage;

#define NC_SLOT_QUEUE_SIZE 10
typedef struct {
    NCSlotMessage messages[NC_SLOT_QUEUE_SIZE];
    int front;
    int back;
    int count;
    pthread_mutex_t mutex;
} NCSlotMessageQueue;

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

typedef enum {
    RRC_STATE_NULL = 0,
    RRC_STATE_IDLE,
    RRC_STATE_CONNECTION_SETUP,
    RRC_STATE_CONNECTED,
    RRC_STATE_RECONFIGURATION,
    RRC_STATE_RELEASE
} RRC_SystemState;

typedef struct {
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

typedef struct {
    struct queue analog_voice_queue;
    struct queue data_from_l3_queue[NUM_PRIORITY];
    struct queue rx_queue;
    NCSlotMessageQueue nc_slot_queue;
    struct queue rrc_relay_queue;
    sem_t queue_mutex;
} SharedQueueData;

typedef struct {
    CustomApplicationPacket app_to_rrc_queue[APP_RRC_QUEUE_SIZE];
    int app_to_rrc_front;
    int app_to_rrc_back;
    int app_to_rrc_count;
    
    CustomApplicationPacket rrc_to_app_queue[APP_RRC_QUEUE_SIZE];
    int rrc_to_app_front;
    int rrc_to_app_back;
    int rrc_to_app_count;
    
    sem_t mutex;
} APP_RRC_SharedMemory;

typedef struct {
    RRC_SystemState current_rrc_state;
    RRC_ConnectionContext connection_pool[RRC_CONNECTION_POOL_SIZE];
    bool fsm_initialized;
    struct {
        uint32_t packets_processed;
        uint32_t messages_enqueued_total;
        uint32_t messages_discarded_no_slots;
    } stats;
} RRC_PrivateState;

typedef struct {
    NeighborState neighbor_table[MAX_MONITORED_NODES];
    int neighbor_count;
    SlotStatus current_slot_status;
    NCSlotManager nc_manager;
    PiggybackTLV current_piggyback_tlv;
} RRC_PrivateNeighborData;

// ============================================================================
// IPC HANDLES
// ============================================================================

static mqd_t mq_olsr_to_rrc = -1;
static mqd_t mq_rrc_to_olsr = -1;
static mqd_t mq_tdma_to_rrc = -1;
static mqd_t mq_rrc_to_tdma = -1;
static mqd_t mq_phy_to_rrc = -1;
static mqd_t mq_rrc_to_phy = -1;

static int shm_queues_fd = -1;
static int shm_app_rrc_fd = -1;

static SharedQueueData *shared_queues = NULL;
static APP_RRC_SharedMemory *app_rrc_shm = NULL;

static RRC_PrivateState rrc_state = {0};
static RRC_PrivateNeighborData rrc_neighbors = {0};

static TDMA_SlotInfo tdma_slot_table[8] = {0};
static bool ipc_initialized = false;

// ============================================================================
// STATIC POOLS AND STATE
// ============================================================================

static ApplicationMessage message_pool[RRC_MESSAGE_POOL_SIZE];
static bool pool_initialized = false;

#define RRC_APP_PACKET_POOL_SIZE 10
typedef struct {
    CustomApplicationPacket packet;
    bool in_use;
} AppPacketPoolEntry;

static AppPacketPoolEntry app_packet_pool[RRC_APP_PACKET_POOL_SIZE];
static bool app_packet_pool_initialized = false;

static struct {
    uint32_t packets_processed;
    uint32_t messages_enqueued_total;
    uint32_t messages_discarded_no_slots;
    uint32_t route_queries;
    uint32_t slot_requests;
    uint32_t poor_links_detected;
} rrc_stats = {0};

static struct {
    uint32_t enqueued;
    uint32_t dequeued;
    uint32_t overflows;
    uint32_t messages_built;
} nc_slot_queue_stats = {0};

#define RRC_DU_GU_SLOT_COUNT 8
static struct {
    bool allocated;
    uint8_t node_id;
    uint8_t priority;
    uint32_t allocation_time;
    uint32_t last_used_time;
} rrc_slot_allocation[RRC_DU_GU_SLOT_COUNT];

static struct {
    uint32_t slot_allocations;
    uint32_t slot_releases;
    uint32_t slot_conflicts;
} rrc_slot_stats = {0};

static struct {
    uint32_t neighbors_added;
    uint32_t neighbors_removed;
    uint32_t neighbors_updated;
    uint32_t neighbor_timeouts;
    uint32_t piggyback_updates;
} neighbor_stats = {0};

static struct {
    uint32_t state_transitions;
    uint32_t setup_success;
    uint32_t setup_failures;
    uint32_t reconfigurations;
    uint32_t inactivity_timeouts;
    uint32_t releases;
    uint32_t power_on_events;
    uint32_t power_off_events;
} rrc_fsm_stats = {0};

static struct {
    uint32_t enqueued;
    uint32_t dequeued;
    uint32_t overflows;
    uint32_t tdma_nc_requests;
} olsr_nc_stats = {0};

static struct {
    uint32_t relay_packets_enqueued;
    uint32_t relay_packets_dequeued;
    uint32_t relay_packets_dropped_ttl;
    uint32_t relay_packets_dropped_full;
    uint32_t relay_packets_to_self;
} relay_stats = {0};

static bool piggyback_active = false;
static uint32_t piggyback_last_update = 0;

// NC reservation queue
static NCReservationRequest reservation_queue[MAX_MONITORED_NODES];
static int reservation_count = 0;

// Thread control
static volatile bool system_running = true;
static pthread_t olsr_handler_thread;
static pthread_t tdma_handler_thread;
static pthread_t app_handler_thread;
static pthread_t phy_handler_thread;
static pthread_t periodic_mgmt_thread;

// Next hop tracking
typedef struct {
    uint8_t dest_node;
    uint32_t update_count;
    uint8_t last_next_hop;
} NextHopUpdateStats;

#define MAX_NEXT_HOP_STATS 40
static NextHopUpdateStats next_hop_stats[MAX_NEXT_HOP_STATS];
static int next_hop_stats_count = 0;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

// IPC initialization
int rrc_ipc_init(void);
void rrc_ipc_cleanup(void);

// Message queue operations
int rrc_send_to_olsr(const IPC_Message *msg);
int rrc_receive_from_olsr(IPC_Message *msg, bool blocking);
int rrc_send_to_tdma(const IPC_Message *msg);
int rrc_receive_from_tdma(IPC_Message *msg, bool blocking);
int rrc_send_to_phy(const IPC_Message *msg);
int rrc_receive_from_phy(IPC_Message *msg, bool blocking);

// Application communication (shared memory)
int rrc_receive_from_app(CustomApplicationPacket *packet, bool blocking);
int rrc_send_to_app(const CustomApplicationPacket *packet);
bool app_to_rrc_queue_is_empty(void);
bool app_to_rrc_queue_is_full(void);
bool rrc_to_app_queue_is_empty(void);
bool rrc_to_app_queue_is_full(void);
int app_to_rrc_queue_count(void);
int rrc_to_app_queue_count(void);
void print_app_rrc_queue_stats(void);

// Shared memory queue operations
void rrc_queue_lock(void);
void rrc_queue_unlock(void);
void rrc_enqueue_shared(struct queue *q, struct frame frame);
struct frame rrc_dequeue_shared(struct queue *q);
bool rrc_is_queue_empty(struct queue *q);
bool rrc_is_queue_full(struct queue *q);

// RRC state operations
RRC_SystemState rrc_get_current_state(void);
void rrc_set_current_state(RRC_SystemState new_state);
RRC_ConnectionContext *rrc_get_connection_ctx(uint8_t dest_node);

// Neighbor operations
NeighborState *rrc_get_neighbor(uint16_t nodeID);
void rrc_update_neighbor(uint16_t nodeID, const NeighborState *state);

// IPC-based external API wrappers
uint8_t ipc_olsr_get_next_hop(uint8_t destination_node_id);
void ipc_olsr_trigger_route_discovery(uint8_t destination_node_id);
bool ipc_tdma_check_slot_available(uint8_t next_hop_node, int priority);
bool ipc_tdma_request_nc_slot(const uint8_t *payload, size_t payload_len, uint8_t *assigned_slot);
void ipc_phy_get_link_metrics(uint8_t node_id, float *rssi, float *snr, float *per);
bool ipc_phy_is_link_active(uint8_t node_id);
uint32_t ipc_phy_get_packet_count(uint8_t node_id);

// Message processing threads
void *rrc_olsr_message_handler(void *arg);
void *rrc_tdma_message_handler(void *arg);
void *rrc_app_message_handler(void *arg);
void *rrc_phy_message_handler(void *arg);
void *rrc_periodic_management_thread(void *arg);

// Thread control
void rrc_start_threads(void);
void rrc_stop_threads(void);
void rrc_signal_handler(int signum);

// Loopback testing
void rrc_loopback_test(void);
void rrc_simulate_app_downlink(void);
void rrc_simulate_tdma_uplink(void);
void rrc_simulate_olsr_route_update(void);
void rrc_simulate_phy_metrics(void);

// Static pool management
void init_message_pool(void);
ApplicationMessage *get_free_message(void);
void release_message(ApplicationMessage *msg);
void init_app_packet_pool(void);
CustomApplicationPacket *get_free_app_packet(void);
void release_app_packet(CustomApplicationPacket *packet);

// PHY layer integration
void update_phy_metrics_for_node(uint8_t node_id);
bool is_link_quality_good(uint8_t node_id);

// NC Slot Message Queue Management
void init_nc_slot_message_queue(void);
void cleanup_nc_slot_message_queue(void);
bool nc_slot_queue_enqueue(const NCSlotMessage *msg);
bool nc_slot_queue_dequeue(NCSlotMessage *msg);
bool nc_slot_queue_is_empty(void);
bool nc_slot_queue_is_full(void);
int nc_slot_queue_count(void);

// NC Slot Message Builders
void build_nc_slot_message(NCSlotMessage *msg, uint8_t nc_slot);
void add_olsr_to_nc_message(NCSlotMessage *msg, const OLSRMessage *olsr_msg);
void add_piggyback_to_nc_message(NCSlotMessage *msg, const PiggybackTLV *piggyback);
void add_neighbor_to_nc_message(NCSlotMessage *msg, const NeighborState *neighbor);
void print_nc_slot_queue_stats(void);

// Relay queue management
void init_relay_queue(void);
bool enqueue_relay_packet(struct frame *relay_frame);
struct frame dequeue_relay_packet(void);
bool should_relay_packet(struct frame *frame);
bool is_packet_for_self(struct frame *frame);
void print_relay_stats(void);
struct frame rrc_tdma_dequeue_relay_packet(void);
bool rrc_has_relay_packets(void);

// RRC configuration
void rrc_set_node_id(uint8_t node_id);
uint8_t rrc_get_node_id(void);

// FSM functions
void init_rrc_fsm(void);
const char *rrc_state_to_string(RRC_SystemState state);
void rrc_transition_to_state(RRC_SystemState new_state, uint8_t dest_node);
RRC_ConnectionContext *rrc_get_connection_context(uint8_t dest_node);
RRC_ConnectionContext *rrc_create_connection_context(uint8_t dest_node);
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

// MANET Waveform Core Functions
void init_nc_slot_manager(void);
uint8_t rrc_get_my_nc_slot(void);
bool rrc_is_my_nc_slot(uint8_t slot);
uint8_t rrc_map_slot_to_nc_index(uint8_t frame, uint8_t slot);
void rrc_update_active_nodes(uint16_t nodeID);
uint8_t rrc_assign_nc_slot(uint16_t nodeID);

// Neighbor State Management
void init_neighbor_state_table(void);
NeighborState *rrc_get_neighbor_state(uint16_t nodeID);
NeighborState *rrc_create_neighbor_state(uint16_t nodeID);
void rrc_update_neighbor_slots(uint16_t nodeID, uint8_t *txSlots, uint8_t *rxSlots);
bool rrc_is_neighbor_tx(uint16_t nodeID, uint8_t slot);
bool rrc_is_neighbor_rx(uint16_t nodeID, uint8_t slot);

// Slot Status Management
void rrc_init_slot_status(void);
void rrc_update_nc_status_bitmap(uint8_t ncSlot, bool active);
void rrc_update_du_gu_usage_bitmap(uint8_t slot, bool willTx);
void rrc_generate_slot_status(SlotStatus *out);

// Piggyback TLV Management
void rrc_init_piggyback_tlv(void);
void rrc_build_piggyback_tlv(PiggybackTLV *tlv);
bool rrc_parse_piggyback_tlv(const uint8_t *data, size_t len, PiggybackTLV *tlv);
void rrc_update_piggyback_ttl(void);

// NC Frame Building
size_t rrc_build_nc_frame(uint8_t *buffer, size_t maxLen);

// Relay Handling
bool rrc_should_relay(struct frame *frame);
void rrc_enqueue_relay_packet(struct frame *frame);

// Uplink processing
int rrc_process_uplink_frame(struct frame *received_frame);
int forward_olsr_packet_to_l3(struct frame *l3_frame);
int deliver_data_packet_to_l7(struct frame *app_frame);
int rrc_deliver_to_application_layer(const CustomApplicationPacket *packet);
CustomApplicationPacket *convert_frame_to_app_packet(const struct frame *frame);
void generate_slot_assignment_failure_message(uint8_t node_id);

// Application feedback
void notify_application_of_failure(uint8_t dest_node, const char *reason);
void notify_successful_delivery(uint8_t dest_node, uint32_t sequence_number);

// Priority-based NC slot allocation
bool rrc_add_nc_reservation(uint16_t nodeID, uint8_t hopCount, bool isSelf,
                            uint8_t trafficType, uint32_t packetCount);
void rrc_process_nc_reservations_by_priority(void);
int rrc_request_nc_reservation_multi_relay(uint16_t dest_node, uint8_t traffic_type,
                                           bool urgent, uint32_t packet_count);
void rrc_cleanup_nc_reservations(void);
uint8_t rrc_assign_nc_slot_with_multi_relay_priority(uint16_t nodeID, uint32_t packet_count);
void print_nc_reservation_priority_status(void);

// TDMA Transfer/Receive Control
int rrc_request_transmit_slot(uint8_t dest_node, MessagePriority priority);
int rrc_confirm_transmit_slot(uint8_t dest_node, uint8_t slot_id);
void rrc_release_transmit_slot(uint8_t dest_node, uint8_t slot_id);
int rrc_setup_receive_slot(uint8_t source_node);
void rrc_handle_received_frame(struct frame *received_frame);
void rrc_cleanup_receive_resources(uint8_t source_node);

// Piggyback support
void rrc_initialize_piggyback_system(void);
void rrc_initialize_piggyback(uint8_t node_id, uint8_t session_id, uint8_t traffic_type, uint8_t reserved_slot);
void rrc_clear_piggyback(void);
bool rrc_should_attach_piggyback(void);
PiggybackTLV *rrc_get_piggyback_data(void);
void rrc_check_start_end_packets(const CustomApplicationPacket *packet);

// Slot status reporting
typedef struct {
    uint8_t frame_number;
    uint8_t slot_number;
    bool is_available;
    uint8_t allocated_node;
    uint8_t priority;
} SlotStatusInfo;

// Removed duplicate declaration - defined below
void rrc_update_nc_schedule(void);
uint8_t rrc_get_current_nc_slot(void);

// ============================================================================
// SHARED MEMORY QUEUE OPERATIONS WITH SEMAPHORE PROTECTION
// ============================================================================

void rrc_queue_lock(void) {
    if (shared_queues != NULL) {
        sem_wait(&shared_queues->queue_mutex);
    }
}

void rrc_queue_unlock(void) {
    if (shared_queues != NULL) {
        sem_post(&shared_queues->queue_mutex);
    }
}

void rrc_enqueue_shared(struct queue *q, struct frame frame) {
    if (q == NULL) return;
    
    rrc_queue_lock();
    
    int next = (q->back + 1) % QUEUE_SIZE;
    if (next != q->front) {
        q->data[q->back] = frame;
        q->back = next;
        rrc_stats.messages_enqueued_total++;
    } else {
        rrc_stats.messages_discarded_no_slots++;
    }
    
    rrc_queue_unlock();
}

struct frame rrc_dequeue_shared(struct queue *q) {
    struct frame result = {0};
    
    if (q == NULL) return result;
    
    rrc_queue_lock();
    
    if (q->front != q->back) {
        result = q->data[q->front];
        q->front = (q->front + 1) % QUEUE_SIZE;
    }
    
    rrc_queue_unlock();
    return result;
}

bool rrc_is_queue_empty(struct queue *q) {
    if (q == NULL) return true;
    
    rrc_queue_lock();
    bool empty = (q->front == q->back);
    rrc_queue_unlock();
    
    return empty;
}

bool rrc_is_queue_full(struct queue *q) {
    if (q == NULL) return false;
    
    rrc_queue_lock();
    bool full = ((q->back + 1) % QUEUE_SIZE == q->front);
    rrc_queue_unlock();
    
    return full;
}

// ============================================================================
// IPC-BASED EXTERNAL API WRAPPERS
// ============================================================================

uint8_t ipc_olsr_get_next_hop(uint8_t destination_node_id) {
    if (!ipc_initialized || mq_rrc_to_olsr == -1 || mq_olsr_to_rrc == -1) {
        printf("RRC-OLSR: IPC not initialized\n");
        return 0xFF;
    }
    
    // Send route request
    IPC_Message request;
    request.route_request.type = MSG_RRC_ROUTE_REQUEST;
    request.route_request.dest_node = destination_node_id;
    request.route_request.request_id = (uint32_t)time(NULL);
    
    if (rrc_send_to_olsr(&request) < 0) {
        printf("RRC-OLSR: Failed to send route request\n");
        return 0xFF;
    }
    
    // Wait for response (with timeout)
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 2; // 2 second timeout
    
    IPC_Message response;
    ssize_t received = mq_timedreceive(mq_olsr_to_rrc, (char*)&response, 
                                       sizeof(response), NULL, &timeout);
    
    if (received > 0 && response.type == MSG_OLSR_ROUTE_UPDATE) {
        if (response.route_response.route_available) {
            printf("RRC-OLSR: Route to node %u via next hop %u (hops=%u)\n",
                   destination_node_id, response.route_response.next_hop,
                   response.route_response.hop_count);
            return response.route_response.next_hop;
        }
    }
    
    printf("RRC-OLSR: No route available to node %u\n", destination_node_id);
    return 0xFF;
}

void ipc_olsr_trigger_route_discovery(uint8_t destination_node_id) {
    if (!ipc_initialized || mq_rrc_to_olsr == -1) return;
    
    IPC_Message request;
    request.discovery_req.type = MSG_RRC_ROUTE_REQUEST;
    request.discovery_req.dest_node = destination_node_id;
    request.discovery_req.request_id = (uint32_t)time(NULL);
    request.discovery_req.urgent = true;
    
    rrc_send_to_olsr(&request);
    printf("RRC-OLSR: Triggered route discovery for node %u\n", destination_node_id);
}

bool ipc_tdma_check_slot_available(uint8_t next_hop_node, int priority) {
    // Check local slot allocation table
    for (int i = 0; i < RRC_DU_GU_SLOT_COUNT; i++) {
        if (!rrc_slot_allocation[i].allocated) {
            return true;
        }
    }
    return false;
}

bool ipc_tdma_request_nc_slot(const uint8_t *payload, size_t payload_len, uint8_t *assigned_slot) {
    // This would send a message to TDMA in full implementation
    // For now, assign based on local NC slot manager
    uint8_t slot = rrc_get_my_nc_slot();
    if (assigned_slot) {
        *assigned_slot = slot;
    }
    return (slot != 0xFF);
}

void ipc_phy_get_link_metrics(uint8_t node_id, float *rssi, float *snr, float *per) {
    if (!ipc_initialized || mq_rrc_to_phy == -1) return;
    
    IPC_Message request;
    request.type = MSG_RRC_METRICS_REQUEST;
    rrc_send_to_phy(&request);
    
    // In full implementation, would wait for PHY response
    // For now, use neighbor table data
    NeighborState *neighbor = rrc_get_neighbor_state(node_id);
    if (neighbor && neighbor->active) {
        if (rssi) *rssi = neighbor->phy.rssi_dbm;
        if (snr) *snr = neighbor->phy.snr_db;
        if (per) *per = neighbor->phy.per_percent;
    }
}

bool ipc_phy_is_link_active(uint8_t node_id) {
    NeighborState *neighbor = rrc_get_neighbor_state(node_id);
    return (neighbor && neighbor->active);
}

uint32_t ipc_phy_get_packet_count(uint8_t node_id) {
    NeighborState *neighbor = rrc_get_neighbor_state(node_id);
    return (neighbor && neighbor->active) ? neighbor->phy.packet_count : 0;
}

// ============================================================================
// IMPLEMENTATION CONTINUES WITH ALL FUNCTIONS FROM rccv2.c
// (Lines 900-5200+ from rccv2.c would follow here with modifications:
//  - Replace extern queue access with shared_queues->
//  - Replace olsr_get_next_hop with ipc_olsr_get_next_hop
//  - Replace olsr_hello_queue/rrc_olsr_nc_queue with shared_queues->nc_slot_queue
//  - Add semaphore locking around all queue operations)
// ============================================================================

// Due to character limits, I'll include key modified functions as examples:

void init_message_pool(void) {
    if (pool_initialized) return;
    
    for (int i = 0; i < RRC_MESSAGE_POOL_SIZE; i++) {
        message_pool[i].in_use = false;
    }
    
    pool_initialized = true;
    printf("RRC: Message pool initialized (%d messages)\n", RRC_MESSAGE_POOL_SIZE);
}

ApplicationMessage *get_free_message(void) {
    if (!pool_initialized) {
        init_message_pool();
    }
    
    for (int i = 0; i < RRC_MESSAGE_POOL_SIZE; i++) {
        if (!message_pool[i].in_use) {
            message_pool[i].in_use = true;
            memset(&message_pool[i], 0, sizeof(ApplicationMessage));
            message_pool[i].in_use = true;
            return &message_pool[i];
        }
    }
    
    printf("RRC: WARNING - Message pool exhausted!\n");
    return NULL;
}

void release_message(ApplicationMessage *msg) {
    if (!msg || !pool_initialized) return;
    
    int index = msg - message_pool;
    if (index >= 0 && index < RRC_MESSAGE_POOL_SIZE) {
        message_pool[index].in_use = false;
    }
}

// ============================================================================
// IPC INITIALIZATION AND CLEANUP
// ============================================================================

int rrc_ipc_init(void) {
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MQ_MAX_MESSAGES;
    attr.mq_msgsize = MQ_MESSAGE_SIZE;
    attr.mq_curmsgs = 0;
    
    printf("RRC: Initializing IPC...\n");
    
    // Unlink any existing queues and shared memory to start fresh
    printf("RRC: Cleaning up any existing IPC resources...\n");
    mq_unlink(MQ_OLSR_TO_RRC);
    mq_unlink(MQ_RRC_TO_OLSR);
    mq_unlink(MQ_TDMA_TO_RRC);
    mq_unlink(MQ_RRC_TO_TDMA);
    mq_unlink(MQ_PHY_TO_RRC);
    mq_unlink(MQ_RRC_TO_PHY);
    shm_unlink(SHM_RRC_QUEUES);
    shm_unlink(SHM_APP_RRC);
    printf("RRC: Cleanup complete. Creating fresh IPC resources with mq_msgsize=%d\n", MQ_MESSAGE_SIZE);
    
    // Verify sizeof(IPC_Message) fits in MQ_MESSAGE_SIZE
    if (sizeof(IPC_Message) > MQ_MESSAGE_SIZE) {
        fprintf(stderr, "ERROR: sizeof(IPC_Message)=%lu > MQ_MESSAGE_SIZE=%d\n", 
                sizeof(IPC_Message), MQ_MESSAGE_SIZE);
        return -1;
    }
    printf("RRC: sizeof(IPC_Message)=%lu, MQ_MESSAGE_SIZE=%d (OK)\n", 
           sizeof(IPC_Message), MQ_MESSAGE_SIZE);
    
    // Open message queues (unidirectional) with O_EXCL to ensure fresh creation
    mq_olsr_to_rrc = mq_open(MQ_OLSR_TO_RRC, O_CREAT | O_EXCL | O_RDONLY, 0644, &attr);
    if (mq_olsr_to_rrc == -1) {
        perror("mq_open(MQ_OLSR_TO_RRC)");
        goto cleanup;
    }
    
    mq_rrc_to_olsr = mq_open(MQ_RRC_TO_OLSR, O_CREAT | O_EXCL | O_WRONLY, 0644, &attr);
    if (mq_rrc_to_olsr == -1) {
        perror("mq_open(MQ_RRC_TO_OLSR)");
        goto cleanup;
    }
    
    mq_tdma_to_rrc = mq_open(MQ_TDMA_TO_RRC, O_CREAT | O_EXCL | O_RDONLY, 0644, &attr);
    if (mq_tdma_to_rrc == -1) {
        perror("mq_open(MQ_TDMA_TO_RRC)");
        goto cleanup;
    }
    
    mq_rrc_to_tdma = mq_open(MQ_RRC_TO_TDMA, O_CREAT | O_EXCL | O_WRONLY, 0644, &attr);
    if (mq_rrc_to_tdma == -1) {
        perror("mq_open(MQ_RRC_TO_TDMA)");
        goto cleanup;
    }
    
    mq_phy_to_rrc = mq_open(MQ_PHY_TO_RRC, O_CREAT | O_EXCL | O_RDONLY, 0644, &attr);
    if (mq_phy_to_rrc == -1) {
        perror("mq_open(MQ_PHY_TO_RRC)");
        goto cleanup;
    }
    
    mq_rrc_to_phy = mq_open(MQ_RRC_TO_PHY, O_CREAT | O_EXCL | O_WRONLY, 0644, &attr);
    if (mq_rrc_to_phy == -1) {
        perror("mq_open(MQ_RRC_TO_PHY)");
        goto cleanup;
    }
    
    printf("RRC: Message queues opened\n");
    
    // Create shared memory for frame queues
    shm_queues_fd = shm_open(SHM_RRC_QUEUES, O_CREAT | O_RDWR, 0644);
    if (shm_queues_fd == -1) {
        perror("shm_open(SHM_RRC_QUEUES)");
        goto cleanup;
    }
    
    if (ftruncate(shm_queues_fd, sizeof(SharedQueueData)) == -1) {
        perror("ftruncate(SharedQueueData)");
        goto cleanup;
    }
    
    shared_queues = (SharedQueueData *)mmap(NULL, sizeof(SharedQueueData),
                                           PROT_READ | PROT_WRITE, MAP_SHARED,
                                           shm_queues_fd, 0);
    if (shared_queues == MAP_FAILED) {
        perror("mmap(shared_queues)");
        shared_queues = NULL;
        goto cleanup;
    }
    
    // Initialize queues
    memset(shared_queues, 0, sizeof(SharedQueueData));
    shared_queues->analog_voice_queue.front = 0;
    shared_queues->analog_voice_queue.back = 0;
    for (int i = 0; i < NUM_PRIORITY; i++) {
        shared_queues->data_from_l3_queue[i].front = 0;
        shared_queues->data_from_l3_queue[i].back = 0;
    }
    shared_queues->rx_queue.front = 0;
    shared_queues->rx_queue.back = 0;
    shared_queues->rrc_relay_queue.front = 0;
    shared_queues->rrc_relay_queue.back = 0;
    
    // Initialize NC slot queue
    shared_queues->nc_slot_queue.front = 0;
    shared_queues->nc_slot_queue.back = 0;
    shared_queues->nc_slot_queue.count = 0;
    pthread_mutex_init(&shared_queues->nc_slot_queue.mutex, NULL);
    
    // Initialize semaphore
    if (sem_init(&shared_queues->queue_mutex, 1, 1) == -1) {
        perror("sem_init(queue_mutex)");
        goto cleanup;
    }
    
    printf("RRC: Shared memory (queues) initialized\n");
    
    // Create shared memory for app-rrc communication
    shm_app_rrc_fd = shm_open(SHM_APP_RRC, O_CREAT | O_RDWR, 0644);
    if (shm_app_rrc_fd == -1) {
        perror("shm_open(SHM_APP_RRC)");
        goto cleanup;
    }
    
    if (ftruncate(shm_app_rrc_fd, sizeof(APP_RRC_SharedMemory)) == -1) {
        perror("ftruncate(APP_RRC_SharedMemory)");
        goto cleanup;
    }
    
    app_rrc_shm = (APP_RRC_SharedMemory *)mmap(NULL, sizeof(APP_RRC_SharedMemory),
                                               PROT_READ | PROT_WRITE, MAP_SHARED,
                                               shm_app_rrc_fd, 0);
    if (app_rrc_shm == MAP_FAILED) {
        perror("mmap(app_rrc_shm)");
        app_rrc_shm = NULL;
        goto cleanup;
    }
    
    // Initialize app queues
    memset(app_rrc_shm, 0, sizeof(APP_RRC_SharedMemory));
    app_rrc_shm->app_to_rrc_front = 0;
    app_rrc_shm->app_to_rrc_back = 0;
    app_rrc_shm->app_to_rrc_count = 0;
    app_rrc_shm->rrc_to_app_front = 0;
    app_rrc_shm->rrc_to_app_back = 0;
    app_rrc_shm->rrc_to_app_count = 0;
    
    if (sem_init(&app_rrc_shm->mutex, 1, 1) == -1) {
        perror("sem_init(app_rrc_mutex)");
        goto cleanup;
    }
    
    printf("RRC: Shared memory (app-rrc) initialized\n");
    
    ipc_initialized = true;
    printf("RRC: IPC initialization complete\n");
    return 0;
    
cleanup:
    rrc_ipc_cleanup();
    return -1;
}

void rrc_ipc_cleanup(void) {
    printf("RRC: Cleaning up IPC resources...\n");
    
    // Close message queues
    if (mq_olsr_to_rrc != -1) {
        mq_close(mq_olsr_to_rrc);
        mq_unlink(MQ_OLSR_TO_RRC);
    }
    if (mq_rrc_to_olsr != -1) {
        mq_close(mq_rrc_to_olsr);
        mq_unlink(MQ_RRC_TO_OLSR);
    }
    if (mq_tdma_to_rrc != -1) {
        mq_close(mq_tdma_to_rrc);
        mq_unlink(MQ_TDMA_TO_RRC);
    }
    if (mq_rrc_to_tdma != -1) {
        mq_close(mq_rrc_to_tdma);
        mq_unlink(MQ_RRC_TO_TDMA);
    }
    if (mq_phy_to_rrc != -1) {
        mq_close(mq_phy_to_rrc);
        mq_unlink(MQ_PHY_TO_RRC);
    }
    if (mq_rrc_to_phy != -1) {
        mq_close(mq_rrc_to_phy);
        mq_unlink(MQ_RRC_TO_PHY);
    }
    
    // Cleanup shared memory
    if (shared_queues != NULL) {
        sem_destroy(&shared_queues->queue_mutex);
        pthread_mutex_destroy(&shared_queues->nc_slot_queue.mutex);
        munmap(shared_queues, sizeof(SharedQueueData));
        shared_queues = NULL;
    }
    if (shm_queues_fd != -1) {
        close(shm_queues_fd);
        shm_unlink(SHM_RRC_QUEUES);
    }
    
    if (app_rrc_shm != NULL) {
        sem_destroy(&app_rrc_shm->mutex);
        munmap(app_rrc_shm, sizeof(APP_RRC_SharedMemory));
        app_rrc_shm = NULL;
    }
    if (shm_app_rrc_fd != -1) {
        close(shm_app_rrc_fd);
        shm_unlink(SHM_APP_RRC);
    }
    
    ipc_initialized = false;
    printf("RRC: IPC cleanup complete\n");
}

// ============================================================================
// MESSAGE QUEUE SEND/RECEIVE OPERATIONS
// ============================================================================

int rrc_send_to_olsr(const IPC_Message *msg) {
    if (!ipc_initialized || mq_rrc_to_olsr == -1 || msg == NULL) {
        return -1;
    }
    
    if (mq_send(mq_rrc_to_olsr, (const char *)msg, sizeof(IPC_Message), 0) == -1) {
        perror("mq_send(rrc_to_olsr)");
        return -1;
    }
    
    return 0;
}

int rrc_receive_from_olsr(IPC_Message *msg, bool blocking) {
    if (!ipc_initialized || mq_olsr_to_rrc == -1 || msg == NULL) {
        return -1;
    }
    
    if (blocking) {
        ssize_t received = mq_receive(mq_olsr_to_rrc, (char *)msg, MQ_MESSAGE_SIZE, NULL);
        if (received == -1) {
            if (errno == EMSGSIZE) {
                // Discard oversized message
                char discard[MQ_MESSAGE_SIZE];
                mq_receive(mq_olsr_to_rrc, discard, MQ_MESSAGE_SIZE, NULL);
            }
            return -1;
        }
        return (int)received;
    } else {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 1000000; // 1ms timeout for non-blocking
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000;
        }
        
        ssize_t received = mq_timedreceive(mq_olsr_to_rrc, (char *)msg, 
                                          MQ_MESSAGE_SIZE, NULL, &timeout);
        if (received == -1) {
            if (errno == EMSGSIZE) {
                // Discard oversized message
                char discard[MQ_MESSAGE_SIZE];
                mq_timedreceive(mq_olsr_to_rrc, discard, MQ_MESSAGE_SIZE, NULL, &timeout);
            }
            return -1;
        }
        return (int)received;
    }
}

int rrc_send_to_tdma(const IPC_Message *msg) {
    if (!ipc_initialized || mq_rrc_to_tdma == -1 || msg == NULL) {
        return -1;
    }
    
    if (mq_send(mq_rrc_to_tdma, (const char *)msg, sizeof(IPC_Message), 0) == -1) {
        perror("mq_send(rrc_to_tdma)");
        return -1;
    }
    
    return 0;
}

int rrc_receive_from_tdma(IPC_Message *msg, bool blocking) {
    if (!ipc_initialized || mq_tdma_to_rrc == -1 || msg == NULL) {
        return -1;
    }
    
    if (blocking) {
        ssize_t received = mq_receive(mq_tdma_to_rrc, (char *)msg, MQ_MESSAGE_SIZE, NULL);
        if (received == -1) {
            if (errno == EMSGSIZE) {
                char discard[MQ_MESSAGE_SIZE];
                mq_receive(mq_tdma_to_rrc, discard, MQ_MESSAGE_SIZE, NULL);
            }
            return -1;
        }
        return (int)received;
    } else {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 1000000;
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000;
        }
        
        ssize_t received = mq_timedreceive(mq_tdma_to_rrc, (char *)msg,
                                          MQ_MESSAGE_SIZE, NULL, &timeout);
        if (received == -1) {
            if (errno == EMSGSIZE) {
                char discard[MQ_MESSAGE_SIZE];
                mq_timedreceive(mq_tdma_to_rrc, discard, MQ_MESSAGE_SIZE, NULL, &timeout);
            }
            return -1;
        }
        return (int)received;
    }
}

int rrc_send_to_phy(const IPC_Message *msg) {
    if (!ipc_initialized || mq_rrc_to_phy == -1 || msg == NULL) {
        return -1;
    }
    
    if (mq_send(mq_rrc_to_phy, (const char *)msg, sizeof(IPC_Message), 0) == -1) {
        perror("mq_send(rrc_to_phy)");
        return -1;
    }
    
    return 0;
}

int rrc_receive_from_phy(IPC_Message *msg, bool blocking) {
    if (!ipc_initialized || mq_phy_to_rrc == -1 || msg == NULL) {
        return -1;
    }
    
    if (blocking) {
        ssize_t received = mq_receive(mq_phy_to_rrc, (char *)msg, MQ_MESSAGE_SIZE, NULL);
        if (received == -1) {
            if (errno == EMSGSIZE) {
                char discard[MQ_MESSAGE_SIZE];
                mq_receive(mq_phy_to_rrc, discard, MQ_MESSAGE_SIZE, NULL);
            }
            return -1;
        }
        return (int)received;
    } else {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 1000000;
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000;
        }
        
        ssize_t received = mq_timedreceive(mq_phy_to_rrc, (char *)msg,
                                          MQ_MESSAGE_SIZE, NULL, &timeout);
        if (received == -1) {
            if (errno == EMSGSIZE) {
                char discard[MQ_MESSAGE_SIZE];
                mq_timedreceive(mq_phy_to_rrc, discard, MQ_MESSAGE_SIZE, NULL, &timeout);
            }
            return -1;
        }
        return (int)received;
    }
}

// ============================================================================
// APP-RRC SHARED MEMORY OPERATIONS
// ============================================================================

int rrc_receive_from_app(CustomApplicationPacket *packet, bool blocking) {
    if (app_rrc_shm == NULL || packet == NULL) {
        return -1;
    }
    
    if (blocking) {
        sem_wait(&app_rrc_shm->mutex);
    } else {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 1000000; // 1ms timeout
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000;
        }
        
        if (sem_timedwait(&app_rrc_shm->mutex, &timeout) == -1) {
            return -1;
        }
    }
    
    // Check if queue is empty
    if (app_rrc_shm->app_to_rrc_count == 0) {
        sem_post(&app_rrc_shm->mutex);
        return -1;
    }
    
    // Dequeue
    *packet = app_rrc_shm->app_to_rrc_queue[app_rrc_shm->app_to_rrc_front];
    app_rrc_shm->app_to_rrc_front = (app_rrc_shm->app_to_rrc_front + 1) % APP_RRC_QUEUE_SIZE;
    app_rrc_shm->app_to_rrc_count--;
    
    sem_post(&app_rrc_shm->mutex);
    return 0;
}

int rrc_send_to_app(const CustomApplicationPacket *packet) {
    if (app_rrc_shm == NULL || packet == NULL) {
        return -1;
    }
    
    sem_wait(&app_rrc_shm->mutex);
    
    // Check if queue is full
    if (app_rrc_shm->rrc_to_app_count >= APP_RRC_QUEUE_SIZE) {
        sem_post(&app_rrc_shm->mutex);
        printf("RRC-APP: rrc_to_app queue full, packet dropped\n");
        return -1;
    }
    
    // Enqueue
    app_rrc_shm->rrc_to_app_queue[app_rrc_shm->rrc_to_app_back] = *packet;
    app_rrc_shm->rrc_to_app_back = (app_rrc_shm->rrc_to_app_back + 1) % APP_RRC_QUEUE_SIZE;
    app_rrc_shm->rrc_to_app_count++;
    
    sem_post(&app_rrc_shm->mutex);
    return 0;
}

bool app_to_rrc_queue_is_empty(void) {
    if (app_rrc_shm == NULL) return true;
    
    sem_wait(&app_rrc_shm->mutex);
    bool empty = (app_rrc_shm->app_to_rrc_count == 0);
    sem_post(&app_rrc_shm->mutex);
    
    return empty;
}

bool app_to_rrc_queue_is_full(void) {
    if (app_rrc_shm == NULL) return false;
    
    sem_wait(&app_rrc_shm->mutex);
    bool full = (app_rrc_shm->app_to_rrc_count >= APP_RRC_QUEUE_SIZE);
    sem_post(&app_rrc_shm->mutex);
    
    return full;
}

bool rrc_to_app_queue_is_empty(void) {
    if (app_rrc_shm == NULL) return true;
    
    sem_wait(&app_rrc_shm->mutex);
    bool empty = (app_rrc_shm->rrc_to_app_count == 0);
    sem_post(&app_rrc_shm->mutex);
    
    return empty;
}

bool rrc_to_app_queue_is_full(void) {
    if (app_rrc_shm == NULL) return false;
    
    sem_wait(&app_rrc_shm->mutex);
    bool full = (app_rrc_shm->rrc_to_app_count >= APP_RRC_QUEUE_SIZE);
    sem_post(&app_rrc_shm->mutex);
    
    return full;
}

int app_to_rrc_queue_count(void) {
    if (app_rrc_shm == NULL) return 0;
    
    sem_wait(&app_rrc_shm->mutex);
    int count = app_rrc_shm->app_to_rrc_count;
    sem_post(&app_rrc_shm->mutex);
    
    return count;
}

int rrc_to_app_queue_count(void) {
    if (app_rrc_shm == NULL) return 0;
    
    sem_wait(&app_rrc_shm->mutex);
    int count = app_rrc_shm->rrc_to_app_count;
    sem_post(&app_rrc_shm->mutex);
    
    return count;
}

void print_app_rrc_queue_stats(void) {
    if (app_rrc_shm == NULL) {
        printf("App-RRC shared memory not initialized\n");
        return;
    }
    
    sem_wait(&app_rrc_shm->mutex);
    printf("\n=== App-RRC Queue Statistics ===\n");
    printf("App→RRC: %d/%d messages\n", 
           app_rrc_shm->app_to_rrc_count, APP_RRC_QUEUE_SIZE);
    printf("RRC→App: %d/%d messages\n",
           app_rrc_shm->rrc_to_app_count, APP_RRC_QUEUE_SIZE);
    sem_post(&app_rrc_shm->mutex);
}

// ============================================================================
// STATIC POOL MANAGEMENT IMPLEMENTATIONS
// ============================================================================

void init_app_packet_pool(void) {
    if (app_packet_pool_initialized) return;
    
    for (int i = 0; i < RRC_APP_PACKET_POOL_SIZE; i++) {
        app_packet_pool[i].in_use = false;
        memset(&app_packet_pool[i].packet, 0, sizeof(CustomApplicationPacket));
    }
    app_packet_pool_initialized = true;
    
    printf("RRC: Application packet pool initialized (%d packets)\n", RRC_APP_PACKET_POOL_SIZE);
}

CustomApplicationPacket *get_free_app_packet(void) {
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

void release_app_packet(CustomApplicationPacket *packet) {
    if (!packet) return;
    
    for (int i = 0; i < RRC_APP_PACKET_POOL_SIZE; i++) {
        if (&app_packet_pool[i].packet == packet) {
            app_packet_pool[i].in_use = false;
            memset(&app_packet_pool[i].packet, 0, sizeof(CustomApplicationPacket));
            break;
        }
    }
}

// Duplicate function implementations removed (already defined above)
// get_free_message() and release_message() are defined once at lines 1054-1077

// ============================================================================
// RRC NODE CONFIGURATION
// ============================================================================

void rrc_set_node_id(uint8_t node_id) {
    rrc_node_id = node_id;
    printf("RRC: Node ID set to %u\n", rrc_node_id);
}

uint8_t rrc_get_node_id(void) {
    return rrc_node_id;
}

// ============================================================================
// RRC STATE OPERATIONS
// ============================================================================

RRC_SystemState rrc_get_current_state(void) {
    return rrc_state.current_rrc_state;
}

void rrc_set_current_state(RRC_SystemState new_state) {
    rrc_state.current_rrc_state = new_state;
}

RRC_ConnectionContext *rrc_get_connection_ctx(uint8_t dest_node) {
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (rrc_state.connection_pool[i].active && 
            rrc_state.connection_pool[i].dest_node_id == dest_node) {
            return &rrc_state.connection_pool[i];
        }
    }
    return NULL;
}

// ============================================================================
// NEIGHBOR OPERATIONS
// ============================================================================

NeighborState *rrc_get_neighbor(uint16_t nodeID) {
    for (int i = 0; i < rrc_neighbors.neighbor_count; i++) {
        if (rrc_neighbors.neighbor_table[i].nodeID == nodeID) {
            return &rrc_neighbors.neighbor_table[i];
        }
    }
    return NULL;
}

void rrc_update_neighbor(uint16_t nodeID, const NeighborState *state) {
    NeighborState *existing = rrc_get_neighbor(nodeID);
    if (existing && state) {
        *existing = *state;
        existing->lastHeardTime = (uint64_t)time(NULL);
    }
}

// ============================================================================
// PHY LAYER INTEGRATION
// ============================================================================

void update_phy_metrics_for_node(uint8_t node_id) {
    if (node_id == 0) return;
    
    float rssi, snr, per;
    ipc_phy_get_link_metrics(node_id, &rssi, &snr, &per);
    bool link_active = ipc_phy_is_link_active(node_id);
    uint32_t packet_count = ipc_phy_get_packet_count(node_id);
    
    NeighborState *neighbor = rrc_create_neighbor_state(node_id);
    if (neighbor) {
        neighbor->phy.rssi_dbm = rssi;
        neighbor->phy.snr_db = snr;
        neighbor->phy.per_percent = per;
        neighbor->phy.packet_count = packet_count;
        neighbor->phy.last_update_time = (uint32_t)time(NULL);
        neighbor->active = link_active;
        
        if (rssi < RSSI_POOR_THRESHOLD_DBM || snr < SNR_POOR_THRESHOLD_DB ||
            per > PER_POOR_THRESHOLD_PERCENT) {
            printf("RRC: WARNING - Poor link quality for node %u (RSSI: %.1f, SNR: %.1f, PER: %.1f%%)\n",
                   node_id, rssi, snr, per);
            rrc_stats.poor_links_detected++;
        }
    }
}

bool is_link_quality_good(uint8_t node_id) {
    NeighborState *neighbor = rrc_get_neighbor_state(node_id);
    if (!neighbor) return false;
    
    uint32_t age = (uint32_t)time(NULL) - neighbor->phy.last_update_time;
    if (age > LINK_TIMEOUT_SECONDS) return false;
    
    return (neighbor->active &&
            neighbor->phy.per_percent <= PER_POOR_THRESHOLD_PERCENT &&
            neighbor->phy.rssi_dbm >= RSSI_POOR_THRESHOLD_DBM &&
            neighbor->phy.snr_db >= SNR_POOR_THRESHOLD_DB);
}

// ============================================================================
// MANET WAVEFORM CORE IMPLEMENTATIONS (IPC-Modified)
// ============================================================================

void init_nc_slot_manager(void) {
    rrc_neighbors.nc_manager.activeNodeCount = 0;
    rrc_neighbors.nc_manager.currentRoundRobinIndex = 0;
    rrc_neighbors.nc_manager.myAssignedNCSlot = rrc_assign_nc_slot(rrc_node_id);
    
    for (int i = 0; i < MAX_MONITORED_NODES; i++) {
        rrc_neighbors.nc_manager.activeNodes[i] = 0;
    }
    
    printf("RRC: NC Slot Manager initialized - My NC slot: %u\n", 
           rrc_neighbors.nc_manager.myAssignedNCSlot);
}

uint8_t rrc_get_my_nc_slot(void) {
    return rrc_neighbors.nc_manager.myAssignedNCSlot;
}

bool rrc_is_my_nc_slot(uint8_t slot) {
    return (slot == rrc_neighbors.nc_manager.myAssignedNCSlot);
}

uint8_t rrc_map_slot_to_nc_index(uint8_t frame, uint8_t slot) {
    if (slot < 8 || slot > 9) return 0;
    
    uint8_t cycle = (frame / 10) % 2;
    uint8_t nc_index = (cycle * 20) + (frame * 2) + (slot - 8) + 1;
    
    return (nc_index > 40) ? (nc_index % 40) + 1 : nc_index;
}

static bool rrc_is_nc_slot_conflicted(uint8_t ncSlot, uint16_t myNode) {
    if (ncSlot == 0 || ncSlot > NC_SLOTS_PER_SUPERCYCLE) return true;
    
    uint64_t mask = 1ULL << (ncSlot - 1);
    
    if ((rrc_neighbors.current_slot_status.ncStatusBitmap & mask) != 0) {
        for (int i = 0; i < rrc_neighbors.neighbor_count; i++) {
            if (rrc_neighbors.neighbor_table[i].active && 
                rrc_neighbors.neighbor_table[i].assignedNCSlot == ncSlot) {
                if (rrc_neighbors.neighbor_table[i].nodeID == myNode) {
                    return false;
                }
                return true;
            }
        }
        return true;
    }
    
    for (int i = 0; i < rrc_neighbors.neighbor_count; i++) {
        if (rrc_neighbors.neighbor_table[i].active && 
            rrc_neighbors.neighbor_table[i].assignedNCSlot == ncSlot &&
            rrc_neighbors.neighbor_table[i].nodeID != myNode) {
            return true;
        }
    }
    
    return false;
}

static uint8_t rrc_pick_nc_slot_seedex(uint16_t nodeID, uint32_t epoch) {
    const int MAX_TRIES = 16;
    
    for (int t = 0; t < MAX_TRIES; ++t) {
        uint32_t k = ((uint32_t)nodeID << 16) ^ epoch ^ ((uint32_t)t * 0x9e3779b1u);
        k = (k ^ (k >> 16)) * 0x45d9f3b;
        k = (k ^ (k >> 16)) * 0x45d9f3b;
        k = k ^ (k >> 16);
        
        uint8_t slot = (uint8_t)((k % NC_SLOTS_PER_SUPERCYCLE) + 1);
        
        if (!rrc_is_nc_slot_conflicted(slot, nodeID)) return slot;
    }
    
    uint8_t start = (uint8_t)((nodeID % NC_SLOTS_PER_SUPERCYCLE) + 1);
    for (int i = 0; i < NC_SLOTS_PER_SUPERCYCLE; ++i) {
        uint8_t slot = (uint8_t)(((start - 1 + i) % NC_SLOTS_PER_SUPERCYCLE) + 1);
        if (!rrc_is_nc_slot_conflicted(slot, nodeID)) return slot;
    }
    
    return 0;
}

void rrc_update_active_nodes(uint16_t nodeID) {
    for (int i = 0; i < rrc_neighbors.nc_manager.activeNodeCount; i++) {
        if (rrc_neighbors.nc_manager.activeNodes[i] == nodeID) {
            return;
        }
    }
    
    if (rrc_neighbors.nc_manager.activeNodeCount < MAX_MONITORED_NODES) {
        rrc_neighbors.nc_manager.activeNodes[rrc_neighbors.nc_manager.activeNodeCount] = nodeID;
        rrc_neighbors.nc_manager.activeNodeCount++;
        printf("RRC: Added active node %u (total: %u)\n", nodeID, rrc_neighbors.nc_manager.activeNodeCount);
    }
}

uint8_t rrc_assign_nc_slot(uint16_t nodeID) {
    if (nodeID == 0) return 0;
    
    uint32_t epoch = (uint32_t)time(NULL);
    
    if (rrc_neighbors.nc_manager.activeNodeCount > 0 && 
        rrc_neighbors.nc_manager.activeNodeCount <= NC_SLOTS_PER_SUPERCYCLE) {
        uint8_t candidate = (uint8_t)((nodeID % rrc_neighbors.nc_manager.activeNodeCount) + 1);
        if (candidate == 0) candidate = 1;
        
        if (!rrc_is_nc_slot_conflicted(candidate, nodeID)) {
            rrc_update_nc_status_bitmap(candidate, true);
            
            NeighborState *n = rrc_get_neighbor_state(nodeID);
            if (!n) n = rrc_create_neighbor_state(nodeID);
            if (n) n->assignedNCSlot = candidate;
            
            neighbor_stats.neighbors_added++;
            printf("RRC: Round-robin assigned NC slot %u to node %u\n", candidate, nodeID);
            return candidate;
        }
    }
    
    uint8_t slot = rrc_pick_nc_slot_seedex(nodeID, epoch);
    if (slot != 0) {
        rrc_update_nc_status_bitmap(slot, true);
        
        NeighborState *n = rrc_get_neighbor_state(nodeID);
        if (!n) n = rrc_create_neighbor_state(nodeID);
        if (n) n->assignedNCSlot = slot;
        
        neighbor_stats.neighbors_added++;
        printf("RRC: Seedex assigned NC slot %u to node %u\n", slot, nodeID);
        return slot;
    }
    
    printf("RRC: Failed to assign NC slot to node %u\n", nodeID);
    return 0;
}

// ============================================================================
// NEIGHBOR STATE MANAGEMENT (IPC-Modified)
// ============================================================================

void init_neighbor_state_table(void) {
    rrc_neighbors.neighbor_count = 0;
    
    for (int i = 0; i < MAX_MONITORED_NODES; i++) {
        rrc_neighbors.neighbor_table[i].active = false;
        rrc_neighbors.neighbor_table[i].nodeID = 0;
        rrc_neighbors.neighbor_table[i].assignedNCSlot = 0;
    }
    
    printf("RRC: Neighbor state table initialized\n");
}

NeighborState *rrc_get_neighbor_state(uint16_t nodeID) {
    return rrc_get_neighbor(nodeID);
}

NeighborState *rrc_create_neighbor_state(uint16_t nodeID) {
    NeighborState *existing = rrc_get_neighbor(nodeID);
    if (existing) return existing;
    
    for (int i = 0; i < MAX_MONITORED_NODES; i++) {
        if (!rrc_neighbors.neighbor_table[i].active) {
            rrc_neighbors.neighbor_table[i].active = true;
            rrc_neighbors.neighbor_table[i].nodeID = nodeID;
            rrc_neighbors.neighbor_table[i].lastHeardTime = (uint64_t)time(NULL);
            rrc_neighbors.neighbor_count++;
            
            neighbor_stats.neighbors_added++;
            printf("RRC: Created neighbor state for node %u\n", nodeID);
            return &rrc_neighbors.neighbor_table[i];
        }
    }
    
    printf("RRC: Neighbor table full, cannot add node %u\n", nodeID);
    return NULL;
}

void rrc_update_neighbor_slots(uint16_t nodeID, uint8_t *txSlots, uint8_t *rxSlots) {
    NeighborState *neighbor = rrc_get_neighbor(nodeID);
    if (!neighbor) {
        neighbor = rrc_create_neighbor_state(nodeID);
        if (!neighbor) return;
    }
    
    if (txSlots) memcpy(neighbor->txSlots, txSlots, 10);
    if (rxSlots) memcpy(neighbor->rxSlots, rxSlots, 10);
    
    neighbor->lastHeardTime = (uint64_t)time(NULL);
    neighbor_stats.neighbors_updated++;
}

bool rrc_is_neighbor_tx(uint16_t nodeID, uint8_t slot) {
    NeighborState *neighbor = rrc_get_neighbor(nodeID);
    if (!neighbor || slot >= 10) return false;
    return (neighbor->txSlots[slot] != 0);
}

bool rrc_is_neighbor_rx(uint16_t nodeID, uint8_t slot) {
    NeighborState *neighbor = rrc_get_neighbor(nodeID);
    if (!neighbor || slot >= 10) return false;
    return (neighbor->rxSlots[slot] != 0);
}

// ============================================================================
// SLOT STATUS MANAGEMENT
// ============================================================================

void rrc_init_slot_status(void) {
    rrc_neighbors.current_slot_status.ncStatusBitmap = 0;
    rrc_neighbors.current_slot_status.duGuUsageBitmap = 0;
    rrc_neighbors.current_slot_status.lastUpdateTime = (uint32_t)time(NULL);
    printf("RRC: Slot status initialized\n");
}

void rrc_update_nc_status_bitmap(uint8_t ncSlot, bool active) {
    if (ncSlot == 0 || ncSlot > NC_SLOTS_PER_SUPERCYCLE) return;
    
    uint64_t mask = 1ULL << (ncSlot - 1);
    
    if (active) {
        rrc_neighbors.current_slot_status.ncStatusBitmap |= mask;
    } else {
        rrc_neighbors.current_slot_status.ncStatusBitmap &= ~mask;
    }
    
    rrc_neighbors.current_slot_status.lastUpdateTime = (uint32_t)time(NULL);
}

void rrc_update_du_gu_usage_bitmap(uint8_t slot, bool willTx) {
    if (slot >= 60) return;
    
    uint64_t mask = 1ULL << slot;
    
    if (willTx) {
        rrc_neighbors.current_slot_status.duGuUsageBitmap |= mask;
    } else {
        rrc_neighbors.current_slot_status.duGuUsageBitmap &= ~mask;
    }
}

void rrc_generate_slot_status(SlotStatus *out) {
    if (!out) return;
    
    *out = rrc_neighbors.current_slot_status;
    
    printf("RRC: Generated slot status - NC bitmap: 0x%016llX, DU/GU bitmap: 0x%016llX\n",
           (unsigned long long)out->ncStatusBitmap, (unsigned long long)out->duGuUsageBitmap);
}

// ============================================================================
// PIGGYBACK TLV MANAGEMENT
// ============================================================================

void rrc_init_piggyback_tlv(void) {
    rrc_neighbors.current_piggyback_tlv.type = 0x01;
    rrc_neighbors.current_piggyback_tlv.length = sizeof(PiggybackTLV) - 2;
    rrc_neighbors.current_piggyback_tlv.sourceNodeID = rrc_node_id;
    rrc_neighbors.current_piggyback_tlv.sourceReservations = 0;
    rrc_neighbors.current_piggyback_tlv.relayReservations = 0;
    rrc_neighbors.current_piggyback_tlv.duGuIntentionMap = 0;
    rrc_neighbors.current_piggyback_tlv.ncStatusBitmap = 0;
    rrc_neighbors.current_piggyback_tlv.timeSync = (uint32_t)time(NULL);
    rrc_neighbors.current_piggyback_tlv.myNCSlot = rrc_neighbors.nc_manager.myAssignedNCSlot;
    rrc_neighbors.current_piggyback_tlv.ttl = 10;
    
    printf("RRC: Piggyback TLV system initialized\n");
}

void rrc_build_piggyback_tlv(PiggybackTLV *tlv) {
    if (!tlv) return;
    
    *tlv = rrc_neighbors.current_piggyback_tlv;
    tlv->timeSync = (uint32_t)time(NULL);
    tlv->ncStatusBitmap = rrc_neighbors.current_slot_status.ncStatusBitmap;
    tlv->duGuIntentionMap = rrc_neighbors.current_slot_status.duGuUsageBitmap;
    
    printf("RRC: Built piggyback TLV for NC slot %u\n", tlv->myNCSlot);
}

bool rrc_parse_piggyback_tlv(const uint8_t *data, size_t len, PiggybackTLV *tlv) {
    if (!data || !tlv || len < sizeof(PiggybackTLV)) return false;
    if (data[0] != 0x01) return false;
    
    memcpy(tlv, data, sizeof(PiggybackTLV));
    
    NeighborState *neighbor = rrc_create_neighbor_state(tlv->sourceNodeID);
    if (neighbor) {
        neighbor->lastHeardTime = (uint64_t)time(NULL);
        neighbor->assignedNCSlot = tlv->myNCSlot;
    }
    
    rrc_update_nc_status_bitmap(tlv->myNCSlot, true);
    
    printf("RRC: Parsed piggyback TLV from node %u (NC slot %u)\n",
           tlv->sourceNodeID, tlv->myNCSlot);
    
    return true;
}

void rrc_update_piggyback_ttl(void) {
    if (rrc_neighbors.current_piggyback_tlv.ttl > 0) {
        rrc_neighbors.current_piggyback_tlv.ttl--;
        
        if (rrc_neighbors.current_piggyback_tlv.ttl == 0) {
            printf("RRC: Piggyback TLV expired\n");
        }
    }
}

size_t rrc_build_nc_frame(uint8_t *buffer, size_t maxLen) {
    if (!buffer || maxLen < sizeof(PiggybackTLV)) return 0;
    
    PiggybackTLV tlv;
    rrc_build_piggyback_tlv(&tlv);
    
    memcpy(buffer, &tlv, sizeof(PiggybackTLV));
    
    printf("RRC: Built NC frame with piggyback TLV (%zu bytes)\n", sizeof(PiggybackTLV));
    
    return sizeof(PiggybackTLV);
}

// ============================================================================
// NC SLOT MESSAGE QUEUE FUNCTIONS (IPC-Modified - Unified Queue)
// ============================================================================

void init_nc_slot_message_queue(void) {
    if (shared_queues == NULL) {
        printf("RRC: Warning - shared memory not initialized for NC slot queue\n");
        return;
    }
    
    printf("RRC: NC Slot Message Queue ready (unified queue in shared memory)\n");
}

void cleanup_nc_slot_message_queue(void) {
    // Cleanup handled by rrc_ipc_cleanup()
}

bool nc_slot_queue_enqueue(const NCSlotMessage *msg) {
    if (shared_queues == NULL || msg == NULL) return false;
    
    pthread_mutex_lock(&shared_queues->nc_slot_queue.mutex);
    
    if (shared_queues->nc_slot_queue.count >= NC_SLOT_QUEUE_SIZE) {
        pthread_mutex_unlock(&shared_queues->nc_slot_queue.mutex);
        nc_slot_queue_stats.overflows++;
        return false;
    }
    
    shared_queues->nc_slot_queue.messages[shared_queues->nc_slot_queue.back] = *msg;
    shared_queues->nc_slot_queue.back = (shared_queues->nc_slot_queue.back + 1) % NC_SLOT_QUEUE_SIZE;
    shared_queues->nc_slot_queue.count++;
    
    pthread_mutex_unlock(&shared_queues->nc_slot_queue.mutex);
    nc_slot_queue_stats.enqueued++;
    
    return true;
}

bool nc_slot_queue_dequeue(NCSlotMessage *msg) {
    if (shared_queues == NULL || msg == NULL) return false;
    
    pthread_mutex_lock(&shared_queues->nc_slot_queue.mutex);
    
    if (shared_queues->nc_slot_queue.count == 0) {
        pthread_mutex_unlock(&shared_queues->nc_slot_queue.mutex);
        return false;
    }
    
    *msg = shared_queues->nc_slot_queue.messages[shared_queues->nc_slot_queue.front];
    shared_queues->nc_slot_queue.front = (shared_queues->nc_slot_queue.front + 1) % NC_SLOT_QUEUE_SIZE;
    shared_queues->nc_slot_queue.count--;
    
    pthread_mutex_unlock(&shared_queues->nc_slot_queue.mutex);
    nc_slot_queue_stats.dequeued++;
    
    return true;
}

bool nc_slot_queue_is_empty(void) {
    if (shared_queues == NULL) return true;
    
    pthread_mutex_lock(&shared_queues->nc_slot_queue.mutex);
    bool empty = (shared_queues->nc_slot_queue.count == 0);
    pthread_mutex_unlock(&shared_queues->nc_slot_queue.mutex);
    
    return empty;
}

bool nc_slot_queue_is_full(void) {
    if (shared_queues == NULL) return false;
    
    pthread_mutex_lock(&shared_queues->nc_slot_queue.mutex);
    bool full = (shared_queues->nc_slot_queue.count >= NC_SLOT_QUEUE_SIZE);
    pthread_mutex_unlock(&shared_queues->nc_slot_queue.mutex);
    
    return full;
}

int nc_slot_queue_count(void) {
    if (shared_queues == NULL) return 0;
    
    pthread_mutex_lock(&shared_queues->nc_slot_queue.mutex);
    int count = shared_queues->nc_slot_queue.count;
    pthread_mutex_unlock(&shared_queues->nc_slot_queue.mutex);
    
    return count;
}

void build_nc_slot_message(NCSlotMessage *msg, uint8_t nc_slot) {
    if (!msg) return;
    
    memset(msg, 0, sizeof(NCSlotMessage));
    msg->myAssignedNCSlot = nc_slot;
    msg->sourceNodeID = rrc_node_id;
    msg->timestamp = (uint32_t)time(NULL);
    msg->sequence_number = nc_slot_queue_stats.messages_built++;
    msg->is_valid = true;
    
    printf("RRC: Built NC slot message for slot %u\n", nc_slot);
}

void add_olsr_to_nc_message(NCSlotMessage *msg, const OLSRMessage *olsr_msg) {
    if (!msg || !olsr_msg) return;
    
    msg->olsr_message = *olsr_msg;
    msg->has_olsr_message = true;
    
    printf("RRC: Added OLSR message to NC slot message\n");
}

void add_piggyback_to_nc_message(NCSlotMessage *msg, const PiggybackTLV *piggyback) {
    if (!msg || !piggyback) return;
    
    msg->piggyback_tlv = *piggyback;
    msg->has_piggyback = true;
    
    printf("RRC: Added piggyback TLV to NC slot message\n");
}

void add_neighbor_to_nc_message(NCSlotMessage *msg, const NeighborState *neighbor) {
    if (!msg || !neighbor) return;
    
    msg->my_neighbor_info = *neighbor;
    msg->has_neighbor_info = true;
    
    printf("RRC: Added neighbor info to NC slot message\n");
}

void print_nc_slot_queue_stats(void) {
    printf("\n=== NC Slot Queue Statistics ===\n");
    printf("Enqueued: %u\n", nc_slot_queue_stats.enqueued);
    printf("Dequeued: %u\n", nc_slot_queue_stats.dequeued);
    printf("Overflows: %u\n", nc_slot_queue_stats.overflows);
    printf("Messages Built: %u\n", nc_slot_queue_stats.messages_built);
    printf("Current Count: %d\n", nc_slot_queue_count());
}

// ============================================================================
// RELAY QUEUE MANAGEMENT (IPC-Modified)
// ============================================================================

void init_relay_queue(void) {
    if (shared_queues == NULL) {
        printf("RRC: Warning - shared memory not initialized for relay queue\n");
        return;
    }
    
    shared_queues->rrc_relay_queue.front = 0;
    shared_queues->rrc_relay_queue.back = 0;
    
    relay_stats.relay_packets_enqueued = 0;
    relay_stats.relay_packets_dequeued = 0;
    relay_stats.relay_packets_dropped_ttl = 0;
    relay_stats.relay_packets_dropped_full = 0;
    relay_stats.relay_packets_to_self = 0;
    
    printf("RRC: Relay queue initialized\n");
}

bool should_relay_packet(struct frame *frame) {
    if (!frame) return false;
    if (frame->TTL <= 0) return false;
    if (frame->dest_add == rrc_node_id) return false;
    
    uint8_t next_hop = ipc_olsr_get_next_hop(frame->dest_add);
    if (next_hop == 0 || next_hop == 0xFF) return false;
    
    return true;
}

bool is_packet_for_self(struct frame *frame) {
    if (!frame) return false;
    return (frame->dest_add == rrc_node_id);
}

bool enqueue_relay_packet(struct frame *relay_frame) {
    if (!relay_frame || !should_relay_packet(relay_frame)) {
        if (relay_frame && relay_frame->TTL <= 0) {
            relay_stats.relay_packets_dropped_ttl++;
        }
        return false;
    }
    
    uint8_t new_next_hop = ipc_olsr_get_next_hop(relay_frame->dest_add);
    relay_frame->next_hop_add = new_next_hop;
    relay_frame->TTL--;
    
    if (!rrc_is_queue_full(&shared_queues->rrc_relay_queue)) {
        rrc_enqueue_shared(&shared_queues->rrc_relay_queue, *relay_frame);
        relay_stats.relay_packets_enqueued++;
        
        printf("RRC: Relayed packet - Dest: %u, Next hop: %u, TTL: %d\n",
               relay_frame->dest_add, relay_frame->next_hop_add, relay_frame->TTL);
        return true;
    } else {
        relay_stats.relay_packets_dropped_full++;
        printf("RRC: Relay queue full, dropped packet\n");
        return false;
    }
}

struct frame dequeue_relay_packet(void) {
    if (!rrc_is_queue_empty(&shared_queues->rrc_relay_queue)) {
        relay_stats.relay_packets_dequeued++;
        return rrc_dequeue_shared(&shared_queues->rrc_relay_queue);
    }
    
    struct frame empty = {0};
    return empty;
}

struct frame rrc_tdma_dequeue_relay_packet(void) {
    return dequeue_relay_packet();
}

bool rrc_has_relay_packets(void) {
    return !rrc_is_queue_empty(&shared_queues->rrc_relay_queue);
}

void print_relay_stats(void) {
    printf("\n=== Relay Queue Statistics ===\n");
    printf("Enqueued: %u\n", relay_stats.relay_packets_enqueued);
    printf("Dequeued: %u\n", relay_stats.relay_packets_dequeued);
    printf("Dropped (TTL): %u\n", relay_stats.relay_packets_dropped_ttl);
    printf("Dropped (Full): %u\n", relay_stats.relay_packets_dropped_full);
    printf("Packets to self: %u\n", relay_stats.relay_packets_to_self);
}

// ============================================================================
// PRIORITY-BASED NC SLOT ALLOCATION SYSTEM
// ============================================================================

// Calculate comprehensive priority score (lower = higher priority)
static uint32_t calculate_priority_score(const NCReservationRequest *request) {
    if (!request) return 0xFFFFFFFF;
    
    uint32_t score = 0;
    
    if (request->isSelfReservation) {
        score = 1000; // Highest priority for self
    } else {
        if (request->hopCount <= 2) {
            score = 2000 + (request->hopCount * 100);
        } else {
            score = 2000 + (request->hopCount * 200);
        }
    }
    
    if (request->packetCount > 0) {
        score -= (request->packetCount > 10) ? 10 : request->packetCount;
    }
    
    score += (request->timestamp % 100);
    
    return score;
}

// Priority comparison function for qsort
static int compare_nc_reservations(const void *a, const void *b) {
    const NCReservationRequest *req_a = (const NCReservationRequest *)a;
    const NCReservationRequest *req_b = (const NCReservationRequest *)b;
    
    uint32_t score_a = calculate_priority_score(req_a);
    uint32_t score_b = calculate_priority_score(req_b);
    
    if (score_a < score_b) return -1;
    if (score_a > score_b) return 1;
    return 0;
}

// Add or update NC reservation
bool rrc_add_nc_reservation(uint16_t nodeID, uint8_t hopCount, bool isSelf,
                            uint8_t trafficType, uint32_t packetCount) {
    if (reservation_count >= MAX_MONITORED_NODES) {
        printf("RRC PRIORITY: Reservation queue full\n");
        return false;
    }
    
    // Update existing reservation
    for (int i = 0; i < reservation_count; i++) {
        if (reservation_queue[i].nodeID == nodeID) {
            reservation_queue[i].hopCount = (hopCount < reservation_queue[i].hopCount) ? 
                                            hopCount : reservation_queue[i].hopCount;
            reservation_queue[i].isSelfReservation = isSelf;
            reservation_queue[i].trafficType = trafficType;
            reservation_queue[i].timestamp = (uint32_t)time(NULL);
            reservation_queue[i].packetCount += packetCount;
            
            printf("RRC PRIORITY: Updated reservation node %u (hops:%u, packets:%u)\n",
                   nodeID, reservation_queue[i].hopCount, reservation_queue[i].packetCount);
            return true;
        }
    }
    
    // Add new reservation
    NCReservationRequest *new_req = &reservation_queue[reservation_count];
    new_req->nodeID = nodeID;
    new_req->hopCount = hopCount;
    new_req->isSelfReservation = isSelf;
    new_req->trafficType = trafficType;
    new_req->timestamp = (uint32_t)time(NULL);
    new_req->packetCount = packetCount;
    
    reservation_count++;
    
    printf("RRC PRIORITY: Added reservation node %u (hops:%u, packets:%u)\n",
           nodeID, hopCount, packetCount);
    
    return true;
}

// Check if we can override slot by priority
static bool rrc_can_assign_nc_slot_by_priority(uint8_t slot, uint32_t my_priority_score, 
                                                uint16_t my_node_id) {
    if (slot == 0 || slot > NC_SLOTS_PER_SUPERCYCLE) return false;
    
    for (int i = 0; i < rrc_neighbors.neighbor_count; i++) {
        if (rrc_neighbors.neighbor_table[i].active && 
            rrc_neighbors.neighbor_table[i].assignedNCSlot == slot) {
            
            if (rrc_neighbors.neighbor_table[i].nodeID == my_node_id) return true;
            
            uint32_t owner_priority_score = 0xFFFFFFFF;
            for (int j = 0; j < reservation_count; j++) {
                if (reservation_queue[j].nodeID == rrc_neighbors.neighbor_table[i].nodeID) {
                    owner_priority_score = calculate_priority_score(&reservation_queue[j]);
                    break;
                }
            }
            
            return (my_priority_score < owner_priority_score);
        }
    }
    
    return true;
}

// Override NC slot from lower priority
static void rrc_assign_priority_nc_slot(uint8_t slot, uint16_t new_owner) {
    for (int i = 0; i < rrc_neighbors.neighbor_count; i++) {
        if (rrc_neighbors.neighbor_table[i].active && 
            rrc_neighbors.neighbor_table[i].assignedNCSlot == slot) {
            
            printf("RRC PRIORITY: Reassigning slot %u from node %u to %u\n",
                   slot, rrc_neighbors.neighbor_table[i].nodeID, new_owner);
            
            rrc_neighbors.neighbor_table[i].assignedNCSlot = 0;
            
            if (rrc_neighbors.neighbor_table[i].nodeID == rrc_node_id) {
                printf("RRC PRIORITY: Our slot %u reassigned to higher priority\n", slot);
            }
            break;
        }
    }
    
    rrc_update_nc_status_bitmap(slot, false);
}

// Assign NC slot based on priority
static uint8_t rrc_assign_nc_slot_by_priority(const NCReservationRequest *request) {
    if (!request) return 0;
    
    uint32_t my_priority_score = calculate_priority_score(request);
    
    uint32_t epoch = (uint32_t)time(NULL);
    const int MAX_TRIES = 16;
    
    for (int t = 0; t < MAX_TRIES; t++) {
        uint32_t priority_weight = (0xFFFFFFFF - my_priority_score) / 1000000;
        uint32_t k = ((uint32_t)request->nodeID << 16) ^ epoch ^ 
                     ((uint32_t)t * 0x9e3779b1u) ^ priority_weight;
        
        k = (k ^ (k >> 16)) * 0x45d9f3b;
        k = (k ^ (k >> 16)) * 0x45d9f3b;
        k = k ^ (k >> 16);
        
        uint8_t slot_range = NC_SLOTS_PER_SUPERCYCLE;
        uint8_t slot_offset = 0;
        
        if (request->isSelfReservation) {
            slot_range = NC_SLOTS_PER_SUPERCYCLE;
            slot_offset = 0;
        } else if (request->hopCount <= 2) {
            slot_range = (NC_SLOTS_PER_SUPERCYCLE * 2) / 3;
            slot_offset = 0;
        } else {
            slot_range = (NC_SLOTS_PER_SUPERCYCLE * 2) / 3;
            slot_offset = NC_SLOTS_PER_SUPERCYCLE / 3;
        }
        
        uint8_t slot = (uint8_t)(((k % slot_range) + slot_offset) + 1);
        if (slot > NC_SLOTS_PER_SUPERCYCLE) 
            slot = (slot % NC_SLOTS_PER_SUPERCYCLE) + 1;
        
        if (!rrc_is_nc_slot_conflicted(slot, request->nodeID)) {
            rrc_update_nc_status_bitmap(slot, true);
            return slot;
        }
        
        if (rrc_can_assign_nc_slot_by_priority(slot, my_priority_score, request->nodeID)) {
            rrc_assign_priority_nc_slot(slot, request->nodeID);
            rrc_update_nc_status_bitmap(slot, true);
            return slot;
        }
    }
    
    for (uint8_t slot = 1; slot <= NC_SLOTS_PER_SUPERCYCLE; slot++) {
        if (!rrc_is_nc_slot_conflicted(slot, request->nodeID)) {
            rrc_update_nc_status_bitmap(slot, true);
            return slot;
        }
    }
    
    return 0;
}

void rrc_process_nc_reservations_by_priority(void) {
    if (reservation_count == 0) return;
    
    printf("RRC PRIORITY: Processing %d reservations\n", reservation_count);
    
    qsort(reservation_queue, reservation_count, sizeof(NCReservationRequest), 
          compare_nc_reservations);
    
    for (int i = 0; i < reservation_count; i++) {
        NCReservationRequest *req = &reservation_queue[i];
        uint32_t priority_score = calculate_priority_score(req);
        
        const char *priority_type = req->isSelfReservation ? "SELF" : 
                                   (req->hopCount <= 2 ? "SHORT_HOP" : "LONG_HOP");
        
        printf("RRC PRIORITY: [%d] Node %u - Score:%u, Type:%s, Hops:%u, Packets:%u\n",
               i + 1, req->nodeID, priority_score, priority_type, 
               req->hopCount, req->packetCount);
        
        uint8_t assigned_slot = rrc_assign_nc_slot_by_priority(req);
        if (assigned_slot != 0) {
            printf("RRC PRIORITY: ✅ Assigned slot %u to node %u\n", 
                   assigned_slot, req->nodeID);
            
            NeighborState *neighbor = rrc_get_neighbor_state(req->nodeID);
            if (!neighbor) neighbor = rrc_create_neighbor_state(req->nodeID);
            if (neighbor) neighbor->assignedNCSlot = assigned_slot;
        } else {
            printf("RRC PRIORITY: ❌ Failed to assign slot to node %u\n", req->nodeID);
        }
    }
}

int rrc_request_nc_reservation_multi_relay(uint16_t dest_node, uint8_t traffic_type,
                                           bool urgent, uint32_t packet_count) {
    uint8_t next_hop = ipc_olsr_get_next_hop(dest_node);
    uint8_t hop_count = 1;
    
    if (next_hop == 0 || next_hop == 0xFF) {
        printf("RRC PRIORITY: No route to %u, triggering discovery\n", dest_node);
        ipc_olsr_trigger_route_discovery(dest_node);
        hop_count = 255;
    } else if (next_hop != dest_node) {
        hop_count = 2;
    }
    
    bool is_self = (dest_node == rrc_node_id);
    
    if (rrc_add_nc_reservation(dest_node, hop_count, is_self, traffic_type, packet_count)) {
        printf("RRC PRIORITY: Multi-relay reservation - Dest:%u, Hops:%u, Self:%s, Packets:%u\n",
               dest_node, hop_count, is_self ? "YES" : "NO", packet_count);
        return 0;
    }
    
    return -1;
}

void rrc_cleanup_nc_reservations(void) {
    uint32_t current_time = (uint32_t)time(NULL);
    const uint32_t RESERVATION_TIMEOUT = 30;
    
    int write_index = 0;
    for (int read_index = 0; read_index < reservation_count; read_index++) {
        NCReservationRequest *req = &reservation_queue[read_index];
        uint32_t age = current_time - req->timestamp;
        
        if (age <= RESERVATION_TIMEOUT) {
            if (write_index != read_index)
                reservation_queue[write_index] = *req;
            write_index++;
        } else {
            printf("RRC PRIORITY: Removing expired reservation node %u (age:%u sec)\n",
                   req->nodeID, age);
        }
    }
    
    reservation_count = write_index;
}

uint8_t rrc_assign_nc_slot_with_multi_relay_priority(uint16_t nodeID, uint32_t packet_count) {
    bool is_self = (nodeID == rrc_node_id);
    uint8_t traffic_type = 3;
    uint8_t hop_count = is_self ? 0 : 1;
    
    if (!is_self) {
        uint8_t next_hop = ipc_olsr_get_next_hop(nodeID);
        if (next_hop == 0 || next_hop == 0xFF) {
            hop_count = 255;
        } else if (next_hop != nodeID) {
            hop_count = 2;
        }
    }
    
    rrc_add_nc_reservation(nodeID, hop_count, is_self, traffic_type, packet_count);
    rrc_process_nc_reservations_by_priority();
    rrc_cleanup_nc_reservations();
    
    NeighborState *neighbor = rrc_get_neighbor_state(nodeID);
    if (neighbor && neighbor->assignedNCSlot != 0) {
        return neighbor->assignedNCSlot;
    }
    
    return rrc_assign_nc_slot(nodeID);
}

void print_nc_reservation_priority_status(void) {
    printf("\n=== NC Reservation Priority Status ===\n");
    printf("Active reservations: %d\n", reservation_count);
    printf("Node | Hops | Self | Traffic | Packets | Score  | Type      | Age(s)\n");
    printf("-----|------|------|---------|---------|--------|-----------|-------\n");
    
    uint32_t current_time = (uint32_t)time(NULL);
    
    for (int i = 0; i < reservation_count; i++) {
        NCReservationRequest *req = &reservation_queue[i];
        uint32_t priority_score = calculate_priority_score(req);
        uint32_t age = current_time - req->timestamp;
        
        const char *priority_type = req->isSelfReservation ? "SELF" : 
                                   (req->hopCount <= 2 ? "SHORT_HOP" : "LONG_HOP");
        
        printf(" %3u | %4u | %4s | %7u | %7u | %6u | %-9s | %5u\n",
               req->nodeID, req->hopCount,
               req->isSelfReservation ? "YES" : "NO",
               req->trafficType, req->packetCount, priority_score,
               priority_type, age);
    }
    
    printf("===========================================\n\n");
}

// ============================================================================
// TDMA TRANSFER/RECEIVE CONTROL
// ============================================================================

int rrc_request_transmit_slot(uint8_t dest_node, MessagePriority priority) {
    if (!ipc_tdma_check_slot_available(dest_node, priority)) {
        printf("RRC-TDMA: No available slots for dest %u (priority %d)\n", dest_node, priority);
        return -1;
    }
    
    printf("RRC-TDMA: Transmit slot available for dest %u\n", dest_node);
    return 0;
}

int rrc_confirm_transmit_slot(uint8_t dest_node, uint8_t slot_id) {
    for (int i = 0; i < RRC_DU_GU_SLOT_COUNT; i++) {
        if (!rrc_slot_allocation[i].allocated && rrc_slot_allocation[i].slot_id == slot_id) {
            rrc_slot_allocation[i].allocated = true;
            rrc_slot_allocation[i].dest_node = dest_node;
            rrc_slot_allocation[i].last_used_time = (uint32_t)time(NULL);
            
            printf("RRC-TDMA: Confirmed transmit slot %u for dest %u\n", slot_id, dest_node);
            rrc_slot_stats.slots_allocated++;
            return 0;
        }
    }
    
    printf("RRC-TDMA: Failed to confirm slot %u\n", slot_id);
    return -1;
}

void rrc_release_transmit_slot(uint8_t dest_node, uint8_t slot_id) {
    for (int i = 0; i < RRC_DU_GU_SLOT_COUNT; i++) {
        if (rrc_slot_allocation[i].allocated && 
            rrc_slot_allocation[i].slot_id == slot_id &&
            rrc_slot_allocation[i].dest_node == dest_node) {
            
            rrc_slot_allocation[i].allocated = false;
            rrc_slot_allocation[i].dest_node = 0;
            
            printf("RRC-TDMA: Released transmit slot %u for dest %u\n", slot_id, dest_node);
            rrc_slot_stats.slots_released++;
            return;
        }
    }
}

int rrc_setup_receive_slot(uint8_t source_node) {
    printf("RRC-TDMA: Setting up receive slot for source %u\n", source_node);
    
    NeighborState *neighbor = rrc_create_neighbor_state(source_node);
    if (!neighbor) {
        printf("RRC-TDMA: Failed to create neighbor state for source %u\n", source_node);
        return -1;
    }
    
    neighbor->active = true;
    neighbor->lastHeardTime = (uint64_t)time(NULL);
    
    return 0;
}

void rrc_handle_received_frame(struct frame *received_frame) {
    if (!received_frame) return;
    
    printf("RRC-TDMA: Handling received frame - src:%u, dest:%u, TTL:%d\n",
           received_frame->source_add, received_frame->dest_add, received_frame->TTL);
    
    if (received_frame->dest_add == rrc_node_id) {
        printf("RRC-TDMA: Frame is for us, processing...\n");
        rrc_process_uplink_frame(received_frame);
    } else if (should_relay_packet(received_frame)) {
        printf("RRC-TDMA: Relaying frame to dest %u\n", received_frame->dest_add);
        enqueue_relay_packet(received_frame);
    }
    
    rrc_update_connection_activity(received_frame->source_add);
}

void rrc_cleanup_receive_resources(uint8_t source_node) {
    printf("RRC-TDMA: Cleaning up receive resources for source %u\n", source_node);
    
    NeighborState *neighbor = rrc_get_neighbor_state(source_node);
    if (neighbor) {
        neighbor->active = false;
    }
}

// ============================================================================
// UPLINK PROCESSING
// ============================================================================

int rrc_process_uplink_frame(struct frame *received_frame) {
    if (!received_frame) return -1;
    
    printf("RRC-UPLINK: Processing frame - type:%d, src:%u, dest:%u\n",
           received_frame->data_type, received_frame->source_add, 
           received_frame->dest_add);
    
    if (received_frame->rx_or_l3) {
        // OLSR/L3 packet
        return forward_olsr_packet_to_l3(received_frame);
    } else {
        // Application data
        return deliver_data_packet_to_l7(received_frame);
    }
}

int forward_olsr_packet_to_l3(struct frame *l3_frame) {
    if (!l3_frame) return -1;
    
    printf("RRC-UPLINK: Forwarding OLSR packet to L3 (src:%u)\n", l3_frame->source_add);
    
    // Build IPC message for OLSR
    IPC_Message olsr_msg;
    olsr_msg.type = MSG_OLSR_MESSAGE;
    
    // Would parse actual OLSR message here
    // For now, just notify OLSR of received packet
    
    if (rrc_send_to_olsr(&olsr_msg) == 0) {
        printf("RRC-UPLINK: OLSR packet forwarded successfully\n");
        return 0;
    }
    
    return -1;
}

int deliver_data_packet_to_l7(struct frame *app_frame) {
    if (!app_frame) return -1;
    
    printf("RRC-UPLINK: Delivering data to L7 (src:%u, size:%u)\n",
           app_frame->source_add, app_frame->payload_length_bytes);
    
    CustomApplicationPacket *app_pkt = convert_frame_to_app_packet(app_frame);
    if (!app_pkt) {
        printf("RRC-UPLINK: Failed to convert frame to app packet\n");
        return -1;
    }
    
    int result = rrc_deliver_to_application_layer(app_pkt);
    release_app_packet(app_pkt);
    
    return result;
}

int rrc_deliver_to_application_layer(const CustomApplicationPacket *packet) {
    if (!packet) return -1;
    
    if (rrc_send_to_app(packet) == 0) {
        printf("RRC-UPLINK: Packet delivered to application (src:%u)\n", packet->src_id);
        notify_successful_delivery(packet->src_id, packet->sequence_number);
        return 0;
    }
    
    printf("RRC-UPLINK: Failed to deliver packet to application\n");
    notify_application_of_failure(packet->src_id, "Queue full");
    return -1;
}

CustomApplicationPacket *convert_frame_to_app_packet(const struct frame *frame) {
    if (!frame) return NULL;
    
    CustomApplicationPacket *app_pkt = get_free_app_packet();
    if (!app_pkt) return NULL;
    
    app_pkt->src_id = frame->source_add;
    app_pkt->dest_id = frame->dest_add;
    app_pkt->data_type = (RRC_DataType)frame->data_type;
    app_pkt->transmission_type = TRANSMISSION_UNICAST;
    app_pkt->data_size = frame->payload_length_bytes;
    memcpy(app_pkt->data, frame->payload, frame->payload_length_bytes);
    app_pkt->timestamp = (uint32_t)time(NULL);
    app_pkt->sequence_number = 0; // Frame doesn't have sequence number
    app_pkt->urgent = (frame->priority == 1);
    
    return app_pkt;
}

void generate_slot_assignment_failure_message(uint8_t node_id) {
    printf("RRC-UPLINK: Slot assignment failed for node %u\n", node_id);
    
    CustomApplicationPacket failure_pkt;
    memset(&failure_pkt, 0, sizeof(failure_pkt));
    
    failure_pkt.src_id = rrc_node_id;
    failure_pkt.dest_id = node_id;
    failure_pkt.data_type = RRC_DATA_TYPE_CONTROL;
    failure_pkt.transmission_type = TRANSMISSION_UNICAST;
    snprintf((char*)failure_pkt.data, sizeof(failure_pkt.data), 
             "Slot assignment failed");
    failure_pkt.data_size = strlen((char*)failure_pkt.data);
    failure_pkt.timestamp = (uint32_t)time(NULL);
    
    rrc_send_to_app(&failure_pkt);
}

// ============================================================================
// APPLICATION FEEDBACK
// ============================================================================

void notify_application_of_failure(uint8_t dest_node, const char *reason) {
    printf("RRC-APP: Notifying failure - dest:%u, reason:%s\n", dest_node, reason);
    
    CustomApplicationPacket failure_pkt;
    memset(&failure_pkt, 0, sizeof(failure_pkt));
    
    failure_pkt.src_id = rrc_node_id;
    failure_pkt.dest_id = dest_node;
    failure_pkt.data_type = RRC_DATA_TYPE_CONTROL;
    failure_pkt.transmission_type = TRANSMISSION_UNICAST;
    snprintf((char*)failure_pkt.data, sizeof(failure_pkt.data), 
             "DELIVERY_FAILED: %s", reason ? reason : "Unknown");
    failure_pkt.data_size = strlen((char*)failure_pkt.data);
    failure_pkt.timestamp = (uint32_t)time(NULL);
    
    rrc_send_to_app(&failure_pkt);
}

void notify_successful_delivery(uint8_t dest_node, uint32_t sequence_number) {
    printf("RRC-APP: Delivery success - dest:%u, seq:%u\n", dest_node, sequence_number);
    
    rrc_stats.messages_enqueued_total++;
}

// ============================================================================
// PIGGYBACK SUPPORT EXTENSIONS
// ============================================================================

void rrc_initialize_piggyback_system(void) {
    piggyback_active = false;
    piggyback_last_update = 0;
    
    rrc_init_piggyback_tlv();
    
    printf("RRC-PIGGYBACK: System initialized\n");
}

void rrc_initialize_piggyback(uint8_t node_id, uint8_t session_id, 
                              uint8_t traffic_type, uint8_t reserved_slot) {
    rrc_neighbors.current_piggyback_tlv.sourceNodeID = node_id;
    rrc_neighbors.current_piggyback_tlv.myNCSlot = reserved_slot;
    rrc_neighbors.current_piggyback_tlv.ttl = 10;
    rrc_neighbors.current_piggyback_tlv.timeSync = (uint32_t)time(NULL);
    
    piggyback_active = true;
    piggyback_last_update = (uint32_t)time(NULL);
    
    printf("RRC-PIGGYBACK: Initialized - node:%u, session:%u, slot:%u\n",
           node_id, session_id, reserved_slot);
}

void rrc_clear_piggyback(void) {
    piggyback_active = false;
    rrc_neighbors.current_piggyback_tlv.ttl = 0;
    
    printf("RRC-PIGGYBACK: Cleared\n");
}

bool rrc_should_attach_piggyback(void) {
    if (!piggyback_active) return false;
    if (rrc_neighbors.current_piggyback_tlv.ttl == 0) return false;
    
    uint32_t age = (uint32_t)time(NULL) - piggyback_last_update;
    return (age < 5); // Active if updated within 5 seconds
}

PiggybackTLV *rrc_get_piggyback_data(void) {
    if (!rrc_should_attach_piggyback()) return NULL;
    
    return &rrc_neighbors.current_piggyback_tlv;
}

void rrc_check_start_end_packets(const CustomApplicationPacket *packet) {
    if (!packet) return;
    
    // Check if this is START packet (initialize piggyback)
    if (packet->data_size > 0 && strncmp((char*)packet->data, "START", 5) == 0) {
        uint8_t nc_slot = rrc_get_my_nc_slot();
        rrc_initialize_piggyback(packet->src_id, packet->sequence_number, 
                                packet->data_type, nc_slot);
        printf("RRC-PIGGYBACK: START detected, piggyback activated\n");
    }
    
    // Check if this is END packet (clear piggyback)
    if (packet->data_size > 0 && strncmp((char*)packet->data, "END", 3) == 0) {
        rrc_clear_piggyback();
        printf("RRC-PIGGYBACK: END detected, piggyback cleared\n");
    }
}

// ============================================================================
// SLOT SCHEDULING SUPPORT
// ============================================================================

void rrc_update_nc_schedule(void) {
    // Update NC schedule based on current supercycle
    uint32_t current_time = (uint32_t)time(NULL);
    
    for (int i = 0; i < rrc_neighbors.neighbor_count; i++) {
        if (rrc_neighbors.neighbor_table[i].active) {
            uint64_t age = current_time - rrc_neighbors.neighbor_table[i].lastHeardTime;
            
            // Remove inactive neighbors
            if (age > NEIGHBOR_TIMEOUT_SUPERCYCLES * 20) {
                printf("RRC-SCHEDULE: Removing inactive neighbor %u\n",
                       rrc_neighbors.neighbor_table[i].nodeID);
                rrc_neighbors.neighbor_table[i].active = false;
                
                uint8_t old_slot = rrc_neighbors.neighbor_table[i].assignedNCSlot;
                if (old_slot > 0) {
                    rrc_update_nc_status_bitmap(old_slot, false);
                }
            }
        }
    }
}

uint8_t rrc_get_current_nc_slot(void) {
    // Return current NC slot based on frame timing
    // Simplified: return our assigned slot
    return rrc_get_my_nc_slot();
}

// ============================================================================
// FSM FUNCTIONS (IPC-Modified)
// ============================================================================

void init_rrc_fsm(void) {
    if (rrc_state.fsm_initialized) return;
    
    rrc_state.current_rrc_state = RRC_STATE_NULL;
    
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        rrc_state.connection_pool[i].active = false;
        rrc_state.connection_pool[i].dest_node_id = 0;
        rrc_state.connection_pool[i].next_hop_id = 0;
        rrc_state.connection_pool[i].connection_state = RRC_STATE_NULL;
    }
    
    rrc_state.fsm_initialized = true;
    
    printf("RRC: FSM initialized (state: NULL)\n");
}

const char *rrc_state_to_string(RRC_SystemState state) {
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

void rrc_transition_to_state(RRC_SystemState new_state, uint8_t dest_node) {
    RRC_SystemState old_state = rrc_state.current_rrc_state;
    rrc_state.current_rrc_state = new_state;
    rrc_fsm_stats.state_transitions++;
    
    printf("RRC: State transition [%s] → [%s] for node %u\n",
           rrc_state_to_string(old_state), rrc_state_to_string(new_state), dest_node);
}

RRC_ConnectionContext *rrc_get_connection_context(uint8_t dest_node) {
    return rrc_get_connection_ctx(dest_node);
}

RRC_ConnectionContext *rrc_create_connection_context(uint8_t dest_node) {
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (!rrc_state.connection_pool[i].active) {
            rrc_state.connection_pool[i].active = true;
            rrc_state.connection_pool[i].dest_node_id = dest_node;
            rrc_state.connection_pool[i].next_hop_id = 0;
            rrc_state.connection_pool[i].connection_state = RRC_STATE_NULL;
            rrc_state.connection_pool[i].last_activity_time = (uint32_t)time(NULL);
            rrc_state.connection_pool[i].setup_pending = false;
            rrc_state.connection_pool[i].reconfig_pending = false;
            
            printf("RRC: Created connection context for node %u (slot %d)\n", dest_node, i);
            return &rrc_state.connection_pool[i];
        }
    }
    
    printf("RRC: Connection pool full, cannot create context for node %u\n", dest_node);
    return NULL;
}

void rrc_release_connection_context(uint8_t dest_node) {
    RRC_ConnectionContext *ctx = rrc_get_connection_context(dest_node);
    if (ctx) {
        ctx->active = false;
        ctx->dest_node_id = 0;
        printf("RRC: Released connection context for node %u\n", dest_node);
    }
}

void rrc_update_connection_activity(uint8_t dest_node) {
    RRC_ConnectionContext *ctx = rrc_get_connection_context(dest_node);
    if (ctx) {
        ctx->last_activity_time = (uint32_t)time(NULL);
    }
}

int rrc_handle_power_on(void) {
    if (rrc_state.current_rrc_state != RRC_STATE_NULL) {
        printf("RRC: ERROR - Power on from invalid state %s\n", 
               rrc_state_to_string(rrc_state.current_rrc_state));
        return -1;
    }
    
    rrc_transition_to_state(RRC_STATE_IDLE, 0);
    rrc_fsm_stats.power_on_events++;
    
    printf("RRC: System powered on, node registered\n");
    return 0;
}

int rrc_handle_power_off(void) {
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (rrc_state.connection_pool[i].active) {
            rrc_release_connection_context(rrc_state.connection_pool[i].dest_node_id);
        }
    }
    
    rrc_transition_to_state(RRC_STATE_NULL, 0);
    rrc_fsm_stats.power_off_events++;
    
    printf("RRC: System powered off\n");
    return 0;
}

int rrc_handle_data_request(uint8_t dest_node, MessagePriority qos) {
    if (rrc_state.current_rrc_state != RRC_STATE_IDLE) {
        printf("RRC: WARNING - Data request in non-IDLE state\n");
    }
    
    RRC_ConnectionContext *ctx = rrc_create_connection_context(dest_node);
    if (!ctx) return -1;
    
    ctx->qos_priority = qos;
    ctx->setup_pending = true;
    
    rrc_transition_to_state(RRC_STATE_CONNECTION_SETUP, dest_node);
    
    uint8_t next_hop = ipc_olsr_get_next_hop(dest_node);
    if (next_hop == 0 || next_hop == 0xFF) {
        printf("RRC: ERROR - No route to destination %u\n", dest_node);
        rrc_fsm_stats.setup_failures++;
        return -1;
    }
    
    ctx->next_hop_id = next_hop;
    
    printf("RRC: Connection setup initiated for node %u via %u\n", dest_node, next_hop);
    return 0;
}

int rrc_handle_route_and_slots_allocated(uint8_t dest_node, uint8_t next_hop) {
    RRC_ConnectionContext *ctx = rrc_get_connection_context(dest_node);
    if (!ctx) return -1;
    
    ctx->next_hop_id = next_hop;
    ctx->setup_pending = false;
    
    rrc_transition_to_state(RRC_STATE_CONNECTED, dest_node);
    rrc_fsm_stats.setup_success++;
    
    printf("RRC: Connection established to node %u via %u\n", dest_node, next_hop);
    return 0;
}

int rrc_handle_route_change(uint8_t dest_node, uint8_t new_next_hop) {
    RRC_ConnectionContext *ctx = rrc_get_connection_context(dest_node);
    if (!ctx) return -1;
    
    ctx->reconfig_pending = true;
    rrc_transition_to_state(RRC_STATE_RECONFIGURATION, dest_node);
    
    printf("RRC: Route change detected for node %u, reconfiguring to next hop %u\n",
           dest_node, new_next_hop);
    return 0;
}

int rrc_handle_reconfig_success(uint8_t dest_node, uint8_t new_next_hop) {
    RRC_ConnectionContext *ctx = rrc_get_connection_context(dest_node);
    if (!ctx) return -1;
    
    ctx->next_hop_id = new_next_hop;
    ctx->reconfig_pending = false;
    
    rrc_transition_to_state(RRC_STATE_CONNECTED, dest_node);
    rrc_fsm_stats.reconfigurations++;
    
    printf("RRC: Reconfiguration complete for node %u\n", dest_node);
    return 0;
}

int rrc_handle_inactivity_timeout(uint8_t dest_node) {
    RRC_ConnectionContext *ctx = rrc_get_connection_context(dest_node);
    if (!ctx) return 0;
    
    rrc_transition_to_state(RRC_STATE_RELEASE, dest_node);
    rrc_fsm_stats.inactivity_timeouts++;
    
    rrc_release_connection_context(dest_node);
    rrc_transition_to_state(RRC_STATE_IDLE, dest_node);
    
    printf("RRC: Connection released due to inactivity timeout (node %u)\n", dest_node);
    return 0;
}

int rrc_handle_release_complete(uint8_t dest_node) {
    rrc_release_connection_context(dest_node);
    rrc_transition_to_state(RRC_STATE_IDLE, dest_node);
    rrc_fsm_stats.releases++;
    
    printf("RRC: Connection released (node %u)\n", dest_node);
    return 0;
}

void rrc_periodic_system_management(void) {
    uint32_t current_time = (uint32_t)time(NULL);
    
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (rrc_state.connection_pool[i].active) {
            uint32_t idle_time = current_time - rrc_state.connection_pool[i].last_activity_time;
            
            if (idle_time > RRC_INACTIVITY_TIMEOUT_SEC) {
                printf("RRC: Inactivity timeout for node %u\n", 
                       rrc_state.connection_pool[i].dest_node_id);
                rrc_handle_inactivity_timeout(rrc_state.connection_pool[i].dest_node_id);
            }
        }
    }
}

void print_rrc_fsm_stats(void) {
    printf("\n=== RRC FSM Statistics ===\n");
    printf("Current state: %s\n", rrc_state_to_string(rrc_state.current_rrc_state));
    printf("State transitions: %u\n", rrc_fsm_stats.state_transitions);
    printf("Setup success: %u\n", rrc_fsm_stats.setup_success);
    printf("Setup failures: %u\n", rrc_fsm_stats.setup_failures);
    printf("Reconfigurations: %u\n", rrc_fsm_stats.reconfigurations);
    printf("Inactivity timeouts: %u\n", rrc_fsm_stats.inactivity_timeouts);
    printf("Releases: %u\n", rrc_fsm_stats.releases);
    printf("Power events: %u on, %u off\n", rrc_fsm_stats.power_on_events, rrc_fsm_stats.power_off_events);
    
    printf("\nActive connections:\n");
    int active_count = 0;
    for (int i = 0; i < RRC_CONNECTION_POOL_SIZE; i++) {
        if (rrc_state.connection_pool[i].active) {
            printf("  Slot %d: Node %u → %u (state: %s)\n", i,
                   rrc_state.connection_pool[i].dest_node_id, 
                   rrc_state.connection_pool[i].next_hop_id,
                   rrc_state_to_string(rrc_state.connection_pool[i].connection_state));
            active_count++;
        }
    }
    if (active_count == 0) {
        printf("  No active connections\n");
    }
    printf("==========================\n\n");
}

// ============================================================================
// MESSAGE HANDLER THREADS
// ============================================================================

// OLSR Message Handler Thread
void *rrc_olsr_message_handler(void *arg) {
    printf("RRC: OLSR message handler thread started\n");
    
    while (system_running) {
        IPC_Message msg;
        
        // Non-blocking receive with short timeout
        if (rrc_receive_from_olsr(&msg, false) > 0) {
            switch (msg.type) {
                case MSG_OLSR_ROUTE_UPDATE:
                    printf("RRC-OLSR: Received route update for node %u → next hop %u\n",
                           msg.route_response.dest_node, msg.route_response.next_hop);
                    
                    // Check if we need to reconfigure any existing connections
                    RRC_ConnectionContext *ctx = rrc_get_connection_context(msg.route_response.dest_node);
                    if (ctx && ctx->next_hop_id != msg.route_response.next_hop) {
                        printf("RRC-OLSR: Route change detected, triggering reconfiguration\n");
                        rrc_handle_route_change(msg.route_response.dest_node, msg.route_response.next_hop);
                    }
                    break;
                    
                case MSG_OLSR_MESSAGE:
                    printf("RRC-OLSR: Received OLSR protocol message from originator %u\n",
                           msg.olsr_msg.originator);
                    
                    // Build NC slot message with OLSR content
                    NCSlotMessage nc_msg;
                    build_nc_slot_message(&nc_msg, rrc_get_my_nc_slot());
                    
                    OLSRMessage olsr_wrapped;
                    olsr_wrapped.msg_type = msg.olsr_msg.msg_type;
                    olsr_wrapped.originator_addr = msg.olsr_msg.originator;
                    olsr_wrapped.ttl = msg.olsr_msg.ttl;
                    olsr_wrapped.hop_count = msg.olsr_msg.hop_count;
                    
                    add_olsr_to_nc_message(&nc_msg, &olsr_wrapped);
                    
                    // Add piggyback if available
                    PiggybackTLV piggyback;
                    rrc_build_piggyback_tlv(&piggyback);
                    add_piggyback_to_nc_message(&nc_msg, &piggyback);
                    
                    // Enqueue to NC slot queue for TDMA transmission
                    if (nc_slot_queue_enqueue(&nc_msg)) {
                        printf("RRC-OLSR: OLSR message queued for NC slot transmission\n");
                    }
                    break;
                    
                default:
                    printf("RRC-OLSR: Unknown message type %d\n", msg.type);
                    break;
            }
        }
        
        usleep(1000); // 1ms sleep to prevent busy-waiting
    }
    
    printf("RRC: OLSR message handler thread stopped\n");
    return NULL;
}

// TDMA Message Handler Thread
void *rrc_tdma_message_handler(void *arg) {
    printf("RRC: TDMA message handler thread started\n");
    
    while (system_running) {
        IPC_Message msg;
        
        if (rrc_receive_from_tdma(&msg, false) > 0) {
            switch (msg.type) {
                case MSG_TDMA_SLOT_STATUS_UPDATE:
                    printf("RRC-TDMA: Slot status update received\n");
                    // Update our local view of slot availability
                    break;
                    
                case MSG_TDMA_RX_QUEUE_DATA:
                    printf("RRC-TDMA: RX queue notification - %u frames from node %u\n",
                           msg.rx_notify.frame_count, msg.rx_notify.source_node);
                    
                    // Process received frames from rx_queue
                    while (!rrc_is_queue_empty(&shared_queues->rx_queue)) {
                        struct frame rx_frame = rrc_dequeue_shared(&shared_queues->rx_queue);
                        
                        printf("RRC-TDMA: Processing uplink frame from node %u to node %u\n",
                               rx_frame.source_add, rx_frame.dest_add);
                        
                        // Check if packet is for us
                        if (rx_frame.dest_add == rrc_node_id) {
                            printf("RRC-TDMA: Frame is for us, delivering to application\n");
                            
                            // Convert to app packet and deliver
                            CustomApplicationPacket app_pkt;
                            app_pkt.src_id = rx_frame.source_add;
                            app_pkt.dest_id = rx_frame.dest_add;
                            app_pkt.data_size = rx_frame.payload_length_bytes;
                            memcpy(app_pkt.data, rx_frame.payload, rx_frame.payload_length_bytes);
                            app_pkt.timestamp = (uint32_t)time(NULL);
                            
                            if (rrc_send_to_app(&app_pkt) == 0) {
                                printf("RRC-TDMA: Uplink packet delivered to application\n");
                            }
                        } else {
                            // Check if we should relay
                            if (should_relay_packet(&rx_frame)) {
                                printf("RRC-TDMA: Relaying packet to destination %u\n", rx_frame.dest_add);
                                enqueue_relay_packet(&rx_frame);
                            }
                        }
                        
                        // Update activity for source node
                        rrc_update_connection_activity(rx_frame.source_add);
                    }
                    break;
                    
                default:
                    printf("RRC-TDMA: Unknown message type %d\n", msg.type);
                    break;
            }
        }
        
        usleep(1000);
    }
    
    printf("RRC: TDMA message handler thread stopped\n");
    return NULL;
}

// Application Message Handler Thread
void *rrc_app_message_handler(void *arg) {
    printf("RRC: Application message handler thread started\n");
    
    while (system_running) {
        CustomApplicationPacket app_pkt;
        
        // Check for downlink packets from application
        if (rrc_receive_from_app(&app_pkt, false) == 0) {
            printf("RRC-APP: Received packet from app (src:%u → dest:%u, size:%zu, type:%d)\n",
                   app_pkt.src_id, app_pkt.dest_id, app_pkt.data_size, app_pkt.data_type);
            
            rrc_stats.packets_processed++;
            
            // Initiate connection setup if needed
            if (rrc_state.current_rrc_state == RRC_STATE_IDLE) {
                printf("RRC-APP: Initiating connection setup for destination %u\n", app_pkt.dest_id);
                
                MessagePriority priority = PRIORITY_DATA_1;
                if (app_pkt.data_type == RRC_DATA_TYPE_VOICE) priority = PRIORITY_DIGITAL_VOICE;
                else if (app_pkt.data_type == RRC_DATA_TYPE_PTT) priority = PRIORITY_ANALOG_VOICE_PTT;
                
                rrc_handle_data_request(app_pkt.dest_id, priority);
            }
            
            // Get route from OLSR
            uint8_t next_hop = ipc_olsr_get_next_hop(app_pkt.dest_id);
            
            if (next_hop == 0 || next_hop == 0xFF) {
                printf("RRC-APP: ERROR - No route to destination %u\n", app_pkt.dest_id);
                
                // Notify app of failure
                CustomApplicationPacket failure_pkt = app_pkt;
                failure_pkt.src_id = rrc_node_id;
                failure_pkt.dest_id = app_pkt.src_id;
                rrc_send_to_app(&failure_pkt);
                continue;
            }
            
            printf("RRC-APP: Route found - next hop is %u\n", next_hop);
            
            // Check if connection is established
            RRC_ConnectionContext *ctx = rrc_get_connection_context(app_pkt.dest_id);
            if (ctx && ctx->connection_state == RRC_STATE_CONNECTED) {
                printf("RRC-APP: Connection established, proceeding with transmission\n");
                
                // Build frame for downlink transmission
                struct frame tx_frame;
                tx_frame.source_add = app_pkt.src_id;
                tx_frame.dest_add = app_pkt.dest_id;
                tx_frame.next_hop_add = next_hop;
                tx_frame.rx_or_l3 = false; // L3 (downlink)
                tx_frame.TTL = 10;
                tx_frame.priority = (app_pkt.urgent ? 1 : 2);
                tx_frame.data_type = (DATATYPE)app_pkt.data_type;
                tx_frame.payload_length_bytes = app_pkt.data_size;
                memcpy(tx_frame.payload, app_pkt.data, app_pkt.data_size);
                
                // Enqueue to appropriate downlink queue based on priority
                if (tx_frame.data_type == DATA_TYPE_ANALOG_VOICE) {
                    rrc_enqueue_shared(&shared_queues->analog_voice_queue, tx_frame);
                    printf("RRC-APP: Frame enqueued to analog voice queue\n");
                } else {
                    int queue_idx = (tx_frame.priority >= 0 && tx_frame.priority < NUM_PRIORITY) ? 
                                    tx_frame.priority : 0;
                    rrc_enqueue_shared(&shared_queues->data_from_l3_queue[queue_idx], tx_frame);
                    printf("RRC-APP: Frame enqueued to data queue %d\n", queue_idx);
                }
                
                rrc_stats.messages_enqueued_total++;
                
                // Update connection activity
                rrc_update_connection_activity(app_pkt.dest_id);
                
                // Complete setup if pending
                if (ctx->setup_pending) {
                    rrc_handle_route_and_slots_allocated(app_pkt.dest_id, next_hop);
                }
            } else {
                printf("RRC-APP: Connection not ready, waiting for setup completion\n");
            }
        }
        
        usleep(1000);
    }
    
    printf("RRC: Application message handler thread stopped\n");
    return NULL;
}

// PHY Message Handler Thread
void *rrc_phy_message_handler(void *arg) {
    printf("RRC: PHY message handler thread started\n");
    
    while (system_running) {
        IPC_Message msg;
        
        if (rrc_receive_from_phy(&msg, false) > 0) {
            switch (msg.type) {
                case MSG_PHY_METRICS_UPDATE:
                    printf("RRC-PHY: Metrics update for node %u (RSSI:%.1f, SNR:%.1f, PER:%.1f%%)\n",
                           msg.phy_metrics.node_id, msg.phy_metrics.rssi_dbm,
                           msg.phy_metrics.snr_db, msg.phy_metrics.per_percent);
                    
                    // Update neighbor metrics
                    NeighborState *neighbor = rrc_create_neighbor_state(msg.phy_metrics.node_id);
                    if (neighbor) {
                        neighbor->phy.rssi_dbm = msg.phy_metrics.rssi_dbm;
                        neighbor->phy.snr_db = msg.phy_metrics.snr_db;
                        neighbor->phy.per_percent = msg.phy_metrics.per_percent;
                        neighbor->phy.packet_count = msg.phy_metrics.packet_count;
                        neighbor->phy.last_update_time = msg.phy_metrics.timestamp;
                        neighbor->active = msg.phy_metrics.link_active;
                        
                        // Check for poor link quality
                        if (!is_link_quality_good(msg.phy_metrics.node_id)) {
                            printf("RRC-PHY: WARNING - Poor link quality for node %u\n", 
                                   msg.phy_metrics.node_id);
                        }
                    }
                    break;
                    
                case MSG_PHY_LINK_STATUS_CHANGE:
                    printf("RRC-PHY: Link status change for node %u\n", msg.phy_metrics.node_id);
                    break;
                    
                default:
                    printf("RRC-PHY: Unknown message type %d\n", msg.type);
                    break;
            }
        }
        
        usleep(1000);
    }
    
    printf("RRC: PHY message handler thread stopped\n");
    return NULL;
}

// Periodic Management Thread
void *rrc_periodic_management_thread(void *arg) {
    printf("RRC: Periodic management thread started\n");
    
    int cycle_count = 0;
    
    while (system_running) {
        sleep(1); // Run every second
        cycle_count++;
        
        // Periodic system management (timeouts, cleanup)
        rrc_periodic_system_management();
        
        // Update piggyback TTL
        rrc_update_piggyback_ttl();
        
        // Every 10 seconds, send slot table update to TDMA
        if (cycle_count % 10 == 0) {
            IPC_Message slot_update;
            slot_update.type = MSG_RRC_SLOT_TABLE_UPDATE;
            
            for (int i = 0; i < 8; i++) {
                slot_update.slot_table.slot_table[i] = tdma_slot_table[i];
            }
            slot_update.slot_table.timestamp = (uint32_t)time(NULL);
            slot_update.slot_table.updated_slot_count = 8;
            
            if (rrc_send_to_tdma(&slot_update) == 0) {
                printf("RRC-MGMT: Sent slot table update to TDMA\n");
            }
        }
        
        // Every 30 seconds, print statistics
        if (cycle_count % 30 == 0) {
            printf("\n");
            print_rrc_fsm_stats();
            print_nc_slot_queue_stats();
            print_relay_stats();
            print_app_rrc_queue_stats();
            
            printf("=== RRC Statistics ===\n");
            printf("Packets processed: %u\n", rrc_stats.packets_processed);
            printf("Messages enqueued: %u\n", rrc_stats.messages_enqueued_total);
            printf("Messages discarded: %u\n", rrc_stats.messages_discarded_no_slots);
            printf("Route queries: %u\n", rrc_stats.route_queries);
            printf("Poor links detected: %u\n", rrc_stats.poor_links_detected);
            printf("=====================\n\n");
        }
    }
    
    printf("RRC: Periodic management thread stopped\n");
    return NULL;
}

// ============================================================================
// THREAD CONTROL
// ============================================================================

void rrc_start_threads(void) {
    printf("RRC: Starting message handler threads...\n");
    
    if (pthread_create(&olsr_handler_thread, NULL, rrc_olsr_message_handler, NULL) != 0) {
        perror("pthread_create(olsr_handler)");
    }
    
    if (pthread_create(&tdma_handler_thread, NULL, rrc_tdma_message_handler, NULL) != 0) {
        perror("pthread_create(tdma_handler)");
    }
    
    if (pthread_create(&app_handler_thread, NULL, rrc_app_message_handler, NULL) != 0) {
        perror("pthread_create(app_handler)");
    }
    
    if (pthread_create(&phy_handler_thread, NULL, rrc_phy_message_handler, NULL) != 0) {
        perror("pthread_create(phy_handler)");
    }
    
    if (pthread_create(&periodic_mgmt_thread, NULL, rrc_periodic_management_thread, NULL) != 0) {
        perror("pthread_create(periodic_mgmt)");
    }
    
    printf("RRC: All threads started successfully\n");
}

void rrc_stop_threads(void) {
    printf("RRC: Stopping message handler threads...\n");
    
    system_running = false;
    
    pthread_join(olsr_handler_thread, NULL);
    pthread_join(tdma_handler_thread, NULL);
    pthread_join(app_handler_thread, NULL);
    pthread_join(phy_handler_thread, NULL);
    pthread_join(periodic_mgmt_thread, NULL);
    
    printf("RRC: All threads stopped\n");
}

void rrc_signal_handler(int signum) {
    printf("\nRRC: Received signal %d, shutting down...\n", signum);
    system_running = false;
}

// ============================================================================
// LOOPBACK TESTING FUNCTIONS
// ============================================================================

void rrc_simulate_app_downlink(void) {
    printf("\n>>> Loopback: Simulating application downlink packet\n");
    
    CustomApplicationPacket app_pkt;
    app_pkt.src_id = rrc_node_id;
    app_pkt.dest_id = 5; // Destination node 5
    app_pkt.data_type = RRC_DATA_TYPE_SMS;
    app_pkt.transmission_type = TRANSMISSION_UNICAST;
    app_pkt.data_size = 100;
    snprintf((char*)app_pkt.data, sizeof(app_pkt.data), "Test message from node %u", rrc_node_id);
    app_pkt.sequence_number = rrc_stats.packets_processed + 1;
    app_pkt.timestamp = (uint32_t)time(NULL);
    app_pkt.urgent = false;
    
    if (app_rrc_shm) {
        sem_wait(&app_rrc_shm->mutex);
        
        if (app_rrc_shm->app_to_rrc_count < APP_RRC_QUEUE_SIZE) {
            app_rrc_shm->app_to_rrc_queue[app_rrc_shm->app_to_rrc_back] = app_pkt;
            app_rrc_shm->app_to_rrc_back = (app_rrc_shm->app_to_rrc_back + 1) % APP_RRC_QUEUE_SIZE;
            app_rrc_shm->app_to_rrc_count++;
            printf(">>> Loopback: Application packet injected to app_to_rrc queue\n");
        }
        
        sem_post(&app_rrc_shm->mutex);
    }
}

void rrc_simulate_tdma_uplink(void) {
    printf("\n>>> Loopback: Simulating TDMA uplink reception\n");
    
    // Simulate received frame in rx_queue
    struct frame rx_frame;
    rx_frame.source_add = 5;
    rx_frame.dest_add = rrc_node_id;
    rx_frame.next_hop_add = rrc_node_id;
    rx_frame.rx_or_l3 = true; // RX (uplink)
    rx_frame.TTL = 8;
    rx_frame.priority = 2;
    rx_frame.data_type = DATA_TYPE_SMS;
    rx_frame.payload_length_bytes = 50;
    snprintf((char*)rx_frame.payload, sizeof(rx_frame.payload), "Uplink test from node 5");
    
    rrc_enqueue_shared(&shared_queues->rx_queue, rx_frame);
    
    // Send notification to RRC
    IPC_Message notify;
    notify.type = MSG_TDMA_RX_QUEUE_DATA;
    notify.rx_notify.frame_count = 1;
    notify.rx_notify.source_node = 5;
    notify.rx_notify.dest_node = rrc_node_id;
    notify.rx_notify.is_for_self = true;
    notify.rx_notify.timestamp = (uint32_t)time(NULL);
    
    rrc_send_to_tdma(&notify); // This will be received by TDMA handler
    printf(">>> Loopback: Uplink frame injected to rx_queue with notification\n");
}

void rrc_simulate_olsr_route_update(void) {
    printf("\n>>> Loopback: Simulating OLSR route update\n");
    
    IPC_Message route_msg;
    route_msg.type = MSG_OLSR_ROUTE_UPDATE;
    route_msg.route_response.dest_node = 5;
    route_msg.route_response.next_hop = 3;
    route_msg.route_response.hop_count = 2;
    route_msg.route_response.route_available = true;
    route_msg.route_response.request_id = (uint32_t)time(NULL);
    
    if (mq_olsr_to_rrc != -1) {
        mq_send(mq_olsr_to_rrc, (const char*)&route_msg, sizeof(IPC_Message), 0);
        printf(">>> Loopback: Route update injected (node 5 via node 3)\n");
    }
}

void rrc_simulate_phy_metrics(void) {
    printf("\n>>> Loopback: Simulating PHY metrics update\n");
    
    IPC_Message phy_msg;
    phy_msg.type = MSG_PHY_METRICS_UPDATE;
    phy_msg.phy_metrics.node_id = 3;
    phy_msg.phy_metrics.rssi_dbm = -65.5f;
    phy_msg.phy_metrics.snr_db = 25.0f;
    phy_msg.phy_metrics.per_percent = 1.5f;
    phy_msg.phy_metrics.link_active = true;
    phy_msg.phy_metrics.packet_count = 100;
    phy_msg.phy_metrics.timestamp = (uint32_t)time(NULL);
    
    if (mq_phy_to_rrc != -1) {
        mq_send(mq_phy_to_rrc, (const char*)&phy_msg, sizeof(IPC_Message), 0);
        printf(">>> Loopback: PHY metrics injected for node 3\n");
    }
}

void rrc_loopback_test(void) {
    printf("\n");
    printf("===========================================\n");
    printf("RRC LOOPBACK TEST MODE\n");
    printf("===========================================\n");
    printf("Testing complete RRC integration with simulated events\n\n");
    
    sleep(2);
    
    // Test 1: Simulate PHY metrics
    rrc_simulate_phy_metrics();
    sleep(1);
    
    // Test 2: Simulate OLSR route update
    rrc_simulate_olsr_route_update();
    sleep(1);
    
    // Test 3: Simulate application downlink
    rrc_simulate_app_downlink();
    sleep(2);
    
    // Test 4: Simulate TDMA uplink
    rrc_simulate_tdma_uplink();
    sleep(2);
    
    // Test 5: Check if uplink was delivered to app
    if (app_rrc_shm) {
        sem_wait(&app_rrc_shm->mutex);
        int count = app_rrc_shm->rrc_to_app_count;
        sem_post(&app_rrc_shm->mutex);
        
        printf("\n>>> Loopback: RRC to App queue has %d messages\n", count);
        
        if (count > 0) {
            CustomApplicationPacket received_pkt;
            if (rrc_receive_from_app(&received_pkt, false) == 0) {
                printf(">>> Loopback: Successfully retrieved uplink packet from rrc_to_app queue\n");
                printf("    Source: %u, Dest: %u, Size: %zu\n", 
                       received_pkt.src_id, received_pkt.dest_id, received_pkt.data_size);
            }
        }
    }
    
    printf("\n===========================================\n");
    printf("LOOPBACK TEST COMPLETED\n");
    printf("===========================================\n\n");
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char *argv[]) {
    printf("\n========================================\n");
    printf("RRC Subsystem with POSIX IPC\n");
    printf("Multi-threaded Event-Driven Architecture\n");
    printf("========================================\n");
    
    // Set our node ID (can be passed as command line argument)
    if (argc > 1) {
        rrc_node_id = (uint8_t)atoi(argv[1]);
    } else {
        rrc_node_id = 1;  // Default node 1
    }
    printf("Node ID: %u\n", rrc_node_id);
    
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, rrc_signal_handler);
    signal(SIGTERM, rrc_signal_handler);
    
    // Initialize IPC infrastructure
    printf("\nInitializing IPC...\n");
    if (rrc_ipc_init() < 0) {
        fprintf(stderr, "FATAL: Failed to initialize IPC\n");
        return -1;
    }
    printf("IPC initialized successfully\n");
    printf("  - Message queues: 6 bidirectional channels\n");
    printf("  - Shared memory: 2 regions (queues + app-rrc)\n");
    printf("  - Semaphores: 2 for synchronization\n");

    // Initialize all RRC subsystems
    printf("\nInitializing RRC subsystems...\n");
    init_message_pool();
    init_app_packet_pool();
    init_rrc_fsm();
    init_nc_slot_manager();
    init_neighbor_state_table();
    init_nc_slot_message_queue();
    init_relay_queue();
    rrc_init_slot_status();
    rrc_init_piggyback_tlv();

    printf("\n========================================\n");
    printf("RRC: All subsystems initialized\n");
    printf("========================================\n");
    printf("  - Node ID: %u\n", rrc_node_id);
    printf("  - My NC Slot: %u\n", rrc_get_my_nc_slot());
    printf("  - FSM State: %s\n", rrc_state_to_string(rrc_state.current_rrc_state));
    printf("  - Max Neighbors: %d\n", MAX_NEIGHBORS);
    printf("  - NC Slot Queue: %d capacity\n", NC_SLOT_QUEUE_SIZE);
    printf("  - IPC ready for all layers\n");

    // Power on the system
    printf("\nSimulating Power ON event...\n");
    rrc_handle_power_on();
    printf("FSM State: %s\n", rrc_state_to_string(rrc_state.current_rrc_state));

    // Start message handler threads
    printf("\n========================================\n");
    printf("Starting Message Handler Threads\n");
    printf("========================================\n");
    rrc_start_threads();
    printf("  ✓ OLSR message handler\n");
    printf("  ✓ TDMA message handler\n");
    printf("  ✓ Application message handler\n");
    printf("  ✓ PHY message handler\n");
    printf("  ✓ Periodic management thread\n");
    
    sleep(1);
    
    // Run loopback test to demonstrate integration
    printf("\n========================================\n");
    printf("Running Integrated Loopback Test\n");
    printf("========================================\n");
    rrc_loopback_test();
    
    printf("\n========================================\n");
    printf("RRC Subsystem Running\n");
    printf("========================================\n");
    printf("Event-driven operation active\n");
    printf("All threads processing messages\n");
    printf("Press Ctrl+C for graceful shutdown\n\n");

    // Main loop - threads handle all events
    while (system_running) {
        sleep(60); // Wake up every minute for housekeeping
        
        // Main thread can do additional coordination here if needed
        // Threads are handling all the real-time message processing
    }

    // Graceful shutdown sequence
    printf("\n========================================\n");
    printf("Shutting Down RRC Subsystem\n");
    printf("========================================\n");
    
    printf("Stopping message handler threads...\n");
    rrc_stop_threads();
    
    printf("Powering off RRC...\n");
    rrc_handle_power_off();
    
    printf("Cleaning up IPC resources...\n");
    rrc_ipc_cleanup();

    printf("\n========================================\n");
    printf("RRC: Shutdown complete\n");
    printf("========================================\n\n");
    
    return 0;
}
