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

/**
 * @struct Request
 * @brief Struct to store thread arguments.
 */
struct Request {
  int session_id;                     // Session ID
  char request_pipe_path[MAX_PATH];   // Request pipe path
  char response_pipe_path[MAX_PATH];  // Response pipe path
  char server_pipe_path[MAX_PATH];    // Server pipe path
};

// Shared buffer
struct Request buffer[MAX_SESSION_COUNT];
int in = 0;     // Index to insert a new request
int out = 0;    // Index to remove a request
int count = 0;  // Number of requests in the buffer

// Mutex to protect the producer-consumer queue
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

// Condition variable to signal that the buffer is not empty
pthread_cond_t not_empty_cond = PTHREAD_COND_INITIALIZER;

// Condition variable to signal that the buffer is not full
pthread_cond_t not_full_cond = PTHREAD_COND_INITIALIZER;

// Server pipe file descriptor
int server_fd;

// Struct to store the arguments for the main thread
struct MainThreadArgs {
  char server_pipe_path[MAX_PATH];
};

// int to store the number of active threads
int session_counter = 0;

// Mutex to protect the sessions array
pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Removes a session from the buffer.
 *
 * @param session_id The ID of the session to be removed.
 */
void remove_session(int session_id) {
  // Lock the buffer mutex to protect the buffer and the session counter
  pthread_mutex_lock(&buffer_mutex);

  // Find the session in the buffer
  int i;
  for (i = 0; i < MAX_SESSION_COUNT; ++i) {
    if (buffer[i].session_id == session_id) {
      break;
    }
  }

  // Remove the session from the buffer
  buffer[i].session_id = -1;
  buffer[i].request_pipe_path[0] = '\0';
  buffer[i].response_pipe_path[0] = '\0';
  buffer[i].server_pipe_path[0] = '\0';

  // Decrement the number of active sessions
  count--;

  // Signal that the buffer is not full
  pthread_cond_signal(&not_full_cond);

  pthread_mutex_unlock(&buffer_mutex);
}

/**
 * Inserts a request into the buffer, allocating a session ID.
 *
 * @param request The pointer to the Request structure containing the request details.
 * @return The session ID assigned to the inserted request.
 */
int insert_request(struct Request* request) {
  // Lock the buffer mutex for thread safety
  pthread_mutex_lock(&buffer_mutex);

  // Wait if the buffer is full
  while (count == MAX_SESSION_COUNT) {
    pthread_cond_wait(&not_full_cond, &buffer_mutex);
  }

  // Increment the session counter and assign the session ID
  session_counter++;
  int session_id = session_counter;

  // Insert the request into the buffer
  buffer[in] = *request;
  buffer[in].session_id = session_id;  // Assign the session_id
  in = (in + 1) % MAX_SESSION_COUNT;
  count++;

  // Signal that the buffer is not empty
  pthread_cond_signal(&not_empty_cond);

  // Unlock the buffer mutex
  pthread_mutex_unlock(&buffer_mutex);

  printf("H: Session id: %d\n", session_id);
  return session_id;
}

/**
 * Retrieves a request from the buffer.
 *
 * @param request The pointer to the Request structure to store the retrieved request.
 */
void retrieve_request(struct Request* request) {
  pthread_mutex_lock(&buffer_mutex);

  // Retrieve the request from the buffer
  *request = buffer[out];               // Copy the request
  out = (out + 1) % MAX_SESSION_COUNT;  // Update the out index
  count--;

  pthread_mutex_unlock(&buffer_mutex);
}

/**
 * Handles a client request.
 *
 * @param args The pointer to the Request structure containing the request details.
 */
