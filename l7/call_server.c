#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <arpa/inet.h>

#define CALL_SOCKET_PATH "/tmp/call_socket"
#define BUFFER_SIZE 1024

int server_fd = -1;
int current_sdr_id = 0;

// Signal handler for clean shutdown
void signal_handler(int sig) {
    printf("\nShutting down Call Server...\n");
    if (server_fd != -1) {
        close(server_fd);
    }
    unlink(CALL_SOCKET_PATH);
    exit(0);
}

// Parse 2-byte big endian length
uint16_t parse_frame_length(const char* buffer) {
    return ntohs(*(uint16_t*)buffer);
}

int main() {
    struct sockaddr_un addr;
    int client_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Starting Call Server...\n");
    
    // Create socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Remove any existing socket file
    unlink(CALL_SOCKET_PATH);
    
    // Set up address structure
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CALL_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd, 5) == -1) {
        perror("listen");
        close(server_fd);
        unlink(CALL_SOCKET_PATH);
        exit(EXIT_FAILURE);
    }
    
    printf("Call Server listening on %s\n", CALL_SOCKET_PATH);
    printf("Waiting for audio streams...\n\n");
    
    while (1) {
        // Accept connection
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }
        
        printf("Call client connected\n");
        
        // Read audio frames continuously
        while (1) {
            // Read frame length (2 bytes)
            bytes_received = recv(client_fd, buffer, 2, MSG_WAITALL);
            if (bytes_received != 2) {
                if (bytes_received == 0) {
                    printf("Call client disconnected\n");
                } else {
                    perror("recv length");
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
                        printf("Call client disconnected\n");
                    } else {
                        perror("recv frame");
                    }
                    break;
                }
                
                // For 128-node MANET: SDR ID is in the first byte, masked to 7 bits
                if (frame_length > 0) {
                    current_sdr_id = (unsigned char)buffer[2] & 0x7F; // Mask to 0-127 range
                    
                    // Validate SDR ID range for 128-node network
                    if (current_sdr_id > 127) {
                        printf("Warning: Invalid SDR ID %d (should be 0-127)\n", current_sdr_id);
                        current_sdr_id = 127; // Cap at maximum valid ID
                    }
                }
                
                printf("Received audio frame of length %d for SDR ID %d\n", 
                       frame_length, current_sdr_id);
            } else {
                printf("Invalid frame length: %d\n", frame_length);
                break;
            }
        }
        
        close(client_fd);
    }
    
    return 0;
}
