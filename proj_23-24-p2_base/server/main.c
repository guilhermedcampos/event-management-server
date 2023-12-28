#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

#define MAX_SESSIONS 3

typedef struct {
  int session_id;
  char client_pipe_path[PATH_MAX];
  char response_pipe_path[PATH_MAX];
} Session;

Session sessions[MAX_SESSIONS];
int session_counter = 0;

pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to handle a client session in a separate thread
void* handle_client(void* args) {
  int session_id = *((int*)args);
  printf("Handling session %d\n", session_id);

  // TODO: Implement logic to handle client requests for this session

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
void associate_session(int session_id, char* client_pipe_path, char* response_pipe_path) {
  if (session_counter < MAX_SESSIONS) {
    sessions[session_counter].session_id = session_id;
    strncpy(sessions[session_counter].client_pipe_path, client_pipe_path, PATH_MAX);
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

// Function to get the active session count
int get_active_sessions_count() { return session_counter; }

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
    associate_session(session_id, client_pipe_path, response_pipe_path);

    // TODO: Write new client to the producer-consumer buffer
    // Write new client to the producer-consumer buffer
    write_to_buffer(session_id, client_pipe_path, response_pipe_path);
  }

  // TODO: Close Server
  close(server_fd);
  unlink(pipe_path);  // Remove the named pipe
  ems_terminate();
}