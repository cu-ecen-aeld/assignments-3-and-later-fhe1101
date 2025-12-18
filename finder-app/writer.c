#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    // Should have exactly 3 arguments: program name, file path, string to write
    if (argc != 3) {
        fprintf(stderr, "Error: Invalid number of arguments\n");
        fprintf(stderr, "Usage: %s <file_path> <string>\n", argv[0]);
        return 1;
    }

    // Extract arguments
    const char *file_path = argv[1];
    const char *write_string = argv[2];

    // TODO: Open syslog with LOG_PID and LOG_USER
    openlog("writer", LOG_PID, LOG_USER);

    // open file for writing
    FILE *fp = fopen(file_path, "w");
    if (fp == NULL) {
        syslog(LOG_ERR, "Error opening file %s: %s", file_path, strerror(errno));
        return 1;
    }

    // Write string to file
    size_t result = fputs(write_string, fp);
    if (result < 0) {
        syslog(LOG_ERR, "Error writing to file %s: %s", file_path, strerror(errno));
        fclose(fp);
        return 1;
    }

    // Log success message to syslog with LOG_DEBUG
    syslog(LOG_DEBUG, "Writing %s to %s", write_string, file_path);

    // Close file
    fclose(fp);

    // Close syslog
    closelog();

    return 0;
}
