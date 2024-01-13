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
int print_flag = 0;

/**
 * @brief Signal handler for SIGUSR1.
 *
 * This function sets the print_flag to 1 when SIGUSR1 is received.
 *
 * @param signum The signal number.
 */
void sigusr1_handler(int signum ) {
  // Set the flag to print event info
  if (signum == SIGUSR1) {
    print_flag = 1; 
  }
   signal(SIGUSR1, sigusr1_handler);
}

/**
 * @brief Removes a session from the buffer.
 *
 * @param session_id The ID of the session to be removed.
 */
void remove_session(int session_id) {
  // Lock the buffer mutex to protect the buffer and the session counter
  if (pthread_mutex_lock(&buffer_mutex) != 0) {
    print_error("Error locking mutex.\n");
  }

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
  session_counter--;
  count--;

  // Signal that the buffer is not full
  pthread_cond_signal(&not_full_cond);

  if (pthread_mutex_unlock(&buffer_mutex) != 0) {
    print_error("Error unlocking mutex.\n");
  }
}

/**
 * Inserts a request into the buffer, allocating a session ID.
 *
 * @param request The pointer to the Request structure containing the request details.
 * @return The session ID assigned to the inserted request.
 */
void *insert_request(void *req) {
  struct Request* request = (struct Request*)req;

  // Lock the buffer mutex for thread safety
  if (pthread_mutex_lock(&buffer_mutex) != 0) {
    print_error("Error locking mutex.\n");
  }

  // Wait if the buffer is full
  while (count == MAX_SESSION_COUNT) {
    pthread_cond_wait(&not_full_cond, &buffer_mutex);
  }

  int session_id = session_counter;

  // Insert the request into the buffer
  buffer[in] = *request;
  buffer[in].session_id = session_id;  // Assign the session_id
  in = (in + 1) % MAX_SESSION_COUNT;
  count++;
  // Increment the session counter and assign the session ID
  session_counter++;

  // Signal that the buffer is not empty
  pthread_cond_signal(&not_empty_cond);

  // Unlock the buffer mutex
  if (pthread_mutex_unlock(&buffer_mutex) != 0) {
    print_error("Error unlocking mutex.\n");
  }

  return NULL;
}

/**
 * Retrieves a request from the buffer.
 *
 * @param request The pointer to the Request structure to store the retrieved request.
 */
