#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

#define MAX_SESSIONS 3

struct Session {
  int session_id;
  char request_pipe_path[PATH_MAX];
  char response_pipe_path[PATH_MAX];
};

struct Session sessions[MAX_SESSIONS];
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
  struct Session* thread_data = (struct Session*)args;
  printf("Handling session %d\n", thread_data->session_id);

  // Open the client's request and response pipes
  char request_pipe_path[PATH_MAX];
  char response_pipe_path[PATH_MAX];

  int request_pipe = open(request_pipe_path, O_RDONLY);
  int response_pipe = open(response_pipe_path, O_WRONLY);

  // Handle client requests
  char op_code;
  unsigned int event_id;
  size_t num_rows, num_cols, num_seats;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];  // Assuming a maximum number of seats

  while (read(request_pipe, &op_code, sizeof(op_code)) > 0 && op_code != 2) {
    switch (op_code) {
      case 2:  // ems_quit
        // Handle ems_quit
        break;
      case 3:  // ems_create
        // Handle ems_create
        read(request_pipe, &event_id, sizeof(event_id));
        read(request_pipe, &num_rows, sizeof(num_rows));
        read(request_pipe, &num_cols, sizeof(num_cols));
        ems_create(event_id, num_rows, num_cols);  // Assuming this function exists
        break;
      case 4:  // ems_reserve
        // Handle ems_reserve
        read(request_pipe, &event_id, sizeof(event_id));
        read(request_pipe, &num_seats, sizeof(num_seats));
        read(request_pipe, xs, num_seats * sizeof(size_t));
        read(request_pipe, ys, num_seats * sizeof(size_t));
        ems_reserve(event_id, num_seats, xs, ys);  // Assuming this function exists
        break;
      case 5:  // ems_show
        // Handle ems_show
        read(request_pipe, &event_id, sizeof(event_id));
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
  close(request_pipe);
  close(response_pipe);

  pthread_mutex_lock(&sessions_mutex);
  remove_session(thread_data->session_id);  // Remove the session after it's handled
  pthread_mutex_unlock(&sessions_mutex);

  printf("Session %d handled\n", thread_data->session_id);
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

  printf("Starting server...\n");
  printf("Server pipe path: %s\n", argv[1]);

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

  printf("Initializing EMS...\n");
  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }
  printf("EMS initialized\n");

  char* server_pipe_path = argv[1];

  printf("Creating server pipe...\n");
  // Create a named pipe for reading
  if (mkfifo(server_pipe_path, 0666) == -1) {  // 0666 is the permission for the pipe to be read and written
    perror("Error creating named pipe");
    ems_terminate();
    return 1;
  }
  printf("Server pipe created\n");

  // Open the named pipe for reading (blocking until a client connects)
  printf("Opening server pipe...\n");
  int server_fd = open(server_pipe_path, O_RDONLY);
  if (server_fd == -1) {
    perror("Error opening server pipe");
    ems_terminate();
    return 1;
  }
  printf("Server pipe opened\n");

  // Loop to populate the sessions array with session IDs and associated pipes
  // while reading from server pipe isnt a blank space
  printf("Waiting for clients...\n");
  while (read(server_fd, &server_fd, sizeof(server_fd)) != ' ') {
    // Read from the pipe to get client session initiation request
    printf("Client connected\n");
    printf("Reading from server pipe...\n");

    char request_pipe_path[PATH_MAX];

    if (read(server_fd, request_pipe_path, sizeof(request_pipe_path)) == -1) {
      perror("Error reading from named pipe");
      break;
    }

    printf("Request pipe path: %s\n", request_pipe_path);

    // Obtain the second named pipe for the new session

    printf("Reading from server pipe...\n");

    char response_pipe_path[PATH_MAX];
    if (read(server_fd, response_pipe_path, sizeof(response_pipe_path)) == -1) {
      perror("Error reading from named pipe");
      break;
    }

    printf("Response pipe path: %s\n", response_pipe_path);

    // Handle session initiation
    // Assign a unique session_id, associate pipes, and respond with the session_id

    printf("Allocating session ID...\n");

    int session_id = allocate_unique_session_id();

    printf("Session ID: %d\n", session_id);

    if (session_id == -1) {
      perror("Error allocating session ID");
      break;
    }

    // Respond to the client with the session_id
    if (write(server_fd, &session_id, sizeof(session_id)) == -1) {
      perror("Error writing to named pipe");
      break;
    }

    // TODO: Write new client to the producer-consumer buffer
    // Write new client to the producer-consumer buffer
    // Store the association between session_id and client pipes
    associate_session(session_id, request_pipe_path, response_pipe_path);
  }

  // TODO: Intialize server, create worker threads

  pthread_t threads[MAX_SESSIONS];  // Array to store thread IDs
  struct Session
      thread_args[MAX_SESSIONS];  // Array to store thread arguments (session IDs, request and response pipe paths)

  // Create worker threads for each session
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    thread_args[i].session_id = sessions[i].session_id;  // Allocate unique session ID for each thread
    snprintf(thread_args[i].request_pipe_path, strlen(sessions[i].request_pipe_path), "%s",
             sessions[i].request_pipe_path);
    snprintf(thread_args[i].response_pipe_path, strlen(sessions[i].response_pipe_path), "%s",
             sessions[i].response_pipe_path);
    if (pthread_create(&threads[i], NULL, handle_client, (void*)&thread_args[i]) != 0) {
      perror("Error creating thread");
      return 1;
    }
  }

  // Wait for all threads to finish before exiting
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    pthread_join(threads[i], NULL);
  }

  // TODO: Close Server
  close(server_fd);
  unlink(server_pipe_path);  // Remove the named pipe
  ems_terminate();
}