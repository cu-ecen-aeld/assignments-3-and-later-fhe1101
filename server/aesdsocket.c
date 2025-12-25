#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include "aesdsocket.h"

#define PORT        9000
#define BACKLOG     1
#define BUFFER_SIZE 1024
#define DATA_FILE   "/var/tmp/aesdsocketdata"

static int daemon_mode = 0;
static int socket_fd = -1;
static int connection_fd = -1;

int main(int argc, char *argv[]) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    // Initialize application
    if (initialize_application(argc, argv) < 0) {
        return -1;
    }

    // Setup server socket
    if (setup_server_socket() < 0) {
        closelog();
        return -1;
    }

    // Daemonize if requested
    if (daemon_mode && daemonize() < 0) {
        return -1;
    }

    // Main accept loop
    while (1) {
        client_addr_len = sizeof(client_addr);

        // Accept connection
        connection_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (connection_fd < 0) {
            syslog(LOG_ERR, "Error accepting connection: %s", strerror(errno));
            continue;
        }

        // Process the client connection
        process_client_connection(&client_addr);
    }

    closelog();
    return 0;
}

void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");

    if (connection_fd >= 0) {
        close(connection_fd);
    }

    if (socket_fd >= 0) {
        close(socket_fd);
    }

    // Delete the data file
    if (unlink(DATA_FILE) < 0 && errno != ENOENT) {
        syslog(LOG_ERR, "Error deleting data file: %s", strerror(errno));
    }

    closelog();
    exit(0);
}

/**
 * Send the full contents of DATA_FILE to the client
 */
int send_file_contents_to_client(void) {
    int read_fd = open(DATA_FILE, O_RDONLY, 0);
    if (read_fd < 0) {
        syslog(LOG_ERR, "Error opening data file for reading: %s", strerror(errno));
        return -1;
    }

    ssize_t file_bytes_read;
    char read_buffer[BUFFER_SIZE];
    int result = 0;

    while ((file_bytes_read = read(read_fd, read_buffer, BUFFER_SIZE)) > 0) {
        if (send(connection_fd, read_buffer, file_bytes_read, 0) < 0) {
            syslog(LOG_ERR, "Error sending data to client: %s", strerror(errno));
            result = -1;
            break;
        }
    }

    if (file_bytes_read < 0) {
        syslog(LOG_ERR, "Error reading data file: %s", strerror(errno));
        result = -1;
    }

    close(read_fd);
    return result;
}

/**
 * Process a complete packet: write to file and send file contents back to client
 */
int process_complete_packet(int *data_fd, char *packet_buffer, size_t packet_len) {
    if (write(*data_fd, packet_buffer, packet_len) < 0) {
        syslog(LOG_ERR, "Error writing to data file: %s", strerror(errno));
        return -1;
    }

    close(*data_fd);

    if (send_file_contents_to_client() < 0) {
        syslog(LOG_ERR, "Failed to send file contents to client");
    }

    *data_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (*data_fd < 0) {
        syslog(LOG_ERR, "Error reopening data file: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Handle incoming data on a connection
 */
void handle_client_connection(int data_fd, char **packet_buffer, size_t *packet_size) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = recv(connection_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        // Expand packet buffer to accommodate new data
        char *temp = realloc(*packet_buffer, *packet_size + bytes_read);
        if (temp == NULL) {
            syslog(LOG_ERR, "Memory allocation failed for packet buffer");
            free(*packet_buffer);
            *packet_buffer = NULL;
            *packet_size = 0;
            break;
        }
        *packet_buffer = temp;

        // Append new data to packet buffer
        memcpy(*packet_buffer + *packet_size, buffer, bytes_read);
        *packet_size += bytes_read;

        // Process complete packets (terminated by newline)
        char *newline_pos;
        while ((newline_pos = memchr(*packet_buffer, '\n', *packet_size)) != NULL) {
            size_t packet_len = (newline_pos - *packet_buffer) + 1;

            if (process_complete_packet(&data_fd, *packet_buffer, packet_len) < 0) {
                return;
            }

            // Remove processed packet from buffer
            memmove(*packet_buffer, *packet_buffer + packet_len, *packet_size - packet_len);
            *packet_size -= packet_len;
        }
    }

    if (bytes_read < 0) {
        syslog(LOG_ERR, "Error receiving data: %s", strerror(errno));
    }
}

/**
 * Setup the server socket
 */
int setup_server_socket(void) {
    // Create socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        syslog(LOG_ERR, "Error creating socket: %s", strerror(errno));
        return -1;
    }

    // Allow reusing address
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "Error setting socket option: %s", strerror(errno));
        close(socket_fd);
        return -1;
    }

    // Prepare address structure and bind
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Error binding socket: %s", strerror(errno));
        close(socket_fd);
        return -1;
    }

    // Listen for connections
    if (listen(socket_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "Error listening on socket: %s", strerror(errno));
        close(socket_fd);
        return -1;
    }

    return 0;
}

/**
 * Initialize application: parse args, setup syslog and signal handlers
 */
int initialize_application(int argc, char *argv[]) {
    // Parse command line arguments
    daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    // Initialize syslog
    openlog("aesdsocket", LOG_PID, LOG_DAEMON);

    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    return 0;
}

/**
 * Daemonize the process
 */
int daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Error forking process: %s", strerror(errno));
        close(socket_fd);
        closelog();
        return -1;
    }
    if (pid > 0) {
        // Parent process exits
        closelog();
        exit(0);
    }
    // Child process continues as daemon
    return 0;
}

/**
 * Process a single client connection
 */
void process_client_connection(struct sockaddr_in *client_addr) {
    int data_fd;
    char client_ip[INET_ADDRSTRLEN];
    char *packet_buffer = NULL;
    size_t packet_size = 0;

    // Convert IP address to string and log
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    // Open/create data file
    data_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (data_fd < 0) {
        syslog(LOG_ERR, "Error opening data file: %s", strerror(errno));
        close(connection_fd);
        return;
    }

    // Handle client connection
    handle_client_connection(data_fd, &packet_buffer, &packet_size);

    // Log connection close
    syslog(LOG_INFO, "Closed connection from %s", client_ip);

    // Cleanup
    close(data_fd);
    close(connection_fd);
    connection_fd = -1;

    if (packet_buffer != NULL) {
        free(packet_buffer);
    }
}