void* handle_client(void* args) {
  struct Request* thread_args = (struct Request*)args;

  // Open the server pipe for writing
  int server_pipe = open(thread_args->server_pipe_path, O_WRONLY);
  if (server_pipe == -1) {
    perror("Error opening server pipe");
    pthread_exit(NULL);
  }

  // Open the request pipe for reading
  int request_pipe = open(thread_args->request_pipe_path, O_RDONLY);
  if (request_pipe == -1) {
    perror("Error opening request pipe");
    pthread_exit(NULL);
  }

  // Open the response pipe for writing
  int response_pipe = open(thread_args->response_pipe_path, O_WRONLY);
  if (response_pipe == -1) {
    perror("Error opening response pipe");
    pthread_exit(NULL);
  }

  // Send the session id to the response pipe
  write(response_pipe, &thread_args->session_id, sizeof(int));

  // Handle client requests
  char op_code;
  unsigned int event_id;
  size_t num_rows, num_cols, num_seats;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
  int result;  // result of the operation

  while (read(request_pipe, &op_code, sizeof(char)) > 0 && op_code != 2) {
    printf("Operation code: %d\n", op_code);
    switch (op_code) {
      case 2:  // ems_quit
        read(request_pipe, &thread_args->session_id, sizeof(int));
        printf("Session %d terminated\n", thread_args->session_id);

        // Close the pipes
        close(request_pipe);
        close(response_pipe);

        // Remove the session from the buffer
        pthread_mutex_lock(&sessions_mutex);
        remove_session(thread_args->session_id);
        pthread_mutex_unlock(&sessions_mutex);

        // Send the session ID to the server pipe
        printf("Session %d handled\n", thread_args->session_id);
        // Free the memory allocated for the thread arguments
        free(thread_args);
        // Send thread to worker function to handle the next request
        pthread_exit(NULL);
      case 3:  // ems_create
        // Handle ems_create
        printf("Handling ems_create\n");
        read(request_pipe, &thread_args->session_id, sizeof(int));
        read(request_pipe, &event_id, sizeof(unsigned int));
        printf("Event id: %d\n", event_id);
        read(request_pipe, &num_rows, sizeof(size_t));
        printf("Num rows: %ld\n", num_rows);
        read(request_pipe, &num_cols, sizeof(size_t));
        printf("Num cols: %ld\n", num_cols);
        printf("Calling ems_create\n");
        result = ems_create(event_id, num_rows, num_cols);
        write(response_pipe, &result, sizeof(int));
        printf("ems_create done\n");
        break;
      case 4:  // ems_reserve
        // Handle ems_reserve
        printf("Handling ems_reserve\n");
        read(request_pipe, &thread_args->session_id, sizeof(int));
        read(request_pipe, &event_id, sizeof(unsigned int));
        read(request_pipe, &num_seats, sizeof(size_t));
        read(request_pipe, xs, num_seats * sizeof(size_t));
        read(request_pipe, ys, num_seats * sizeof(size_t));
        result = ems_reserve(event_id, num_seats, xs, ys);
        write(response_pipe, &result, sizeof(int));
        break;
      case 5:  // ems_show
        // Handle ems_show
        printf("Handling ems_show\n");
        read(request_pipe, &thread_args->session_id, sizeof(int));
        read(request_pipe, &event_id, sizeof(unsigned int));
        printf("Event id: %d\n", event_id);
        printf("Sending response pipe path: %s\n", thread_args->response_pipe_path);
        ems_show(response_pipe, event_id);
        break;
      case 6:  // ems_list_events
        // Handle ems_list_events
        printf("Handling ems_list_events\n");
        read(request_pipe, &thread_args->session_id, sizeof(int));
        printf("Sending response pipe path: %s\n", thread_args->response_pipe_path);
        ems_list_events(response_pipe);
        break;
      default:
        printf("Unknown operation code: %d\n", op_code);
        break;
    }
  }
  return NULL;
}

/**
 * Worker thread function responsible for retrieving requests from the buffer and processing them.
 *
 * This function runs in an infinite loop, retrieving requests from the buffer and passing them to the
 * handle_client function for further processing.
 *
 * @return NULL
 */
void* worker_function() {
  while (1) {
    struct Request current_request;  // Request to be processed

    // Condition variable to wait for a request to be available
    pthread_mutex_lock(&buffer_mutex);

    // Wait if the buffer is empty
    while (count == 0) {
      pthread_cond_wait(&not_empty_cond, &buffer_mutex);
    }
    pthread_mutex_unlock(&buffer_mutex);

    // Retrieve the request from the buffer and handle it
    retrieve_request(&current_request);
    handle_client(&current_request);
  }
}

