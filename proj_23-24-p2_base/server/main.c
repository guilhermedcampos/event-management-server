#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

#define MAX_SESSIONS 3

typedef struct {
  int session_id;
  char request_pipe_path[PATH_MAX];
  char response_pipe_path[PATH_MAX];
} Session;

Session sessions[MAX_SESSIONS];
int session_counter = 0;

pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;



// Function to remove a session
void remove_session(int session_id) {
  for (int i = 0; i < session_counter; i++) {
    if (sessions[i].session_id == session_id) {
      // Shift all sessions after this one up
      for (int j = i; j < session_counter - 1; j++) {
        sessions[j] = sessions[j + 1];
      }
      session_counter--;
      break;
    }
  }
}

// Function to handle a client session in a separate thread
void* handle_client(void* args) {
  int session_id = *((int*)args);
  printf("Handling session %d\n", session_id);

  // Open the client's request and response pipes
  char request_pipe_path[PATH_MAX];
  char response_pipe_path[PATH_MAX];
  get_pipe_paths(session_id, request_pipe_path, response_pipe_path);  // Assuming this function exists
  int client_pipe = open(request_pipe_path, O_RDONLY);
  int response_pipe = open(response_pipe_path, O_WRONLY);

  // Handle client requests
  char op_code;
  unsigned int event_id;
  size_t num_rows, num_cols, num_seats;
  size_t xs[num_seats], ys[num_seats];  // Assuming a maximum number of seats

  while (read(client_pipe, &op_code, sizeof(op_code)) > 0 && op_code != 2) {
    switch (op_code) {
      case 1:  // ems_setup
        // Handle ems_setup
        char req_pipe_path[PATH_MAX];
        char resp_pipe_path[PATH_MAX];
        read(client_pipe, req_pipe_path, PATH_MAX);
        read(client_pipe, resp_pipe_path, PATH_MAX);
        break;
      case 2:  // ems_quit
        // Handle ems_quit
        break;
      case 3:  // ems_create
        // Handle ems_create
        read(client_pipe, &event_id, sizeof(event_id));
        read(client_pipe, &num_rows, sizeof(num_rows));
        read(client_pipe, &num_cols, sizeof(num_cols));
        ems_create(event_id, num_rows, num_cols);  // Assuming this function exists
        break;
      case 4:  // ems_reserve
        // Handle ems_reserve
        read(client_pipe, &event_id, sizeof(event_id));
        read(client_pipe, &num_seats, sizeof(num_seats));
        read(client_pipe, xs, num_seats * sizeof(size_t));
        read(client_pipe, ys, num_seats * sizeof(size_t));
        ems_reserve(event_id, num_seats, xs, ys);  // Assuming this function exists
        break;
      case 5:  // ems_show
        // Handle ems_show
        read(client_pipe, &event_id, sizeof(event_id));
        ems_show(response_pipe, event_id);  // Assuming this function exists
        break;
      case 6:  // ems_list_events
        // Handle ems_list_events
        ems_list_events(response_pipe);  // Assuming this function exists
        break;
      default:
        printf("Unknown operation code: %d\n", op_code);
        break;
    }
  }

  // Close the pipes
  close(client_pipe);
  close(response_pipe);

  pthread_mutex_lock(&sessions_mutex);
  remove_session(session_id);  // Remove the session after it's handled
  pthread_mutex_unlock(&sessions_mutex);

  printf("Session %d handled\n", session_id);
  pthread_exit(NULL);
}

// Function to allocate a unique session ID
int allocate_unique_session_id() {
  // Increment the counter and ensure it doesn't exceed the maximum
  session_counter = (session_counter % MAX_SESSIONS) + 1;
  return session_counter;
}

// Function to associate a session with client pipes
void associate_session(int session_id, char* request_pipe_path, char* response_pipe_path) {
  if (session_counter < MAX_SESSIONS) {
    sessions[session_counter].session_id = session_id;
    strncpy(sessions[session_counter].request_pipe_path, request_pipe_path, PATH_MAX);
    strncpy(sessions[session_counter].response_pipe_path, response_pipe_path, PATH_MAX);
  } else {
    printf("Max sessions reached, cannot add more sessions.\n");
  }
}

// Function to decrement the active session count
void decrement_active_sessions_count() {
  if (session_counter > 0) {
    session_counter--;
  }
}

// Function to get the active session count
int get_active_sessions_count() { return session_counter; }

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <server_pipe_path> [delay]\n", argv[0]);
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

  char* server_pipe_path = argv[1];

  // Create a named pipe for reading
  if (mkfifo(server_pipe_path, 0666) == -1) {
    perror("Error creating named pipe");
    ems_terminate();
    return 1;
  }

  // Open the named pipe for reading (blocking until a client connects)
  int server_fd = open(server_pipe_path, O_RDONLY);
  if (server_fd == -1) {
    perror("Error opening named pipe for reading");
    ems_terminate();
    return 1;
  }

  // TODO: Intialize server, create worker threads

  pthread_t threads[MAX_SESSIONS];  // Array to store thread IDs
  int thread_args[MAX_SESSIONS];    // Array to store thread arguments (session IDs)

  // Create worker threads for each session
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    thread_args[i] = allocate_unique_session_id();  // Allocate unique session ID for each thread
    if (pthread_create(&threads[i], NULL, handle_client, &thread_args[i]) != 0) {
      perror("Error creating thread");
      return 1;
    }
  }

  // Wait for all threads to finish before exiting
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    pthread_join(threads[i], NULL);
  }

  while (1) {
    // Read from the pipe to get client session initiation request
    char request_pipe_path[PATH_MAX];
    if (read(server_fd, request_pipe_path, sizeof(request_pipe_path)) == -1) {
      perror("Error reading from named pipe");
      break;
    }

    // Obtain the second named pipe for the new session
    char response_pipe_path[PATH_MAX];
    if (read(server_fd, response_pipe_path, sizeof(response_pipe_path)) == -1) {
      perror("Error reading from named pipe");
      break;
    }

    // Handle session initiation
    // Assign a unique session_id, associate pipes, and respond with the session_id
    int session_id = allocate_unique_session_id();

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
    associate_session(session_id, request_pipe_path, response_pipe_path);

    // TODO: Write new client to the producer-consumer buffer
    // Write new client to the producer-consumer buffer
    write_to_buffer(session_id, request_pipe_path, response_pipe_path);
  }

  // TODO: Close Server
  close(server_fd);
  unlink(server_pipe_path);  // Remove the named pipe
  ems_terminate();
}