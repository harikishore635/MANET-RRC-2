#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>

#define SOCKET_PATH "/tmp/file_socket"
#define BUFFER_SIZE 1024
#define UPLOADS_DIR "../uploads"

int server_fd = -1;

// Signal handler for clean shutdown
void signal_handler(int sig) {
    printf("\nShutting down File Server...\n");
    if (server_fd != -1) {
        close(server_fd);
    }
    unlink(SOCKET_PATH);
    exit(0);
}

// Create uploads directory if it doesn't exist
void ensure_uploads_dir() {
    struct stat st = {0};
    if (stat(UPLOADS_DIR, &st) == -1) {
        if (mkdir(UPLOADS_DIR, 0755) == -1) {
            perror("Error creating uploads directory");
            exit(1);
        }
        printf("Created uploads directory: %s\n", UPLOADS_DIR);
    }
}

// Handle file reception from client
void handle_file_transfer(int client_fd) {
    char buffer[BUFFER_SIZE];
    char filename[256];
    long file_size = 0;
    char filepath[512];
    FILE *file = NULL;
    long bytes_received = 0;
    
    printf("Starting file transfer from client...\n");
    
    // Read metadata (filename:filesize)
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        printf("Error reading file metadata\n");
        write(client_fd, "ERROR: Failed to read metadata\n", 31);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    // Parse metadata
    char *newline = strchr(buffer, '\n');
    if (newline) {
        *newline = '\0';
        
        char *colon = strchr(buffer, ':');
        if (colon) {
            *colon = '\0';
            strncpy(filename, buffer, sizeof(filename) - 1);
            filename[sizeof(filename) - 1] = '\0';
            file_size = atol(colon + 1);
            
            printf("Receiving file: %s (%ld bytes)\n", filename, file_size);
        } else {
            printf("Invalid metadata format\n");
            write(client_fd, "ERROR: Invalid metadata format\n", 31);
            return;
        }
    } else {
        printf("Metadata missing newline\n");
        write(client_fd, "ERROR: Metadata missing newline\n", 32);
        return;
    }
    
    // Create full file path
    snprintf(filepath, sizeof(filepath), "%s/%s", UPLOADS_DIR, filename);
    
    // Open file for writing
    file = fopen(filepath, "wb");
    if (!file) {
        printf("Error creating file: %s\n", strerror(errno));
        write(client_fd, "ERROR: Failed to create file\n", 29);
        return;
    }
    
    // Receive file data
    while (bytes_received < file_size) {
        bytes_read = read(client_fd, buffer, sizeof(buffer));
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                printf("Connection closed by client\n");
            } else {
                printf("Error reading file data: %s\n", strerror(errno));
            }
            break;
        }
        
        // Check for EOF marker in the data
        char *eof_pos = strstr(buffer, "EOF\n");
        if (eof_pos) {
            // Write only the data before EOF marker
            size_t data_before_eof = eof_pos - buffer;
            if (data_before_eof > 0) {
                size_t written = fwrite(buffer, 1, data_before_eof, file);
                if (written != data_before_eof) {
                    printf("Error writing final data to file: %s\n", strerror(errno));
                    break;
                }
                bytes_received += data_before_eof;
            }
            printf("EOF marker received\n");
            break;
        }
        
        // Write data to file
        size_t written = fwrite(buffer, 1, bytes_read, file);
        if (written != bytes_read) {
            printf("Error writing to file: %s\n", strerror(errno));
            break;
        }
        
        bytes_received += bytes_read;
        
        // Log progress every 10%
        static long last_progress_logged = 0;
        long current_progress = ((double)bytes_received / file_size) * 100.0;
        if (current_progress - last_progress_logged >= 10 || bytes_received == file_size) {
            printf("File transfer progress: %ld%% (%ld/%ld bytes)\n", 
                   current_progress, bytes_received, file_size);
            last_progress_logged = current_progress;
        }
    }
    
    fclose(file);
    
    // Small delay to ensure all data is written
    sleep(1); // 1 second delay
    
    if (bytes_received >= file_size) {
        printf("File transfer completed successfully: %s (%ld bytes)\n", filename, bytes_received);
        write(client_fd, "SUCCESS: File received successfully\n", 36);
    } else {
        printf("File transfer incomplete: %ld/%ld bytes\n", bytes_received, file_size);
        write(client_fd, "ERROR: File transfer incomplete\n", 32);
        // Remove incomplete file
        unlink(filepath);
    }
}

int main() {
    struct sockaddr_un addr;
    int client_fd;
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Ensure uploads directory exists
    ensure_uploads_dir();
    
    // Create socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(1);
    }
    
    // Remove existing socket file
    unlink(SOCKET_PATH);
    
    // Bind socket
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        exit(1);
    }
    
    // Listen for connections
    if (listen(server_fd, 5) == -1) {
        perror("listen");
        close(server_fd);
        unlink(SOCKET_PATH);
        exit(1);
    }
    
    printf("File Server listening on %s\n", SOCKET_PATH);
    printf("Files will be saved to: %s\n", UPLOADS_DIR);
    printf("Press Ctrl+C to stop the server\n");
    
    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }
        
        printf("Client connected for file transfer\n");
        handle_file_transfer(client_fd);
        close(client_fd);
        printf("Client disconnected\n");
    }
    
    return 0;
}
