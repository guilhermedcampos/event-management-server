#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

#define MAX_SESSIONS 3

// Assuming this is a global variable
static int session_counter = 0;

// Function to allocate a unique session ID
int allocate_unique_session_id() {
    // Increment the counter and ensure it doesn't exceed the maximum
    session_counter = (session_counter % MAX_SESSIONS) + 1;
    return session_counter;
}

// Function to decrement the active session count
void decrement_active_sessions_count() {
    if (session_counter > 0) {
        session_counter--;
    }
}

// Function to get the active session count
int get_active_sessions_count() {
    return session_counter;
}

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  char* endptr;
  unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_us = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  char* pipe_path = argv[1];

  // Create a named pipe for reading
  if (mkfifo(pipe_path, 0666) == -1) {
    perror("Error creating named pipe");
    ems_terminate();
    return 1;
  }

  // Open the named pipe for reading (blocking until a client connects)
  int server_fd = open(pipe_path, O_RDONLY);
  if (server_fd == -1) {
    perror("Error opening named pipe for reading");
    ems_terminate();
    return 1;
  }

  //TODO: Intialize server, create worker threads


  while (1) {
    //TODO: Read from pipe

    while (get_active_sessions_count() >= MAX_SESSIONS) {
      sleep(1);  // Adjust the sleep duration as needed
    }

    // Read from the pipe to get client session initiation request
    char client_pipe_path[PATH_MAX];
    if (read(server_fd, client_pipe_path, sizeof(client_pipe_path)) == -1) {
      perror("Error reading from named pipe");
      break;
    }

    // Obtain the second named pipe for the new session
    char response_pipe_path[PATH_MAX];
    if (read(server_fd, response_pipe_path, sizeof(response_pipe_path)) == -1) {
      perror("Error reading from named pipe");
      break;
    }

    // TODO: Handle session initiation
  // Assign a unique session_id, associate pipes, and respond with the session_id
  int session_id = allocate_unique_session_id();  // Implement your own logic for generating a unique session_id

  if (session_id == -1) {
    perror("Error allocating session ID");
    break;
  }

  // Respond to the client with the session_id
  if (write(server_fd, &session_id, sizeof(session_id)) == -1) {
    perror("Error writing to named pipe");
    break;
  }

  // Store the association between session_id and client pipes
  //associate_session(session_id, client_pipe_path, response_pipe_path);


    //TODO: Write new client to the producer-consumer buffer
  }

  //TODO: Close Server
  close(server_fd);
  unlink(pipe_path);  // Remove the named pipe
  ems_terminate();
}