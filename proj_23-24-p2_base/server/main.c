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

// Struct to store session information
struct Session {
  int session_id;
  char request_pipe_path[MAX_PATH];
  char response_pipe_path[MAX_PATH];
};

// Struct to store thread arguments
struct ThreadArgs {
  int session_id;
  char request_pipe_path[MAX_PATH];
  char response_pipe_path[MAX_PATH];
  char server_pipe_path[MAX_PATH];
};

// int to store the number of active threads
int session_counter = 0;

// Array to store the sessions
struct Session sessions[MAX_SESSIONS];

// Mutex to protect the sessions array
pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to remove a session from the buffer
void remove_session(int session_id) {
  printf("Removing session %d from buffer...\n", session_id);

  // Find the session in the buffer
  int i;
  for (i = 0; i < MAX_SESSIONS; ++i) {
    if (sessions[i].session_id == session_id) {
      break;
    }
  }

  // Remove the session from the buffer
  sessions[i].session_id = -1;
  sessions[i].request_pipe_path[0] = '\0';
  sessions[i].response_pipe_path[0] = '\0';

  printf("Session %d removed from buffer\n", session_id);

  // Decrement the number of active sessions
  session_counter--;
}

// adds a session to the buffer, if max sessions is reached, it waits until a session is removed
int add_session_to_buffer(char* request_pipe_path, char* response_pipe_path) {
  // Lock the sessions mutex
  pthread_mutex_lock(&sessions_mutex);

  // Wait until there is space in the buffer
  while (session_counter == MAX_SESSIONS) {
    pthread_mutex_unlock(&sessions_mutex);
    sleep(1);
    pthread_mutex_lock(&sessions_mutex);
  }

  printf("Adding session to buffer...\n");

  // Get the session id
  int session_id = session_counter;
  session_counter++;

  // Unlock the sessions mutex
  pthread_mutex_unlock(&sessions_mutex);

  // Create the session
  struct Session session;
  session.session_id = session_id;
  snprintf(session.request_pipe_path, strlen(request_pipe_path), "%s", request_pipe_path);
  snprintf(session.response_pipe_path, strlen(response_pipe_path), "%s", response_pipe_path);

  pthread_mutex_lock(&sessions_mutex);
  sessions[session_id] = session;
  pthread_mutex_unlock(&sessions_mutex);

  printf("Session added to buffer with id: %d\n", session_id);

  return session_id;
}

