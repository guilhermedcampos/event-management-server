#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/constants.h"

/**
 * Represents a session in the Event Management System (EMS), storing the
 * session ID and paths to the named pipes for requests and responses.
 */
typedef struct {
  int session_id;                 // The unique identifier for the session.
  char req_pipe_path[MAX_PATH];   // The path to the named pipe for requests.
  char resp_pipe_path[MAX_PATH];  // The path to the named pipe for responses.
} Session;

// Global variable to store session information
Session session;

/**
 * Set up a connection to the Event Management System (EMS) server by creating
 * named pipes for communication and sending a session start request.
 *
 * @param req_pipe_p   The path to the request pipe.
 * @param resp_pipe_p  The path to the response pipe.
 * @param server_pipe_p The path to the server pipe.
 * @return             0 on success, 1 on failure.
 */
int ems_setup(char const *req_pipe_p, char const *resp_pipe_p, char const *server_pipe_p) {
  // Create buffer for pipe path with size MAX_PATH
  char resp_pipe_path[MAX_PATH];
  char req_pipe_path[MAX_PATH];
  char server_pipe_path[MAX_PATH];

  // Fill buffer with null bytes
  if (memset(server_pipe_path, '\0', MAX_PATH) == NULL) {
    printf("Failed to memset server_pipe_path.\n");
    return 1;
  }

  if (memset(resp_pipe_path, '\0', MAX_PATH) == NULL) {
    printf("Failed to memset resp_pipe_path.\n");
    return 1;
  }

  if (memset(req_pipe_path, '\0', MAX_PATH) == NULL) {
    printf("Failed to memset req_pipe_path.\n");
    return 1;
  }

  // Copy the pipe path to the buffer
  strcpy(server_pipe_path, server_pipe_p);
  strcpy(resp_pipe_path, resp_pipe_p);
  strcpy(req_pipe_path, req_pipe_p);

  // Create request and response pipes with permissions read and write
  mkfifo(resp_pipe_path, 0666);
  mkfifo(req_pipe_path, 0666);

  printf("Server pipe path: %s\n", server_pipe_path);

  // Connect to server pipe
  int server_fd = open(server_pipe_path, O_WRONLY);
  if (server_fd < 0) {
    printf("Failed to connect to server pipe.\n");
    return 1;
  }

  // Send session start request to server
  char op_code = 1;  // op_code for session start

  if (write(server_fd, &op_code, sizeof(char)) < 0) {
    printf("Failed to write op_code.\n");
    return 1;
  }
  if (write(server_fd, req_pipe_path, MAX_PATH) < 0) {
    printf("Failed to write req_pipe_path.\n");
    return 1;
  }
  if (write(server_fd, resp_pipe_path, MAX_PATH) < 0) {
    printf("Failed to write resp_pipe_path.\n");
    return 1;
  }

  int req_fd = open(req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    printf("Failed to open request pipe.\n");
    return 1;
  }

  int resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    printf("Failed to open response pipe.\n");
    return 1;
  }

  // Read session_id from server
  read(resp_fd, &session.session_id, sizeof(int));

  printf("Session ID: %d\n", session.session_id);

  // Copy named pipe paths to session struct
  strcpy(session.req_pipe_path, req_pipe_path);
  strcpy(session.resp_pipe_path, resp_pipe_path);

  // Close named pipes
  close(server_fd);
  close(resp_fd);

  return 0;
}

/**
 * Sends a session end message to the Event Management System (EMS) server,
 * closes named pipes, and deletes client named pipes to terminate
 * the connection with the server.
 *
 * @return 0 on success, 1 on failure.
 */
int ems_quit() {
  // Open request pipe
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }

  // Send session end request to server
  char op_code = 2;  // op_code for session end

  if (write(req_fd, &op_code, sizeof(char)) < 0) {
    printf("Failed to write op_code.\n");
    return 1;
  }

  if (write(req_fd, &session.session_id, sizeof(int)) < 0) {
    printf("Failed to write session_id.\n");
    return 1;
  }

  // Close named pipes
  close(req_fd);

  // Open response pipe
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd >= 0) {
    close(resp_fd);
  }

  // Delete client named pipes
  unlink(session.req_pipe_path);
  unlink(session.resp_pipe_path);

  return 0;
}

