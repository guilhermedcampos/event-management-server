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

// Declare a rwlock
pthread_rwlock_t server_pipe_rwlock = PTHREAD_RWLOCK_INITIALIZER;


// Struct to store session information
struct Session {
  int session_id;
  char request_pipe_path[MAX_PATH];
  char response_pipe_path[MAX_PATH];
};

// Struct to store thread arguments
struct Request {
  int session_id;
  char request_pipe_path[MAX_PATH];
  char response_pipe_path[MAX_PATH];
  char server_pipe_path[MAX_PATH];
};

// Shared buffer
struct Request buffer[MAX_SESSION_COUNT];
int in = 0; // Index to insert a new request
int out = 0; // Index to remove a request
int count = 0; // Number of requests in the buffer

// Mutex and condition variables
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t not_empty_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t not_full_cond = PTHREAD_COND_INITIALIZER;

struct MainThreadArgs {
    int server_fd;
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

int insert_request(struct Request* request) {
  pthread_mutex_lock(&buffer_mutex);

  // Wait if the buffer is full
  while (count == MAX_SESSION_COUNT) {
    printf("Buffer is full, waiting...\n");
    pthread_cond_wait(&not_full_cond, &buffer_mutex);
    printf("Done waiting, buffer is not full\n");
  }

  session_counter++;
  int session_id = session_counter;

  // Insert the request into the buffer
  buffer[in] = *request;
  buffer[in].session_id = session_id;  // Assign the session_id
  in = (in + 1) % MAX_SESSION_COUNT;
  count++;

  // Signal that the buffer is not empty
  pthread_cond_signal(&not_empty_cond);

  pthread_mutex_unlock(&buffer_mutex);
  printf("Session id: %d\n", session_id);
  return session_id;  
}

// Function to retrieve a request from the buffer
void retrieve_request(struct Request* request) {
  pthread_mutex_lock(&buffer_mutex);

  // Wait if the buffer is empty
  while (count == 0) {
    printf("Buffer is empty, waiting...\n");
    pthread_cond_wait(&not_empty_cond, &buffer_mutex);
  }

  // Retrieve the request from the buffer
  *request = buffer[out];
  out = (out + 1) % MAX_SESSION_COUNT;
  count--;

  printf("Request retrieved from buffer\n");
  // Print request information
  printf("REQ FROM BUF: Session id: %d\n", request->session_id);
  printf("REQ FROM BUF: Request pipe path: %s\n", request->request_pipe_path);
  printf("REQ FROM BUF: Response pipe path: %s\n", request->response_pipe_path);

  // Signal that the buffer is not full
  pthread_cond_signal(&not_full_cond);

  pthread_mutex_unlock(&buffer_mutex);
}

// Function to handle a client session in a separate thread
void* handle_client(void* args) {
  // Add the session to the buffer and get its server id
  printf("Adding session to buffer...\n");

  struct Request* thread_args = (struct Request*)args;

  printf("Retrieving session id: %d\n", thread_args->session_id);

  printf("Server pipe path: %s\n", thread_args->server_pipe_path);

  // write the session id to the server pipe
  printf("Opening server pipe...\n");
  int server_pipe = open(thread_args->server_pipe_path, O_WRONLY);
  if (server_pipe == -1) {
    perror("Error opening server pipe");
    pthread_exit(NULL);
  }

  printf("Server pipe opened\n");

  write(server_pipe, &thread_args->session_id, sizeof(int));
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
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
  int result;  // result of the operation

  while (read(request_pipe, &op_code, sizeof(char)) > 0 && op_code != 2) {
    printf("Operation code: %d\n", op_code);
    switch (op_code) {
      case 2:  // ems_quit
        // Handle ems_quit
        break;
      case 3:  // ems_create
        // Handle ems_create
        printf("Handling ems_create\n");
        open(thread_args->request_pipe_path, O_RDONLY);
        read(request_pipe, &event_id, sizeof(unsigned int));
        printf("Event id: %d\n", event_id);
        read(request_pipe, &num_rows, sizeof(size_t));
        printf("Num rows: %ld\n", num_rows);
        read(request_pipe, &num_cols, sizeof(size_t));
        printf("Num cols: %ld\n", num_cols);
        printf("Calling ems_create\n");
        result = ems_create(event_id, num_rows, num_cols);
        open(thread_args->response_pipe_path, O_WRONLY);
        write(response_pipe, &result, sizeof(int));
        printf("ems_create done\n");
        break;
      case 4:  // ems_reserve
        // Handle ems_reserve
        printf("Handling ems_reserve\n");
        open(thread_args->request_pipe_path, O_RDONLY);
        read(request_pipe, &event_id, sizeof(unsigned int));
        read(request_pipe, &num_seats, sizeof(size_t));
        read(request_pipe, xs, num_seats * sizeof(size_t));
        read(request_pipe, ys, num_seats * sizeof(size_t));
        result = ems_reserve(event_id, num_seats, xs, ys);
        open(thread_args->response_pipe_path, O_WRONLY);
        write(response_pipe, &result, sizeof(int));
        break;
      case 5:  // ems_show
        // Handle ems_show
        printf("Handling ems_show\n");
        open(thread_args->request_pipe_path, O_RDONLY);
        read(request_pipe, &event_id, sizeof(unsigned int));
        printf("Event id: %d\n", event_id);
        ems_show(response_pipe, event_id);
        break;
      case 6:  // ems_list_events
        // Handle ems_list_events
        printf("Handling ems_list_events\n");
        open(thread_args->request_pipe_path, O_RDONLY);
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

// Worker thread function
void* worker_function() {
  while (1) {
    struct Request current_request;
    printf("Retrieving request...\n");
    // Retrieve a request from the buffer
    retrieve_request(&current_request);
    printf("Request retrieved\n");
    // Print details of request
    printf("Request: %d %s %s\n", current_request.session_id, current_request.request_pipe_path, current_request.response_pipe_path);
    // Execute the handle_client function with the retrieved request
    handle_client(&current_request);
  }
}

// Extract requests function (producer)
void* extract_requests(void *args) {
  struct MainThreadArgs* main_args = (struct MainThreadArgs*)args;
  printf("Server pipe: %d\n", main_args->server_fd);
  printf("Waiting for clients...\n");
  // Loop to populate the sessions array with session IDs and associated pipes
  while (1) {
    char op_code;

    // Lock the rwlock for reading before reading from the server pipe
    pthread_rwlock_rdlock(&server_pipe_rwlock);

    read(main_args->server_fd, &op_code, sizeof(char));
    if (op_code == 1) {
      printf("New session request\n");

      // Obtain the first named pipe for the new session

      char request_pipe_path[MAX_PATH];
      if (read(main_args->server_fd, &request_pipe_path, MAX_PATH) == -1) {
        perror("Error reading from named pipe");
        break;
      }

      printf("Request pipe path: %s\n", request_pipe_path);

      // Obtain the second named pipe for the new session

      char response_pipe_path[MAX_PATH];
      if (read(main_args->server_fd, &response_pipe_path, MAX_PATH) == -1) {
        perror("Error reading from named pipe");
        break;
      }

      printf("Response pipe path: %s\n", response_pipe_path);

      // Remove server/ from the beginning of the path
      memmove(request_pipe_path, request_pipe_path + 7, strlen(request_pipe_path));
      memmove(response_pipe_path, response_pipe_path + 7, strlen(response_pipe_path));

      printf("New request pipe path: %s\n", request_pipe_path);
      printf("New response pipe path: %s\n", response_pipe_path);
      printf("Allocating session ID...\n");

      // Create thread to handle the client function
      printf("Creating thread...\n");

      struct Request request;
      request.session_id = -1;
      snprintf(request.request_pipe_path, MAX_PATH, "%s", request_pipe_path);
      snprintf(request.response_pipe_path, MAX_PATH, "%s", response_pipe_path);
      snprintf(request.server_pipe_path, MAX_PATH, "%s", main_args->server_pipe_path);

      // Insert the request into the requests array
      insert_request(&request);

      // Unlock the rwlock after reading from the server pipe
      pthread_rwlock_unlock(&server_pipe_rwlock);

    }
    if (op_code == 2) {
      printf("Client disconnected\n");
      break;
    }
  }
  return NULL;
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

  struct MainThreadArgs main_args;
  main_args.server_fd = server_fd;
  snprintf(main_args.server_pipe_path, MAX_PATH, "%s", server_pipe_path);
  pthread_t host_thread;
  pthread_create(&host_thread, NULL, extract_requests, (void*)&main_args); 

  // TODO: Intialize server, create worker threads

  pthread_t worker_threads[MAX_SESSIONS];  // Array to store thread IDs

  // Create worker threads for each session
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    if (pthread_create(&worker_threads[i], NULL, worker_function, NULL) != 0) {
      perror("Error creating thread");
      return 1;
    }
  }

  // Wait for all threads to finish before exiting
  for (int i = 0; i < MAX_SESSIONS; ++i) {
    pthread_join(worker_threads[i], NULL);
  }

  // Wait for the host thread to finish
  pthread_join(host_thread, NULL); 

  // TODO: Close Server
  close(server_fd);
  unlink(server_pipe_path);  // Remove the named pipe
  ems_terminate();
}