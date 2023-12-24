#include "api.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int session_id = -1;  // Variable to store the active session ID
static int req_fd, resp_fd, server_pipe;  // Declare these as global variables

/*responsible for setting up the EMS client by creating pipes and connecting to the server. */
int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  // Create request pipe (write-end)
  int req_pipe = mkfifo(req_pipe_path, 0666);
  if (req_pipe == -1) {
    perror("Error creating request pipe");
    return 1;
  }

  // Create response pipe (read-end)
  int resp_pipe = mkfifo(resp_pipe_path, 0666);
  if (resp_pipe == -1) {
    perror("Error creating response pipe");
    return 1;
  }

  // Open server pipe for writing (send requests to the server)
  int server_pipe = open(server_pipe_path, O_WRONLY);
  if (server_pipe == -1) {
    perror("Error opening server pipe for writing");
    return 1;
  }

  // Open request pipe for writing (send requests to the server)
  int req_fd = open(req_pipe_path, O_WRONLY);
  if (req_fd == -1) {
    perror("Error opening request pipe for writing");
    return 1;
  }

  // Open response pipe for reading (receive responses from the server)
  int resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd == -1) {
    perror("Error opening response pipe for reading");
    return 1;
  }

  // Send session initiation request to the server
  if (write(server_pipe, req_pipe_path, sizeof(req_pipe_path)) == -1) {
    perror("Error sending session initiation request to the server");
    return 1;
  }

  if (write(server_pipe, resp_pipe_path, sizeof(resp_pipe_path)) == -1) {
    perror("Error sending session initiation request to the server");
    return 1;
  }

  // Receive session ID from the server
  if (read(resp_fd, &session_id, sizeof(session_id)) == -1) {
    perror("Error receiving session ID from the server");
    return 1;
  }

  return 0;  // Success
}

int ems_quit(void) {
  // Close pipes and reset the active session
  close(req_fd);
  close(resp_fd);
  unlink(req_pipe_path);
  unlink(resp_pipe_path);

  // Send session termination request to the server
  if (write(server_pipe, &session_id, sizeof(session_id)) == -1) {
    perror("Error sending session termination request to the server");
    return 1;
  }

  session_id = -1;  // Reset the active session ID
  return 0;  // Success
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  //TODO: send create request to the server (through the request pipe) and wait for the response (through the response pipe)
  return 1;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  //TODO: send reserve request to the server (through the request pipe) and wait for the response (through the response pipe)
  return 1;
}

int ems_show(int out_fd, unsigned int event_id) {
  //TODO: send show request to the server (through the request pipe) and wait for the response (through the response pipe)
  return 1;
}

int ems_list_events(int out_fd) {
  //TODO: send list request to the server (through the request pipe) and wait for the response (through the response pipe)
  return 1;
}