/**
 * Sends a create request to the Event Management System (EMS) server through
 * named pipes, providing information about the event to be created.
 *
 * @param event_id   The unique identifier for the event.
 * @param num_rows   The number of rows in the event.
 * @param num_cols   The number of columns in the event.
 * @return           0 on success, 1 on failure.
 */
int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  // Open request and response pipes
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }

  // Send create request to server and event information
  char op_code = 3;  // op_code for create

  if (write(req_fd, &op_code, sizeof(char)) < 0) {
    printf("Failed to write op_code.\n");
    return 1;
  }

  if (write(req_fd, &session.session_id, sizeof(int)) < 0) {
    printf("Failed to write session_id.\n");
    return 1;
  }

  if (write(req_fd, &event_id, sizeof(unsigned int)) < 0) {
    printf("Failed to write event_id.\n");
    return 1;
  }

  if (write(req_fd, &num_rows, sizeof(size_t)) < 0) {
    printf("Failed to write num_rows.\n");
    return 1;
  }

  if (write(req_fd, &num_cols, sizeof(size_t)) < 0) {
    printf("Failed to write num_cols.\n");
    return 1;
  }

  // Handle server response
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    return 1;
  }

  int result;

  if (read(resp_fd, &result, sizeof(int)) < 0) {
    printf("Failed to read result.\n");
    return 1;
  }

  if (result == 1) {
    perror("Server couldn't create.");
    return 1;
  }

  // Close named pipes
  close(req_fd);
  close(resp_fd);

  return result;
}

/**
 * Sends a reserve request to the Event Management System (EMS) server through
 * named pipes, providing information about the seats to be reserved.
 *
 * @param event_id   The unique identifier for the event.
 * @param num_seats  The number of seats to be reserved.
 * @param xs         An array of X coordinates for the reserved seats.
 * @param ys         An array of Y coordinates for the reserved seats.
 * @return           0 on success, 1 on failure.
 */
int ems_reserve(unsigned int event_id, size_t num_seats, size_t *xs, size_t *ys) {
  // Send reserve request to server through named pipe
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }

  // Send reserve request to server and seat information
  char op_code = 4;
  if (write(req_fd, &op_code, sizeof(char)) < 0) {
    printf("Failed to write op_code.\n");
    return 1;
  }

  if (write(req_fd, &session.session_id, sizeof(int)) < 0) {
    printf("Failed to write session_id.\n");
    return 1;
  }

  if (write(req_fd, &event_id, sizeof(unsigned int)) < 0) {
    printf("Failed to write event_id.\n");
    return 1;
  }

  if (write(req_fd, &num_seats, sizeof(size_t)) < 0) {
    printf("Failed to write num_seats.\n");
    return 1;
  }

  if (write(req_fd, xs, num_seats * sizeof(size_t)) < 0) {
    printf("Failed to write xs.\n");
    return 1;
  }

  if (write(req_fd, ys, num_seats * sizeof(size_t)) < 0) {
    printf("Failed to write ys.\n");
    return 1;
  }

  // Handle server response
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    return 1;
  }

  int result;

  if (read(resp_fd, &result, sizeof(int)) < 0) {
    printf("Failed to read result.\n");
    return 1;
  }

  if (result == 1) {
    perror("Server couldn't reserve.");
    return 1;
  }

  // Close named pipes
  close(req_fd);
  close(resp_fd);

  return result;
}

/**
 * Sends a show request to the Event Management System (EMS) server through
 * named pipes, requesting information about a specific event, and writes the
 * seat layout to the specified output file descriptor.
 *
 * @param out_fd     The file descriptor for the output where the seat layout
 *                   information will be written.
 * @param event_id   The unique identifier for the event to show.
 * @return           0 on success, 1 on failure.
 */
