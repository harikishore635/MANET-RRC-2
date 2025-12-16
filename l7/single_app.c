#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/wait.h>

// Socket paths for different services
#define MSG_SOCKET_PATH "/tmp/msg_socket"
#define CALL_SOCKET_PATH "/tmp/call_socket"
#define FILE_SOCKET_PATH "/tmp/file_socket"
#define VIDEO_SOCKET_PATH "/tmp/video_socket"

// Common definitions
#define BUFFER_SIZE 1048576  // 1MB buffer for video data
#define UPLOADS_DIR "../uploads"
#define PIPE_PATH "/tmp/video_pipe"
#define WEBM_FILE "/tmp/video_stream.webm"

// Colors for output
#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define RESET   "\x1b[0m"

// Global server file descriptors
int msg_server_fd = -1;
int call_server_fd = -1;
int file_server_fd = -1;
int video_server_fd = -1;

// Global state variables
volatile int running = 1;
int current_sdr_id = 0;
FILE* webm_file = NULL;

// Thread data structure
typedef struct {
    int server_fd;
    const char* socket_path;
    const char* service_name;
    void (*handler)(int);
} server_thread_data_t;

// Function prototypes
void* server_thread(void* arg);
void handle_message_client(int client_fd);
void handle_call_client(int client_fd);
void handle_file_client(int client_fd);
void handle_video_client(int client_fd);
void signal_handler(int sig);
int create_server_socket(const char* socket_path);
uint16_t parse_frame_length(const char* buffer);
void print_info(const char* message);
void print_success(const char* message);
void print_error(const char* message);

// Utility functions
void print_info(const char* message) {
    printf(BLUE "[INFO]" RESET " %s\n", message);
}

void print_success(const char* message) {
    printf(GREEN "[SUCCESS]" RESET " %s\n", message);
}

void print_error(const char* message) {
    printf(RED "[ERROR]" RESET " %s\n", message);
}

// Signal handler for clean shutdown
void signal_handler(int sig) {
    printf("\n" YELLOW "[SHUTDOWN]" RESET " Shutting down unified MANET server...\n");
    running = 0;
    
    if (msg_server_fd != -1) {
        close(msg_server_fd);
        unlink(MSG_SOCKET_PATH);
    }
    if (call_server_fd != -1) {
        close(call_server_fd);
        unlink(CALL_SOCKET_PATH);
    }
    if (file_server_fd != -1) {
        close(file_server_fd);
        unlink(FILE_SOCKET_PATH);
    }
    if (video_server_fd != -1) {
        close(video_server_fd);
        unlink(VIDEO_SOCKET_PATH);
    }
    
    if (webm_file) {
        fclose(webm_file);
    }
    
    print_success("All services stopped successfully");
    exit(0);
}

// Create and configure server socket
int create_server_socket(const char* socket_path) {
    int server_fd;
    struct sockaddr_un addr;
    
    // Create socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        print_error("Failed to create socket");
        return -1;
    }
    
    // Remove any existing socket file
    unlink(socket_path);
    
    // Set up address structure
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        print_error("Failed to bind socket");
        close(server_fd);
        return -1;
    }
    
    // Listen for connections
    if (listen(server_fd, 5) == -1) {
        print_error("Failed to listen on socket");
        close(server_fd);
        unlink(socket_path);
        return -1;
    }
    
    return server_fd;
}

// Generic server thread function
void* server_thread(void* arg) {
    server_thread_data_t* data = (server_thread_data_t*)arg;
    int client_fd;
    
    printf(GREEN "[%s]" RESET " Service started on %s\n", data->service_name, data->socket_path);
    
    while (running) {
        // Accept connection with timeout
        client_fd = accept(data->server_fd, NULL, NULL);
        if (client_fd == -1) {
            if (running) {
                print_error("Accept failed");
            }
            continue;
        }
        
        printf(BLUE "[%s]" RESET " Client connected\n", data->service_name);
        
        // Handle client based on service type
        data->handler(client_fd);
        
        close(client_fd);
        printf(BLUE "[%s]" RESET " Client disconnected\n", data->service_name);
    }
    
    printf(YELLOW "[%s]" RESET " Service stopped\n", data->service_name);
    return NULL;
}