/**
 * Main thread function responsible for extracting requests from the server pipe and inserting them into the buffer.
 *
 * This function runs in an infinite loop, extracting requests from the server pipe and inserting them into the buffer.
 *
 * @param args The pointer to the MainThreadArgs structure containing the server pipe path.
 * @return NULL
 */
void* extract_requests(void* args) {
  struct MainThreadArgs* main_args = (struct MainThreadArgs*)args;  // Cast the arguments to the correct type

  // Loop to populate the sessions array with session IDs and associated pipes
  while (1) {
    char op_code;

    // Read the operation code from the server pipe
    read(server_fd, &op_code, sizeof(char));
    if (op_code == 1) {
      // Obtain the first named pipe for the new session
      char request_pipe_path[MAX_PATH];

      // Read the request pipe path from the server pipe
      if (read(server_fd, &request_pipe_path, MAX_PATH) == -1) {
        perror("Error reading from named pipe");
        break;
      }

      // Obtain the second named pipe for the new session
      char response_pipe_path[MAX_PATH];
      if (read(server_fd, &response_pipe_path, MAX_PATH) == -1) {
        perror("Error reading from named pipe");
        break;
      }

      // Remove the "server/" prefix from the pipe paths
      if (strncmp(request_pipe_path, "server/", 7) == 0) {
        memmove(request_pipe_path, request_pipe_path + 7, strlen(request_pipe_path) - 6);
      }
      if (strncmp(response_pipe_path, "server/", 7) == 0) {
        memmove(response_pipe_path, response_pipe_path + 7, strlen(response_pipe_path) - 6);
      }

      struct Request request;
      request.session_id = -1;
      snprintf(request.request_pipe_path, MAX_PATH, "%s", request_pipe_path);
      snprintf(request.response_pipe_path, MAX_PATH, "%s", response_pipe_path);
      snprintf(request.server_pipe_path, MAX_PATH, "%s", main_args->server_pipe_path);

      // Insert the request into the requests array
      insert_request(&request);
      printf("H: Request inserted\n");
    }
  }
  return NULL;
}

/**
 * The main function for the EMS server program.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return 0 if the program executed successfully, 1 otherwise.
 */
int main(int argc, char* argv[]) {
  // Check if the required number of command-line arguments is provided
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <server_pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  // Set the state access delay if provided
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

  // Initialize the EMS
  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  char* server_pipe_path = argv[1];
  ;
  // Create a named pipe for reading and writing
  if (mkfifo(server_pipe_path, 0666) == -1) {
    perror("Error creating named pipe");
    ems_terminate();
    return 1;
  }

  // Open the pipe for reading and writing
  server_fd = open(server_pipe_path, O_RDWR);
  if (server_fd == -1) {
    perror("Error opening server pipe");
    ems_terminate();
    return 1;
  }

  // Create the main thread arguments
  struct MainThreadArgs main_args;

  // Copy the server pipe path to the main thread arguments
  snprintf(main_args.server_pipe_path, MAX_PATH, "%s", server_pipe_path);
  pthread_t host_thread;

  // Create the host thread
  pthread_create(&host_thread, NULL, extract_requests, (void*)&main_args);

  // Create worker threads
  pthread_t worker_threads[MAX_SESSION_COUNT];

  for (int i = 0; i < MAX_SESSION_COUNT; ++i) {
    if (pthread_create(&worker_threads[i], NULL, worker_function, NULL) != 0) {
      perror("Error creating thread");
      return 1;
    }
  }

  // Wait for all threads to finish before exiting
  for (int i = 0; i < MAX_SESSION_COUNT; ++i) {
    pthread_join(worker_threads[i], NULL);
  }

  // Wait for the host thread to finish
  pthread_join(host_thread, NULL);

  close(server_fd);
  unlink(server_pipe_path);
  ems_terminate();
}