int ems_show(int out_fd, int event_id) {
  // Send show request to server through named pipe
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }

  char op_code = 5;  // op_code for show

  if (write(req_fd, &op_code, sizeof(char)) < 0) {
    printf("Failed to write op_code.\n");
    return 1;
  }

  if (write(req_fd, &session.session_id, sizeof(int)) < 0) {
    printf("Failed to write session_id.\n");
    return 1;
  }

  if (write(req_fd, &event_id, sizeof(unsigned int)) < 0) {
    printf("Failed to write event_id.\n");
    return 1;
  }

  // Handle server response
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    return 1;
  }

  int result;

  if (read(resp_fd, &result, sizeof(int)) < 0) {
    printf("Failed to read result.\n");
    return 1;
  }

  if (result == 1) {
    perror("Server couldn't show.");
    return 1;
  }
  // Read seat layout from server and write it to out_fd
  size_t num_rows;
  size_t num_cols;

  if (read(resp_fd, &num_rows, sizeof(size_t)) < 0) {
    printf("Failed to read num_rows.\n");
    return 1;
  }

  if (read(resp_fd, &num_cols, sizeof(size_t)) < 0) {
    printf("Failed to read num_cols.\n");
    return 1;
  }

  for (size_t i = 0; i < num_rows; i++) {
    for (size_t j = 0; j < num_cols; j++) {
      unsigned int seat;
      if (read(resp_fd, &seat, sizeof(unsigned int)) < 0) {
        printf("Failed to read seat.\n");
        return 1;
      }
      char seat_str[64];
      snprintf(seat_str, 64, "%u ", seat);
      if (write(out_fd, seat_str, strlen(seat_str)) < 0) {
        printf("Failed to write seat_str.\n");
        return 1;
      }
    }

    // Add a newline after each row
    char newline = '\n';
    if (write(out_fd, &newline, 1) < 0) {
      printf("Failed to write newline.\n");
      return 1;
    }
  }

  // Close named pipes
  close(req_fd);
  close(resp_fd);

  return result;
}

/**
 * Sends a request to the Event Management System (EMS) server to list available
 * events through named pipes and writes the result to the specified output file descriptor.
 *
 * @param out_fd     The file descriptor for the output where the list of events
 *                   information will be written.
 * @return           0 on success, 1 on failure.
 */
int ems_list_events(int out_fd) {
  // Open request pipe
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    printf("Failed to open request pipe.\n");
    return 1;
  }

  // Send list events request to server
  char op_code = 6;  // op_code for list events

  if (write(req_fd, &op_code, sizeof(char)) < 0) {
    printf("Failed to write op_code.\n");
    return 1;
  }

  if (write(req_fd, &session.session_id, sizeof(int)) < 0) {
    printf("Failed to write session_id.\n");
    return 1;
  }

  // Handle server response
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    printf("Failed to open response pipe.\n");
    return 1;
  }

  int result;

  if (read(resp_fd, &result, sizeof(int)) < 0) {
    printf("Failed to read result.\n");
    return 1;
  }

  if (result == 1) {
    perror("Server couldn't list events.");
    return 1;
  }

  if (result == 2) {
    if (write(out_fd, "No events\n", strlen("No events\n")) < 0) {
      printf("Failed to write no events.\n");
      return 1;
    }
    return 1;
  }

  // Write "Events:" to out_fd
  if (write(out_fd, "Events:", strlen("Events:")) < 0) {
    printf("Failed to write events.\n");
    return 1;
  };
  // Read events from server and write them to out_fd
  if (result == 0) {
    size_t num_events;
    if (read(resp_fd, &num_events, sizeof(size_t)) < 0) {
      printf("Failed to read num_events.\n");
      return 1;
    }
    for (size_t i = 0; i < num_events; i++) {
      unsigned int event_id;
      if (read(resp_fd, &event_id, sizeof(unsigned int)) < 0) {
        printf("Failed to read event_id.\n");
        return 1;
      }
      char id_str[64];
      snprintf(id_str, 64, " %u", event_id);
      if (write(out_fd, id_str, strlen(id_str)) < 0) {
        printf("Failed to write id_str.\n");
        return 1;
      }
    }

    // Add a newline after listing all events
    char newline = '\n';
    if (write(out_fd, &newline, 1) < 0) {
      printf("Failed to write newline.\n");
      return 1;
    }
  }

  // Close named pipes
  close(req_fd);
  close(resp_fd);

  return result;
}
