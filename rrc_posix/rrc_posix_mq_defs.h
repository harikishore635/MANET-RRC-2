/**
 * RRC POSIX Message Queue Integration - Message Definitions
 * Linux POSIX message queues + shared memory for layer integration
 */

#ifndef RRC_POSIX_MQ_DEFS_H
#define RRC_POSIX_MQ_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

#define MAX_MQ_MSG_SIZE 2048           // POSIX MQ message size limit
#define FRAME_POOL_SIZE 64             // Number of frame pool entries
#define APP_POOL_SIZE 32               // Number of app packet pool entries
#define PAYLOAD_SIZE_BYTES 2800        // From rrc1011.c
#define REQUEST_TIMEOUT_MS 5000        // Default timeout for requests
#define MAX_NEIGHBORS 40               // Max neighbors
#define MAX_SLOTS 10                   // Max slots per frame

// ============================================================================
// POSIX MESSAGE QUEUE NAMES (must start with '/')
// ============================================================================

#define MQ_APP_TO_RRC       "/rrc_app_to_rrc_mq"
#define MQ_RRC_TO_APP       "/rrc_rrc_to_app_mq"
#define MQ_RRC_TO_OLSR      "/rrc_rrc_to_olsr_mq"
#define MQ_OLSR_TO_RRC      "/rrc_olsr_to_rrc_mq"
#define MQ_RRC_TO_TDMA      "/rrc_rrc_to_tdma_mq"
#define MQ_TDMA_TO_RRC      "/rrc_tdma_to_rrc_mq"
#define MQ_MAC_TO_RRC       "/rrc_mac_to_rrc_mq"

// Per-datatype queues (separate MQs for priority/type separation)
#define MQ_RRC_MSG_QUEUE    "/rrc_msg_queue"
#define MQ_RRC_VOICE_QUEUE  "/rrc_voice_queue"
#define MQ_RRC_VIDEO_QUEUE  "/rrc_video_queue"
#define MQ_RRC_FILE_QUEUE   "/rrc_file_queue"
#define MQ_RRC_RELAY_QUEUE  "/rrc_relay_queue"
#define MQ_RRC_PTT_QUEUE    "/rrc_ptt_queue"
#define MQ_RRC_UNKNOWN_QUEUE "/rrc_unknown_queue"

// ============================================================================
// POSIX SHARED MEMORY NAMES
// ============================================================================

#define SHM_FRAME_POOL      "/rrc_frame_pool_shm"
#define SHM_APP_POOL        "/rrc_app_pool_shm"
#define SHM_MAC_RX_POOL     "/rrc_mac_rx_pool_shm"

// ============================================================================
// MESSAGE TYPE ENUMERATIONS
// ============================================================================

typedef enum {
    // APP <-> RRC
    MSG_APP_TO_RRC_DATA = 1,
    MSG_RRC_TO_APP_FRAME = 2,
    MSG_RRC_TO_APP_ERROR = 3,
    
    // RRC <-> OLSR
    MSG_RRC_TO_OLSR_ROUTE_REQ = 10,
    MSG_OLSR_TO_RRC_ROUTE_RSP = 11,
    MSG_RRC_TO_OLSR_RELAY = 12,
    MSG_RRC_TO_OLSR_NC_HELLO = 13,
    MSG_OLSR_TO_RRC_NEIGHBOR_UPDATE = 14,
    MSG_OLSR_TO_RRC_NO_ROUTE = 15,
    
    // RRC <-> TDMA
    MSG_RRC_TO_TDMA_SLOT_INFO = 20,
    MSG_RRC_TO_TDMA_SLOT_CHECK = 21,
    MSG_TDMA_TO_RRC_SLOT_RSP = 22,
    MSG_RRC_TO_TDMA_NC_REQUEST = 23,
    MSG_TDMA_TO_RRC_NC_RSP = 24,
    
    // MAC -> RRC
    MSG_MAC_TO_RRC_RX_FRAME = 30
} MessageType;

typedef enum {
    DATA_TYPE_MSG = 0,
    DATA_TYPE_VOICE = 1,
    DATA_TYPE_VIDEO = 2,
    DATA_TYPE_FILE = 3,
    DATA_TYPE_RELAY = 4,
    DATA_TYPE_PTT = 5,
    DATA_TYPE_UNKNOWN = 99
} DataType;

typedef enum {
    ERROR_OLSR_NO_ROUTE = 1,
    ERROR_TDMA_SLOT_UNAVAILABLE = 2,
    ERROR_PHY_LINK_POOR = 3,
    ERROR_TIMEOUT = 4,
    ERROR_BUFFER_FULL = 5
} ErrorCode;