void retrieve_request(struct Request* request) {
  if (pthread_mutex_lock(&buffer_mutex) != 0) {
    print_error("Error locking mutex.\n");
  }

  // Retrieve the request from the buffer
  *request = buffer[out];               // Copy the request
  out = (out + 1) % MAX_SESSION_COUNT;  // Update the out index
  count--;

  if (pthread_mutex_unlock(&buffer_mutex) != 0) {
    print_error("Error unlocking mutex.\n");
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
    print_error("Error opening server pipe.\n");
    free(thread_args);
    pthread_exit(NULL);
  }

  // Open the request pipe for reading
  int request_pipe = open(thread_args->request_pipe_path, O_RDONLY);
  if (request_pipe == -1) {
    print_error("Error opening request pipe.\n");
    free(thread_args);
    pthread_exit(NULL);
  }

  // Open the response pipe for writing
  int response_pipe = open(thread_args->response_pipe_path, O_WRONLY);
  if (response_pipe == -1) {
    print_error("Error opening response pipe.\n");
    free(thread_args);
    pthread_exit(NULL);
  }

  // Send the session id to the response pipe
  if (my_write(response_pipe, &thread_args->session_id, sizeof(int)) == -1) {
    print_error("Error writing to named pipe.\n");
    free(thread_args); 
    pthread_exit(NULL);
  }

  printf("Session %d started.\n", thread_args->session_id);

  // Handle client requests
  char op_code;
  unsigned int event_id;
  size_t num_rows, num_cols, num_seats;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
  int result;  // result of the operation

  while (my_read(request_pipe, &op_code, sizeof(char)) > 0 && op_code != 2) {
    switch (op_code) {
      case 2:  // ems_quit

        // Read the session ID from the request pipe
        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          print_error("Error reading from named pipe.\n");
          result = 1;
        }

        // Close the pipes
        if (close(request_pipe) == -1) {
          print_error("Error closing request pipe.\n");
          result = 1;
        }

        if (close(response_pipe) == -1) {
          print_error("Error closing response pipe.\n");
          result = 1;
        }

        if (close(server_pipe) == -1) {
          print_error("Error closing server pipe.\n");
          result = 1;
        }

        // Remove the session from the buffer
        if (pthread_mutex_lock(&sessions_mutex) != 0) {
          print_error("Error locking mutex.\n");
        }

        remove_session(thread_args->session_id);

        if (pthread_mutex_unlock(&sessions_mutex) != 0) {
          print_error("Error unlocking mutex.\n");
        }

        printf("Session %d terminated.\n", thread_args->session_id);

        // Free the memory allocated for the thread arguments
        free(thread_args);
        // Send thread to worker function to handle the next request
        pthread_exit(NULL);

      case 3:  // ems_create

        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          print_error("2Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        if (my_read(request_pipe, &event_id, sizeof(unsigned int)) == -1) {
          print_error("3Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        if (my_read(request_pipe, &num_rows, sizeof(size_t)) == -1) {
          print_error("4Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        if (my_read(request_pipe, &num_cols, sizeof(size_t)) == -1) {
          print_error("5Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        result = ems_create(event_id, num_rows, num_cols);

        if (my_write(response_pipe, &result, sizeof(int)) == -1) {
          print_error("Error writing to named pipe.\n");
          break;
        }
        break;

      case 4:  // ems_reserve

        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          print_error("6Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        if (my_read(request_pipe, &event_id, sizeof(unsigned int)) == -1) {
          print_error("7Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        if (my_read(request_pipe, &num_seats, sizeof(size_t)) == -1) {
          print_error("8Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        if (my_read(request_pipe, xs, num_seats * sizeof(size_t)) == -1) {
          print_error("9Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        if (my_read(request_pipe, ys, num_seats * sizeof(size_t)) == -1) {
          print_error("10Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        result = ems_reserve(event_id, num_seats, xs, ys);

        if (my_write(response_pipe, &result, sizeof(int)) == -1) {
          print_error("Error writing to named pipe.\n");
          break;
        }
        break;

      case 5:  // ems_show

        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          print_error("11Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        if (my_read(request_pipe, &event_id, sizeof(unsigned int)) == -1) {
          print_error("12Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
            break;
          }
          break;
        }

        ems_show(response_pipe, event_id);
        break;

      case 6:  // ems_list_events

        if (my_read(request_pipe, &thread_args->session_id, sizeof(int)) == -1) {
          print_error("13Error reading from named pipe.\n");
          result = 1;
          if (my_write(response_pipe, &result, sizeof(int)) == -1) {
            print_error("Error writing to named pipe.\n");
          }
          break;
        }

        ems_list_events(response_pipe);
        break;

      default:
        print_error("Unknown operation code.\n");
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

/**
 * Prints the events from the event list.
 *
 * This function retrieves the event list and iterates over it, printing each event.
 * If the event list is empty, it prints an error message.
 *
 * @return Returns 1 if there are no events to print, 0 otherwise.
 */
int print_events() {
  // Get event_list
    struct EventList* events = get_event_list();

    // Get the tail of the list
    struct ListNode* to = events->tail;
    struct ListNode* current = events->head;

    if (current == NULL) {
      print_error("No event details to print.\n");
      return 1;
    }

    while (1) {
      if (current == to) {
        break;
      }
      print_str(STDOUT_FILENO, "Event: ");
      print_uint(STDOUT_FILENO, current->event->id);
      print_str(STDOUT_FILENO, "\n");
      if (ems_show_stdout(current->event->id) == 1) {
        print_error("Error printing event.\n");
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
void extract_requests(void* args) {
  struct MainThreadArgs* main_args = (struct MainThreadArgs*)args;  // Cast the arguments to the correct type
  pthread_t host_thread;

  while (1) {

    char op_code = 0;
    
    // Read the operation code from the server pipe
    ssize_t res = my_read(server_fd, &op_code, sizeof(char));
    if (res == -1) {
      print_error("Error reading from named pipe.\n");
      break;
    }

    // Check if the print_flag is set
    if (print_flag == 1) {
      print_events();
      // Reset print_flag
      print_flag = 0;
      continue;
    }

    if (op_code == 1) {  // ems_setup

      // Obtain the first named pipe for the new session
      char request_pipe_path[MAX_PATH];

      // Read the request pipe path from the server pipe
      if (my_read(server_fd, &request_pipe_path, MAX_PATH) == -1) {
        print_error("15Error reading from named pipe.\n");
        break;
      }

      // Obtain the second named pipe for the new session
      char response_pipe_path[MAX_PATH];
      if (my_read(server_fd, &response_pipe_path, MAX_PATH) == -1) {
        print_error("16Error reading from named pipe.\n");
        break;
      }

      struct Request request;
      request.session_id = -1;
      snprintf(request.request_pipe_path, MAX_PATH, "%s", request_pipe_path);
      snprintf(request.response_pipe_path, MAX_PATH, "%s", response_pipe_path);
      snprintf(request.server_pipe_path, MAX_PATH, "%s", main_args->server_pipe_path);

      // Insert the request into the requests array by creating an auxiliar thread
      if (pthread_create(&host_thread, NULL, insert_request, (void*)&request) != 0) {
        print_error("Error creating thread.\n");
        break;
      }

      if (pthread_join(host_thread, NULL) != 0) {
        print_error("Error joining thread.\n");
        break;
      }
    }
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
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay].\n", argv[0]);
    return 1;
  }
  
  // Set the state access delay if provided
  char* endptr;
  unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      print_error("Invalid delay value or value too large.\n");
      return 1;
    }

    state_access_delay_us = (unsigned int)delay;
  }

  // Initialize the EMS
  if (ems_init(state_access_delay_us)) {
    print_error("Failed to initialize EMS.\n");
    return 1;
  }

  char* server_pipe_path = argv[1];
  // Create a named pipe for reading and writing
  if (mkfifo(server_pipe_path, 0666) == -1) {
    print_error("Error creating named pipe.\n");
    ems_terminate();
    return 1;
  }

  signal(SIGUSR1, sigusr1_handler);

  // Open the pipe for reading and writing
  server_fd = open(server_pipe_path, O_RDWR);
  if (server_fd == -1) {
    print_error("Error opening server pipe.\n");
    ems_terminate();
    return 1;
  }

  // Create the main thread arguments
  struct MainThreadArgs main_args;

  // Copy the server pipe path to the main thread arguments
  snprintf(main_args.server_pipe_path, MAX_PATH, "%s", server_pipe_path);

  // Create worker threads
  pthread_t worker_threads[MAX_SESSION_COUNT];

  for (int i = 0; i < MAX_SESSION_COUNT; ++i) {
    if (pthread_create(&worker_threads[i], NULL, worker_function, NULL) != 0) {
      print_error("Error creating thread.\n");
      return 1;
    }
  }

  extract_requests(&main_args); 

  // Wait for all threads to finish before exiting
  for (int i = 0; i < MAX_SESSION_COUNT; ++i) {
    pthread_join(worker_threads[i], NULL);
  }

  if (close(server_fd) == -1) {
    print_error("Error closing server pipe.\n");
    ems_terminate();
    return 1;
  }

  if (unlink(server_pipe_path) == -1) {
    print_error("Error unlinking server pipe.\n");
    ems_terminate();
    return 1;
  }
  if (unlink(server_pipe_path) == -1) {
    print_error("Error unlinking server pipe.\n");
    ems_terminate();
    return 1;
  }
  ems_terminate();
}