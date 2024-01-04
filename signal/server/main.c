#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"
#include "eventlist.h"

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

// Mutex to protect the sessions array
pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

// Server pipe file descriptor
int server_fd;

// Struct to store the arguments for the main thread
struct MainThreadArgs {
  char server_pipe_path[MAX_PATH];
};

// int to store the number of active threads
int session_counter = 0;

// int to store the flag to print event info
volatile int print_flag = 0;

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

  if (pthread_mutex_unlock(&buffer_mutex) != 0) {
    perror("Error unlocking mutex");
  }
}

/**
 * Inserts a request into the buffer, allocating a session ID.
 *
 * @param request The pointer to the Request structure containing the request details.
 * @return The session ID assigned to the inserted request.
 */
int insert_request(struct Request* request) {
  // Lock the buffer mutex for thread safety
  if (pthread_mutex_lock(&buffer_mutex) != 0) {
    perror("Error locking mutex");
  }

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
  if (pthread_mutex_unlock(&buffer_mutex) != 0) {
    perror("Error unlocking mutex");
  }

  return session_id;
}

/**
 * Retrieves a request from the buffer.
 *
 * @param request The pointer to the Request structure to store the retrieved request.
 */
void retrieve_request(struct Request* request) {
  if (pthread_mutex_lock(&buffer_mutex) != 0) {
    perror("Error locking mutex");
  }

  // Retrieve the request from the buffer
  *request = buffer[out];               // Copy the request
  out = (out + 1) % MAX_SESSION_COUNT;  // Update the out index
  count--;

  if (pthread_mutex_unlock(&buffer_mutex) != 0) {
    perror("Error unlocking mutex");
  }
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
  if (my_write(response_pipe, &thread_args->session_id, sizeof(int)) == -1) {
    perror("Error writing to named pipe");
    pthread_exit(NULL);
  }

  // Handle client requests
  char op_code;
  unsigned int event_id;
  size_t num_rows, num_cols, num_seats;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
  int result;  // result of the operation

  while (my_read(request_pipe, &op_code, sizeof(char)) > 0 && op_code != 2) {
    //printf("Operation code: %d\n", op_code);
    switch (op_code) {
      case 2:  // ems_quit

        // Read the session ID from the request pipe
        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
        }

        // Close the pipes
        if (close(request_pipe) == -1) {
          perror("Error closing request pipe");
          result = 1;
        }

        if (close(response_pipe) == -1) {
          perror("Error closing response pipe");
          result = 1;
        }

        if (close(server_pipe) == -1) {
          perror("Error closing server pipe");
          result = 1;
        }

        // Remove the session from the buffer
        if (pthread_mutex_lock(&sessions_mutex) != 0) {
          perror("Error locking mutex");
        }
        remove_session(thread_args->session_id);
        if (pthread_mutex_unlock(&sessions_mutex) != 0) {
          perror("Error unlocking mutex");
        }

        // Free the memory allocated for the thread arguments
        free(thread_args);

        printf("Session %d terminated\n", thread_args->session_id);

        // Send thread to worker function to handle the next request
        pthread_exit(NULL);

      case 3:  // ems_create

        //printf("Handling ems_create in session %d\n", thread_args->session_id);

        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        if (my_read(request_pipe, &event_id, sizeof(unsigned int)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        if (my_read(request_pipe, &num_rows, sizeof(size_t)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        if (my_read(request_pipe, &num_cols, sizeof(size_t)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        result = ems_create(event_id, num_rows, num_cols);

        if (my_write(response_pipe, &result, sizeof(int)) == -1) {
          perror("Error writing to named pipe");
          break;
        }
        break;

      case 4:  // ems_reserve

        //printf("Handling ems_reserve in session %d\n", thread_args->session_id);

        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        if (my_read(request_pipe, &event_id, sizeof(unsigned int)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        if (my_read(request_pipe, &num_seats, sizeof(size_t)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        if (my_read(request_pipe, xs, num_seats * sizeof(size_t)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        if (my_read(request_pipe, ys, num_seats * sizeof(size_t)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        result = ems_reserve(event_id, num_seats, xs, ys);

        if (my_write(response_pipe, &result, sizeof(int)) == -1) {
          perror("Error writing to named pipe");
          break;
        }
        break;

      case 5:  // ems_show

        //printf("Handling ems_show in session %d\n", thread_args->session_id);

        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

        if (my_read(request_pipe, &event_id, sizeof(unsigned int)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
            break;
          }
          break;
        }

        // result: (int) success (0 to 1) | (size_t) num_rows | (size_t) num_cols | (unsigned int[num_rows * num_cols])
        // seats
        ems_show(response_pipe, event_id);
        break;

      case 6:  // ems_list_events

        //printf("Handling ems_list_events in session %d\n", thread_args->session_id);

        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          perror("Error reading from named pipe");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            perror("Error writing to named pipe");
          }
          break;
        }

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

  // Block SIGUSR1 in this thread
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, NULL); 

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

int print_events() {
  // Get event_list
    struct EventList* events = get_event_list();
    print_str(STDOUT_FILENO, "ENTROU NO PRINT_EVENTS\n");

    struct ListNode* to = events->tail;
    struct ListNode* current = events->head;

    if (current == NULL) {
      printf("H: No event details to print\n");
      return 1;
    }

    while (1) {
      if (current == to) {
        break;
      }
      print_str(STDOUT_FILENO, "!Event: \n");
      print_uint(STDOUT_FILENO, current->event->id);
      if (ems_show_stdout(current->event->id) == 1) {
        printf("H: Error printing event\n");
        return 1;
      }
      current = current->next;
    }
    return 0;
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
  
    int local_print_flag = print_flag;

    // Check if the print_flag is set
    if (local_print_flag == 1) {
      printf("H: Printing events to standard output:\n");
      print_events();
      // Reset print_flag
      print_flag = 0;
    }

    char op_code;
    // Read the operation code from the server pipe
    if (my_read(server_fd, &op_code, sizeof(char)) == -1) {
      perror("Error reading from named pipe");
      break;
    }

    if (op_code == 1) {
      printf("H: Handling request\n");
      // Obtain the first named pipe for the new session
      char request_pipe_path[MAX_PATH];

      // Read the request pipe path from the server pipe
      if (my_read(server_fd, &request_pipe_path, MAX_PATH) == -1) {
        perror("Error reading from named pipe");
        break;
      }

      // Obtain the second named pipe for the new session
      char response_pipe_path[MAX_PATH];
      if (my_read(server_fd, &response_pipe_path, MAX_PATH) == -1) {
        perror("Error reading from named pipe");
        break;
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

void sigusr1_handler(int signum ) {
  // Set the flag to print event info
  if (signum == SIGUSR1) {
    printf("H: SIGUSR1 received\n");
    print_flag = 1; 
  }
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
  if (pthread_create(&host_thread, NULL, extract_requests, (void*)&main_args) != 0) {
    perror("Error creating thread");
    return 1;
  }

  printf("Host thread created\n");

  // Set the SIGUSR1 signal handler
  signal(SIGUSR1, sigusr1_handler);
  printf("SIGUSR1 handler set\n");

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

  if (close(server_fd) == -1) {
    perror("Error closing server pipe");
    ems_terminate();
    return 1;
  }

  if (unlink(server_pipe_path) == -1) {
    perror("Error unlinking server pipe");
    ems_terminate();
    return 1;
  }
  if (unlink(server_pipe_path) == -1) {
    perror("Error unlinking server pipe");
    ems_terminate();
    return 1;
  }
  ems_terminate();
}