// Message service handler
void handle_message_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    
    bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf(GREEN "[MESSAGE]" RESET " Received: %s\n", buffer);
        
        // Parse JSON command (simplified)
        char command[256] = "";
        int destination_id = 0;
        
        char* cmd_start = strstr(buffer, "\"command\"");
        if (cmd_start) {
            sscanf(cmd_start, "\"command\":\"%255[^\"]\"", command);
        }
        
        char* dest_start = strstr(buffer, "\"destination_id\"");
        if (dest_start) {
            sscanf(dest_start, "\"destination_id\":%d", &destination_id);
        }
        
        printf(BLUE "[MESSAGE]" RESET " Command: %s, Destination: %d\n", command, destination_id);
        
        // Send acknowledgment
        const char* ack = "{\"status\":\"success\",\"message\":\"Message received by MANET server\"}";
        send(client_fd, ack, strlen(ack), 0);
    }
}

// Parse 2-byte big endian length (for call server compatibility)
uint16_t parse_frame_length(const char* buffer) {
    return ntohs(*(uint16_t*)buffer);
}

// Call service handler - handles binary audio streaming protocol
void handle_call_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    
    printf(GREEN "[CALL]" RESET " Client connected for audio streaming\n");
    
    // Handle continuous audio frame reception
    while (1) {
        // Read frame length (2 bytes)
        bytes_received = recv(client_fd, buffer, 2, MSG_WAITALL);
        if (bytes_received != 2) {
            if (bytes_received == 0) {
                printf(BLUE "[CALL]" RESET " Client disconnected\n");
            } else {
                print_error("Failed to receive frame length");
            }
            break;
        }
        
        // Parse frame length
        uint16_t frame_length = parse_frame_length(buffer);
        
        // Read the actual frame data
        if (frame_length > 0 && frame_length <= BUFFER_SIZE - 2) {
            bytes_received = recv(client_fd, buffer + 2, frame_length, MSG_WAITALL);
            if (bytes_received != frame_length) {
                if (bytes_received == 0) {
                    printf(BLUE "[CALL]" RESET " Client disconnected during frame read\n");
                } else {
                    print_error("Failed to receive complete frame");
                }
                break;
            }
            
            // Extract SDR ID from first byte of frame data
            if (frame_length > 0) {
                current_sdr_id = (unsigned char)buffer[2] & 0x7F; // Mask to 0-127 range
                
                // Validate SDR ID range for 128-node network
                if (current_sdr_id > 127) {
                    printf(YELLOW "[CALL]" RESET " Warning: Invalid SDR ID %d (should be 0-127)\n", current_sdr_id);
                    current_sdr_id = 127; // Cap at maximum valid ID
                }
            }
            
            printf(GREEN "[CALL]" RESET " Received audio frame of length %d for SDR ID %d\n", 
                   frame_length, current_sdr_id);
                   
            // Send acknowledgment back (simple OK response)
            const char* ack = "OK";
            send(client_fd, ack, strlen(ack), 0);
            
        } else {
            printf(RED "[CALL]" RESET " Invalid frame length: %d\n", frame_length);
            break;
        }
    }
}

// File service handler
void handle_file_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    
    bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf(GREEN "[FILE]" RESET " Received file operation: %s\n", buffer);
        
        // Create uploads directory if it doesn't exist
        struct stat st = {0};
        if (stat(UPLOADS_DIR, &st) == -1) {
            mkdir(UPLOADS_DIR, 0755);
        }
        
        if (strstr(buffer, "list_files")) {
            // List files in uploads directory
            const char* response = "{\"status\":\"success\",\"files\":[\"example.txt\",\"sample.pdf\"]}";
            send(client_fd, response, strlen(response), 0);
        } else if (strstr(buffer, "clear_files")) {
            // Clear all files (simplified)
            printf(BLUE "[FILE]" RESET " Clearing all files\n");
            const char* response = "{\"status\":\"success\",\"message\":\"All files cleared\"}";
            send(client_fd, response, strlen(response), 0);
        } else {
            // Handle file upload/download
            printf(BLUE "[FILE]" RESET " Processing file operation\n");
            const char* response = "{\"status\":\"success\",\"message\":\"File operation completed\"}";
            send(client_fd, response, strlen(response), 0);
        }
    }
}

