/*
 * RRC Message Queue System - Thread-Safe Inter-Layer Communication
 * Header file for MANET RRC message queue definitions and APIs
 */

#ifndef RRC_MESSAGE_QUEUE_H
#define RRC_MESSAGE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#include <semaphore.h>

// Configuration
#define MESSAGE_QUEUE_SIZE 32
#define REQUEST_TIMEOUT_MS 5000
#define PAYLOAD_SIZE_BYTES 2800

// Message types for different layer communications
typedef enum {
    // OLSR L3 Messages
    MSG_TYPE_OLSR_ROUTE_REQUEST = 1,
    MSG_TYPE_OLSR_ROUTE_RESPONSE = 2,
    MSG_TYPE_OLSR_TRIGGER_DISCOVERY = 3,
    MSG_TYPE_OLSR_HELLO_NC = 4,
    
    // TDMA L2 Messages
    MSG_TYPE_TDMA_SLOT_CHECK_REQUEST = 5,
    MSG_TYPE_TDMA_SLOT_CHECK_RESPONSE = 6,
    MSG_TYPE_TDMA_NC_SLOT_REQUEST = 7,
    MSG_TYPE_TDMA_NC_SLOT_RESPONSE = 8,
    
    // PHY Layer Messages
    MSG_TYPE_PHY_METRICS_REQUEST = 9,
    MSG_TYPE_PHY_METRICS_RESPONSE = 10,
    MSG_TYPE_PHY_LINK_STATUS_REQUEST = 11,
    MSG_TYPE_PHY_LINK_STATUS_RESPONSE = 12,
    MSG_TYPE_PHY_PACKET_COUNT_REQUEST = 13,
    MSG_TYPE_PHY_PACKET_COUNT_RESPONSE = 14,
    
    // Application Layer Messages
    MSG_TYPE_APP_TO_RRC = 15,
    MSG_TYPE_RRC_TO_APP = 16
} MessageType;

// Generic message header
typedef struct {
    MessageType msg_type;
    uint32_t request_id;
    uint32_t timestamp;
    uint8_t source_layer;
    uint8_t dest_layer;
} MessageHeader;

// OLSR Messages
typedef struct {
    MessageHeader header;
    uint8_t dest_node;
    uint8_t src_node;
} OLSR_RouteRequest;

typedef struct {
    MessageHeader header;
    uint8_t dest_node;
    uint8_t next_hop;
    uint8_t hop_count;
    bool route_available;
} OLSR_RouteResponse;

typedef struct {
    MessageHeader header;
    uint8_t dest_node;
} OLSR_TriggerDiscovery;

typedef struct {
    MessageHeader header;
    uint8_t source_node;
    uint8_t my_nc_slot;
    uint8_t payload[256];
    size_t payload_len;
} OLSR_HelloNC;

// TDMA Messages
typedef struct {
    MessageHeader header;
    uint8_t next_hop;
    int priority;
} TDMA_SlotCheckRequest;

typedef struct {
    MessageHeader header;
    uint8_t next_hop;
    bool slot_available;
} TDMA_SlotCheckResponse;

typedef struct {
    MessageHeader header;
    uint8_t payload[PAYLOAD_SIZE_BYTES];
    size_t payload_len;
} TDMA_NCSlotRequest;

typedef struct {
    MessageHeader header;
    bool granted;
    uint8_t assigned_slot;
} TDMA_NCSlotResponse;

// PHY Messages
typedef struct {
    MessageHeader header;
    uint8_t node_id;
} PHY_MetricsRequest;

typedef struct {
    MessageHeader header;
    uint8_t node_id;
    float rssi_dbm;
    float snr_db;
    float per_percent;
} PHY_MetricsResponse;

typedef struct {
    MessageHeader header;
    uint8_t node_id;
} PHY_LinkStatusRequest;

typedef struct {
    MessageHeader header;
    uint8_t node_id;
    bool link_active;
} PHY_LinkStatusResponse;

typedef struct {
    MessageHeader header;
    uint8_t node_id;
} PHY_PacketCountRequest;

typedef struct {
    MessageHeader header;
    uint8_t node_id;
    uint32_t packet_count;
} PHY_PacketCountResponse;

// Application Messages
typedef struct {
    MessageHeader header;
    uint8_t src_id;
    uint8_t dest_id;
    uint8_t cast_type;
    uint8_t message_type;
    uint8_t payload[PAYLOAD_SIZE_BYTES];
    size_t payload_len;
} APP_ToRRC_Msg;

typedef struct {
    MessageHeader header;
    uint8_t src_id;
    uint8_t dest_id;
    uint8_t next_hop;
    uint8_t payload[PAYLOAD_SIZE_BYTES];
    size_t payload_len;
    uint32_t sequence_number;
} RRC_ToAPP_Frame;

// Union for all message types
typedef union {
    MessageHeader header;
    OLSR_RouteRequest olsr_route_req;
    OLSR_RouteResponse olsr_route_resp;
    OLSR_TriggerDiscovery olsr_trigger;
    OLSR_HelloNC olsr_hello;
    TDMA_SlotCheckRequest tdma_check_req;
    TDMA_SlotCheckResponse tdma_check_resp;
    TDMA_NCSlotRequest tdma_nc_req;
    TDMA_NCSlotResponse tdma_nc_resp;
    PHY_MetricsRequest phy_metrics_req;
    PHY_MetricsResponse phy_metrics_resp;
    PHY_LinkStatusRequest phy_link_req;
    PHY_LinkStatusResponse phy_link_resp;
    PHY_PacketCountRequest phy_count_req;
    PHY_PacketCountResponse phy_count_resp;
    APP_ToRRC_Msg app_to_rrc;
    RRC_ToAPP_Frame rrc_to_app;
} LayerMessage;

// Message Queue Structure
typedef struct {
    LayerMessage buffer[MESSAGE_QUEUE_SIZE];
    int read_index;
    int write_index;
    int count;
    pthread_mutex_t mutex;
    sem_t empty_slots;
    sem_t filled_slots;
    uint32_t enqueue_count;
    uint32_t dequeue_count;
    uint32_t overflow_count;
    char name[64];
} MessageQueue;

// Global queue declarations
extern MessageQueue rrc_to_olsr_queue;
extern MessageQueue olsr_to_rrc_queue;
extern MessageQueue rrc_to_tdma_queue;
extern MessageQueue tdma_to_rrc_queue;
extern MessageQueue rrc_to_phy_queue;
extern MessageQueue phy_to_rrc_queue;
extern MessageQueue app_to_rrc_queue;
extern MessageQueue rrc_to_app_queue;
extern MessageQueue mac_to_rrc_relay_queue;

// Function prototypes
int message_queue_init(MessageQueue *mq, const char *name);
void message_queue_cleanup(MessageQueue *mq);
int message_queue_enqueue(MessageQueue *mq, const LayerMessage *msg, int timeout_ms);
int message_queue_dequeue(MessageQueue *mq, LayerMessage *msg, int timeout_ms);
bool message_queue_has_messages(MessageQueue *mq);
uint32_t generate_request_id(void);

// Initialization and cleanup
void init_all_message_queues(void);
void cleanup_all_message_queues(void);
void print_all_message_queue_stats(void);

#endif // RRC_MESSAGE_QUEUE_H