// ============================================================================
// SHARED MEMORY POOL STRUCTURES
// ============================================================================

// Frame pool entry in shared memory
typedef struct {
    uint8_t src_id;
    uint8_t dest_id;
    uint8_t next_hop;
    uint8_t ttl;
    uint8_t data_type;
    uint8_t priority;
    uint16_t payload_len;
    uint32_t sequence_number;
    uint32_t timestamp_ms;
    uint8_t payload[PAYLOAD_SIZE_BYTES];
    bool in_use;
    bool valid;
} FramePoolEntry;

// Application packet pool entry in shared memory
typedef struct {
    uint8_t src_id;
    uint8_t dest_id;
    uint8_t data_type;
    uint8_t transmission_type;
    uint8_t priority;
    uint16_t payload_len;
    uint32_t sequence_number;
    uint32_t timestamp_ms;
    uint8_t payload[PAYLOAD_SIZE_BYTES];
    bool in_use;
    bool urgent;
} AppPacketPoolEntry;

// ============================================================================
// MESSAGE STRUCTURES (must fit in MAX_MQ_MSG_SIZE)
// ============================================================================

// Common message header
typedef struct {
    uint32_t request_id;
    uint32_t timestamp_ms;
    MessageType msg_type;
    uint8_t reserved[4];  // alignment
} MessageHeader;

// APP -> RRC: Notify that app packet is in shared memory
typedef struct {
    MessageHeader header;
    uint16_t pool_index;       // Index into SHM_APP_POOL
    uint8_t data_type;
    uint8_t priority;
} AppToRrcMsg;

// RRC -> APP: Deliver frame from MAC or send error
typedef struct {
    MessageHeader header;
    uint16_t pool_index;       // Index into SHM_FRAME_POOL (if type=FRAME)
    uint8_t is_error;          // 0=frame delivery, 1=error
    uint8_t error_code;        // ErrorCode if is_error=1
    char error_text[64];       // Human-readable error
} RrcToAppMsg;

// RRC -> OLSR: Route lookup request
typedef struct {
    MessageHeader header;
    uint8_t dest_node;
    uint8_t src_node;
    uint8_t purpose;           // 0=route_lookup, 1=relay_notify, 2=nc_hello
    uint16_t pool_index;       // Optional payload for hello/piggyback
} RrcToOlsrMsg;

// OLSR -> RRC: Route response
typedef struct {
    MessageHeader header;
    uint8_t dest_node;
    uint8_t next_hop;
    uint8_t hop_count;
    uint8_t status;            // 0=OK, 1=NO_ROUTE
} OlsrToRrcMsg;

// RRC -> TDMA: Slot info table or slot check
typedef struct {
    MessageHeader header;
    uint8_t req_type;          // 0=slot_info, 1=slot_check, 2=nc_request
    uint8_t next_hop;
    int8_t priority;
    uint16_t pool_index;       // For NC payload
    uint8_t slot_bitmap[8];    // 64-bit bitmap for slot table
} RrcToTdmaMsg;

// TDMA -> RRC: Slot response
typedef struct {
    MessageHeader header;
    uint8_t success;
    uint8_t assigned_slot;
    uint16_t slot_bitmap_low;
    uint16_t slot_bitmap_high;
} TdmaToRrcMsg;

// MAC -> RRC: Incoming frame notification
typedef struct {
    MessageHeader header;
    uint16_t pool_index;       // Index into SHM_MAC_RX_POOL or SHM_FRAME_POOL
    float rssi_dbm;
    float snr_db;
} MacToRrcMsg;

// Generic message union (for convenience)
typedef union {
    MessageHeader header;
    AppToRrcMsg app_to_rrc;
    RrcToAppMsg rrc_to_app;
    RrcToOlsrMsg rrc_to_olsr;
    OlsrToRrcMsg olsr_to_rrc;
    RrcToTdmaMsg rrc_to_tdma;
    TdmaToRrcMsg tdma_to_rrc;
    MacToRrcMsg mac_to_rrc;
    uint8_t raw[MAX_MQ_MSG_SIZE];
} GenericMessage;

// ============================================================================
// STATISTICS STRUCTURES
// ============================================================================

typedef struct {
    uint32_t enqueue_count;
    uint32_t dequeue_count;
    uint32_t timeout_count;
    uint32_t error_count;
} MQStats;

typedef struct {
    uint32_t alloc_count;
    uint32_t release_count;
    uint32_t in_use_count;
    uint32_t overflow_count;
} PoolStats;

#endif // RRC_POSIX_MQ_DEFS_H
