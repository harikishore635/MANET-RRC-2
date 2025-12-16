#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>

#define SOCKET_PATH "/tmp/video_socket"
#define PIPE_PATH "/tmp/video_pipe"
#define WEBM_FILE "/tmp/video_stream.webm"
#define BUFFER_SIZE 1048576  // 1MB buffer for video data

// Colors for output
#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define RESET   "\x1b[0m"

volatile int running = 1;
FILE* webm_file = NULL;

void print_info(const char* message) {
    printf(BLUE "[INFO]" RESET " %s\n", message);
    fflush(stdout);
}

void print_error(const char* message) {
    printf(RED "[ERROR]" RESET " %s\n", message);
    fflush(stdout);
}

void print_success(const char* message) {
    printf(GREEN "[SUCCESS]" RESET " %s\n", message);
    fflush(stdout);
}

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        print_info("Received shutdown signal");
        running = 0;
        if (webm_file) {
            fclose(webm_file);
            webm_file = NULL;
        }
        // Kill any running VLC processes
        system("pkill -f 'vlc.*video_stream.webm'");
    }
}

void stop_vlc_player() {
    print_info("Stopping any existing VLC players...");
    // Kill VLC processes playing our video file
    system("pkill -f 'vlc.*video_stream.webm'");
    sleep(1); // Give time for processes to terminate
}

void start_vlc_player() {
    print_info("Starting VLC player for video stream...");
    
    // Start VLC in the background to play the WebM file continuously
    if (fork() == 0) {
        // Child process - start VLC
        execl("/usr/bin/vlc", "vlc", 
              WEBM_FILE,
              "--intf", "qt",
              "--no-video-title-show",
              "--file-caching=100",
              "--network-caching=100",
              "--live-caching=100",
              "--input-repeat=999999",
              "--loop",
              (char*)NULL);
        
        // If execl fails, try with different path
        execl("/bin/vlc", "vlc", 
              WEBM_FILE,
              "--intf", "qt",
              "--no-video-title-show",
              "--file-caching=100",
              "--network-caching=100",
              "--live-caching=100",
              "--input-repeat=999999",
              "--loop",
              (char*)NULL);
        
        print_error("Failed to start VLC");
        exit(1);
    }
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_un server_addr;
    char buffer[BUFFER_SIZE];
    uint32_t frame_length;
    ssize_t bytes_received;

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    print_info("Starting MANET Video Server...");

    // Remove old WebM file if it exists
    unlink(WEBM_FILE);

    // Create Unix Domain Socket
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket == -1) {
        print_error("Failed to create socket");
        perror("socket");
        return 1;
    }

    // Remove existing socket file
    unlink(SOCKET_PATH);

    // Set up socket address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strcpy(server_addr.sun_path, SOCKET_PATH);

    // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        print_error("Failed to bind socket");
        perror("bind");
        close(server_socket);
        return 1;
    }

    // Listen for connections
    if (listen(server_socket, 1) == -1) {
        print_error("Failed to listen on socket");
        perror("listen");
        close(server_socket);
        unlink(SOCKET_PATH);
        return 1;
    }

    print_success("Video server listening on " SOCKET_PATH);
    print_info("Waiting for video client connection...");

    while (running) {
        // Accept client connection
        client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == -1) {
            if (running) {
                print_error("Failed to accept connection");
                perror("accept");
            }
            continue;
        }

        print_success("Video client connected");
        
        // Stop any existing VLC players
        stop_vlc_player();
        
        // Remove old WebM file and create fresh one
        unlink(WEBM_FILE);
        if (webm_file) {
            fclose(webm_file);
        }
        webm_file = fopen(WEBM_FILE, "wb");
        if (!webm_file) {
            print_error("Failed to create fresh WebM file");
            perror("fopen");
            close(client_socket);
            continue;
        }
        print_success("Fresh WebM file created for new session");
        
        // Start VLC player for the new session
        start_vlc_player();
        sleep(2); // Give VLC time to start

        // Process video frames
        int frame_count = 0;
        while (running) {
            // Read frame length (4 bytes, big endian)
            bytes_received = recv(client_socket, &frame_length, sizeof(frame_length), MSG_WAITALL);
            if (bytes_received <= 0) {
                if (bytes_received == 0) {
                    print_info("Client disconnected");
                } else {
                    print_error("Failed to receive frame length");
                }
                break;
            }

            // Convert from network byte order
            frame_length = ntohl(frame_length);

            if (frame_length > BUFFER_SIZE) {
                print_error("Frame size too large");
                break;
            }

            // Read frame data
            size_t total_received = 0;
            while (total_received < frame_length && running) {
                bytes_received = recv(client_socket, buffer + total_received, 
                                    frame_length - total_received, 0);
                if (bytes_received <= 0) {
                    print_error("Failed to receive frame data");
                    break;
                }
                total_received += bytes_received;
            }

            if (total_received != frame_length) {
                print_error("Incomplete frame received");
                break;
            }

            printf(BLUE "[INFO]" RESET " Received video frame #%d of size %u bytes\n", 
                   ++frame_count, frame_length);

            // Write frame to WebM file
            if (fwrite(buffer, 1, frame_length, webm_file) != frame_length) {
                print_error("Failed to write to WebM file");
                perror("fwrite");
                break;
            }
            
            // Flush to ensure data is written immediately
            fflush(webm_file);

            printf(BLUE "[INFO]" RESET " Written to %s (total frames: %d)\n", 
                   WEBM_FILE, frame_count);
            fflush(stdout);
        }

        // Close client socket
        close(client_socket);
        
        // Close WebM file but don't delete it yet (VLC might still be playing)
        if (webm_file) {
            fclose(webm_file);
            webm_file = NULL;
        }
        
        print_info("Client session ended");
    }

    // Cleanup
    if (webm_file) {
        fclose(webm_file);
        webm_file = NULL;
    }
    close(server_socket);
    unlink(SOCKET_PATH);
    unlink(WEBM_FILE);
    
    // Stop any running VLC processes
    stop_vlc_player();
    
    print_success("Video server shutdown complete");

    return 0;
}