// Function to handle a client session in a separate thread
void* handle_client(void* args) {
  printf("Thread with id %d created\n", pthread_self());

  // Add the session to the buffer and get its server id
  printf("Adding session to buffer...\n");

  struct ThreadArgs* thread_args = (struct ThreadArgs*)args;

  printf("Session id before adding to buffer: %d\n", thread_args->session_id);

  // Add the session to the buffer and get its server id
  thread_args->session_id = add_session_to_buffer(thread_args->request_pipe_path, thread_args->response_pipe_path);

  printf("Retrieving session id: %d\n", thread_args->session_id);

  // Check if thread_args->server_pipe_path is a valid string
  if (thread_args->server_pipe_path == NULL) {
    fprintf(stderr, "thread_args->server_pipe_path is NULL\n");
    pthread_exit(NULL);
  }
  printf("Server pipe path: %s\n", thread_args->server_pipe_path);

  // write the session id to the server pipe
  printf("Opening server pipe...\n");
  int server_pipe = open(thread_args->server_pipe_path, O_WRONLY);
  if (server_pipe == -1) {
    perror("Error opening server pipe");
    pthread_exit(NULL);
  }

  printf("Server pipe opened\n");

  write(server_pipe, &thread_args->session_id, sizeof(thread_args->session_id));
  printf("Session id written to server pipe\n");

  printf("Handling session %d\n", thread_args->session_id);

  printf("Request pipe path: %s\n", thread_args->request_pipe_path);
  int request_pipe = open(thread_args->request_pipe_path, O_RDONLY);
  if (request_pipe == -1) {
    perror("Error opening request pipe");
    pthread_exit(NULL);
  }
  printf("Request pipe: %d\n", request_pipe);

  printf("Response pipe path: %s\n", thread_args->response_pipe_path);
  // find a path to the response pipe

  int response_pipe = open(thread_args->response_pipe_path, O_WRONLY);
  if (response_pipe == -1) {
    perror("Error opening response pipe");
    pthread_exit(NULL);
  }
  printf("Response pipe: %d\n", response_pipe);

  // Handle client requests
  char op_code;
  unsigned int event_id;
  size_t num_rows, num_cols, num_seats;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];  // Assuming a maximum number of seats

  while (read(request_pipe, &op_code, sizeof(char)) > 0 && op_code != 2) {
    printf("Operation code: %d\n", op_code);
    switch (op_code) {
      case 2:  // ems_quit
        // Handle ems_quit
        break;
      case 3:  // ems_create
        // Handle ems_create
        printf("Handling ems_create\n");
        read(request_pipe, &event_id, sizeof(event_id));
        read(request_pipe, &num_rows, sizeof(num_rows));
        read(request_pipe, &num_cols, sizeof(num_cols));
        ems_create(event_id, num_rows, num_cols);  // Assuming this function exists
        break;
      case 4:  // ems_reserve
        // Handle ems_reserve
        printf("Handling ems_reserve\n");
        read(request_pipe, &event_id, sizeof(event_id));
        read(request_pipe, &num_seats, sizeof(num_seats));
        read(request_pipe, xs, num_seats * sizeof(size_t));
        read(request_pipe, ys, num_seats * sizeof(size_t));
        ems_reserve(event_id, num_seats, xs, ys);  // Assuming this function exists
        break;
      case 5:  // ems_show
        // Handle ems_show
        printf("Handling ems_show\n");
        read(request_pipe, &event_id, sizeof(event_id));
        ems_show(response_pipe, event_id);  // Assuming this function exists
        break;
      case 6:  // ems_list_events
        // Handle ems_list_events
        printf("Handling ems_list_events\n");
        ems_list_events(response_pipe);  // Assuming this function exists
        break;
      default:
        printf("Unknown operation code: %d\n", op_code);
        break;
    }
  }

  printf("Session %d terminated\n", thread_args->session_id);

  // Close the pipes
  close(request_pipe);
  close(response_pipe);

  pthread_mutex_lock(&sessions_mutex);
  remove_session(thread_args->session_id);  // remove the session from the buffer
  pthread_mutex_unlock(&sessions_mutex);

  printf("Session %d handled\n", thread_args->session_id);
  pthread_exit(NULL);
}

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
  printf("Server pipe created: %s\n", server_pipe_path);

  // Open the named pipe for reading (blocking until a client connects)
  printf("Opening server pipe...\n");
  // Open the pipe for reading and writing
  int server_fd = open(server_pipe_path, O_RDWR);
  if (server_fd == -1) {
    perror("Error opening server pipe");
    ems_terminate();
    return 1;
  }
  printf("Server pipe opened\n");

  // Loop to populate the sessions array with session IDs and associated pipes
  // while reading from server pipe isnt a blank space
  printf("Waiting for clients...\n");
  printf("Server pipe: %d\n", server_fd);
  while (1) {
    char op_code;
    read(server_fd, &op_code, sizeof(char));
    if (op_code == 1) {
      printf("New session request\n");

      // Obtain the first named pipe for the new session

      char request_pipe_path[MAX_PATH];

      if (read(server_fd, &request_pipe_path, sizeof(MAX_PATH)) == -1) {
        perror("Error reading from named pipe");
        break;
      }

      printf("Request pipe path: %s\n", request_pipe_path);

      // Obtain the second named pipe for the new session

      char response_pipe_path[MAX_PATH];
      if (read(server_fd, &response_pipe_path, sizeof(MAX_PATH)) == -1) {
        perror("Error reading from named pipe");
        break;
      }

      printf("Response pipe path: %s\n", response_pipe_path);

      printf("Allocating session ID...\n");

      // Create thread to handle the client function
      printf("Creating thread...\n");
      pthread_t thread;
      struct ThreadArgs thread_args;
      thread_args.session_id = -1;
      snprintf(thread_args.request_pipe_path, sizeof(request_pipe_path), "%s", request_pipe_path);
      snprintf(thread_args.response_pipe_path, sizeof(response_pipe_path), "%s", response_pipe_path);
      snprintf(thread_args.server_pipe_path, sizeof(server_pipe_path), "%s", server_pipe_path);
      pthread_create(&thread, NULL, handle_client, (void*)&thread_args);
    }
    if (op_code == 2) {
      printf("Client disconnected\n");
      break;
    }
  }

  /*
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
  */

  // TODO: Close Server
  close(server_fd);
  unlink(server_pipe_path);  // Remove the named pipe
  ems_terminate();
}