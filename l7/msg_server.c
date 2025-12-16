#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#define SOCKET_PATH "/tmp/msg_socket"
#define BUFFER_SIZE 1024
/*typedef struct{
    char buffer[1024];
}rcvPkt;*/

int server_fd = -1;

// Signal handler for clean shutdown
void signal_handler(int sig) {
    printf("\nShutting down SDR application...\n");
    if (server_fd != -1) {
        close(server_fd);
    }
    unlink(SOCKET_PATH);
    exit(0);
}

// Simple JSON parser for extracting command and destination_id
void parse_json_command(const char* json_str, char* command, int* destination_id) {
    char* cmd_start = strstr(json_str, "\"command\"");
    char* dest_start = strstr(json_str, "\"destination_id\"");
    
    // Initialize defaults
    strcpy(command, "");
    *destination_id = 0;
    
    if (cmd_start) {
        // Find the value after "command":
        cmd_start = strchr(cmd_start, ':');
        if (cmd_start) {
            cmd_start++;
            // Skip whitespace and quotes
            while (*cmd_start == ' ' || *cmd_start == '\t') cmd_start++;
            if (*cmd_start == '"') {
                cmd_start++;
                char* cmd_end = strchr(cmd_start, '"');
                if (cmd_end) {
                    int len = cmd_end - cmd_start;
                    if (len < 63) {  // Ensure we don't overflow
                        strncpy(command, cmd_start, len);
                        command[len] = '\0';
                    }
                }
            }
        }
    }
    
    if (dest_start) {
        // Find the value after "destination_id":
        dest_start = strchr(dest_start, ':');
        if (dest_start) {
            dest_start++;
            while (*dest_start == ' ' || *dest_start == '\t') dest_start++;
            *destination_id = atoi(dest_start);
        }
    }
}

int main() {
    struct sockaddr_un addr;
    int client_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Starting SDR Application...\n");
    
    // Create socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Remove any existing socket file
    unlink(SOCKET_PATH);
    
    // Set up address structure
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
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
        unlink(SOCKET_PATH);
        exit(EXIT_FAILURE);
    }
    
    printf("Message Server listening on %s\n", SOCKET_PATH);
    printf("Waiting for messages...\n\n");
    
    while (1) {
        // Accept connection
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }
        
        printf("Client connected\n");
        
        // Receive message
        //Dhanush - rcvPkt var_x;
        bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            //Dhanush - memcpy(var_x.buffer, buffer, bytes_received);
            //Dhanush - printf("%s", var_x.buffer);
            buffer[bytes_received] = '\0';
            printf("Message received: %s\n", buffer);
            
            // Check if it's a JSON command
            if (buffer[0] == '{') {
                char command[64];
                int destination_id;
                parse_json_command(buffer, command, &destination_id);
                
                if (strcmp(command, "start_call") == 0) {
                    printf("Starting call to SDR with ID: %d\n", destination_id);
                    
                    // Send acknowledgment for call command
                    const char* ack = "Call command received";
                    send(client_fd, ack, strlen(ack), 0);
                } else {
                    // Unknown command
                    const char* ack = "Unknown command";
                    send(client_fd, ack, strlen(ack), 0);
                }
            } else {
                // Regular text message
                const char* ack = "Message received by SDR";
                send(client_fd, ack, strlen(ack), 0);
            }
            
            printf("Acknowledgment sent\n\n");
        } else if (bytes_received == 0) {
            printf("Client disconnected\n");
        } else {
            perror("recv");
        }
        
        close(client_fd);
    }
    
    return 0;
}
