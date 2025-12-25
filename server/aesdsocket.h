#ifndef D0BAD0DB_4B5D_4015_AF61_4468F016EF65
#define D0BAD0DB_4B5D_4015_AF61_4468F016EF65

#include <stddef.h>
#include <netinet/in.h>

/**
 * Signal handler for SIGINT and SIGTERM
 */
void signal_handler(int sig);

/**
 * Send the full contents of DATA_FILE to the client
 */
int send_file_contents_to_client(void);

/**
 * Process a complete packet: write to file and send file contents back to client
 */
int process_complete_packet(int *data_fd, char *packet_buffer, size_t packet_len);

/**
 * Handle incoming data on a connection
 */
void handle_client_connection(int data_fd, char **packet_buffer, size_t *packet_size);

/**
 * Setup the server socket
 */
int setup_server_socket(void);

/**
 * Initialize application: parse args, setup syslog and signal handlers
 */
int initialize_application(int argc, char *argv[]);

/**
 * Daemonize the process
 */
int daemonize(void);

/**
 * Process a single client connection
 */
void process_client_connection(struct sockaddr_in *client_addr);

#endif /* D0BAD0DB_4B5D_4015_AF61_4468F016EF65 */