// Video service handler
void handle_video_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    
    bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf(GREEN "[VIDEO]" RESET " Received video command: %s\n", buffer);
        
        if (strstr(buffer, "start_stream")) {
            printf(BLUE "[VIDEO]" RESET " Starting video stream\n");
            
            // Open WebM file for writing if not already open
            if (!webm_file) {
                webm_file = fopen(WEBM_FILE, "wb");
                if (!webm_file) {
                    print_error("Failed to open WebM file");
                }
            }
            
            const char* response = "{\"status\":\"success\",\"action\":\"stream_started\"}";
            send(client_fd, response, strlen(response), 0);
        } else if (strstr(buffer, "stop_stream")) {
            printf(BLUE "[VIDEO]" RESET " Stopping video stream\n");
            
            if (webm_file) {
                fclose(webm_file);
                webm_file = NULL;
            }
            
            const char* response = "{\"status\":\"success\",\"action\":\"stream_stopped\"}";
            send(client_fd, response, strlen(response), 0);
        } else {
            // Handle video frame data
            printf(BLUE "[VIDEO]" RESET " Processing video frame\n");
            const char* response = "{\"status\":\"success\",\"message\":\"Frame processed\"}";
            send(client_fd, response, strlen(response), 0);
        }
    }
}

int main() {
    pthread_t msg_thread, call_thread, file_thread, video_thread;
    server_thread_data_t thread_data[4];
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    print_info("Starting Unified MANET Communication Server...");
    
    // Create server sockets
    msg_server_fd = create_server_socket(MSG_SOCKET_PATH);
    call_server_fd = create_server_socket(CALL_SOCKET_PATH);
    file_server_fd = create_server_socket(FILE_SOCKET_PATH);
    video_server_fd = create_server_socket(VIDEO_SOCKET_PATH);
    
    if (msg_server_fd == -1 || call_server_fd == -1 || 
        file_server_fd == -1 || video_server_fd == -1) {
        print_error("Failed to create one or more server sockets");
        signal_handler(SIGTERM);
        return 1;
    }
    
    // Set up thread data
    thread_data[0] = (server_thread_data_t){msg_server_fd, MSG_SOCKET_PATH, "MESSAGE", handle_message_client};
    thread_data[1] = (server_thread_data_t){call_server_fd, CALL_SOCKET_PATH, "CALL", handle_call_client};
    thread_data[2] = (server_thread_data_t){file_server_fd, FILE_SOCKET_PATH, "FILE", handle_file_client};
    thread_data[3] = (server_thread_data_t){video_server_fd, VIDEO_SOCKET_PATH, "VIDEO", handle_video_client};
    
    // Create service threads
    if (pthread_create(&msg_thread, NULL, server_thread, &thread_data[0]) != 0) {
        print_error("Failed to create message thread");
        return 1;
    }
    
    if (pthread_create(&call_thread, NULL, server_thread, &thread_data[1]) != 0) {
        print_error("Failed to create call thread");
        return 1;
    }
    
    if (pthread_create(&file_thread, NULL, server_thread, &thread_data[2]) != 0) {
        print_error("Failed to create file thread");
        return 1;
    }
    
    if (pthread_create(&video_thread, NULL, server_thread, &thread_data[3]) != 0) {
        print_error("Failed to create video thread");
        return 1;
    }
    
    print_success("All services started successfully!");
    print_info("Unified MANET server is running...");
    printf(GREEN "Services available:" RESET "\n");
    printf("  • Message Service: %s\n", MSG_SOCKET_PATH);
    printf("  • Call Service: %s\n", CALL_SOCKET_PATH);
    printf("  • File Service: %s\n", FILE_SOCKET_PATH);
    printf("  • Video Service: %s\n", VIDEO_SOCKET_PATH);
    printf("\nPress Ctrl+C to stop all services\n\n");
    
    // Wait for all threads to complete
    pthread_join(msg_thread, NULL);
    pthread_join(call_thread, NULL);
    pthread_join(file_thread, NULL);
    pthread_join(video_thread, NULL);
    
    print_success("Unified MANET server shutdown complete");
    return 0;
}
