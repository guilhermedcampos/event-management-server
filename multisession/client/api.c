#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/constants.h"

#define S 10

typedef struct {
  int session_id;
  char req_pipe_path[MAX_PATH];
  char resp_pipe_path[MAX_PATH];
} Session;

Session session;

int ems_setup(char const *req_pipe_path, char const *resp_pipe_path, char const *server_pipe_path) {
  // Create request and response pipes
  mkfifo(resp_pipe_path, 0666);
  mkfifo(req_pipe_path, 0666);

  // Connect to server pipe
  int server_fd = open(server_pipe_path, O_WRONLY);
  if (server_fd < 0) {
    printf("Failed to connect to server pipe.\n");
    return 1;
  }

  // Send session start request
  char op_code = 1;  // Character OP_CODE for session setup
  write(server_fd, &op_code, sizeof(char));
  write(server_fd, req_pipe_path, MAX_PATH);
  write(server_fd, resp_pipe_path, MAX_PATH);

  int req_fd = open(req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    printf("Failed to open request pipe.\n");
    return 1;
  }

  // Store session_id in session variable
  int resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    printf("Failed to open response pipe.\n");
    return 1;
  }

  read(resp_fd, &session.session_id, sizeof(int));
  printf("Session ID: %d\n", session.session_id);

  strcpy(session.req_pipe_path, req_pipe_path);
  strcpy(session.resp_pipe_path, resp_pipe_path);

  close(server_fd);
  close(resp_fd);

  return 0;
}

int ems_quit() {
  // Send session end message to server
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }
  char op_code = 2;  // Character OP_CODE for session end
  write(req_fd, &op_code, sizeof(char));

  // Close named pipes
  close(req_fd);
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd >= 0) {
    close(resp_fd);
  }

  // Delete client named pipes
  unlink(session.req_pipe_path);
  unlink(session.resp_pipe_path);

  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  // Send create request to server through named pipe
  printf("Sending create request to server through named pipe.\n");
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }

  // Send create request
  char op_code = 3;
  printf("Sending op_code: %d\n", op_code);
  write(req_fd, &op_code, sizeof(char));
  printf("Sending event_id: %d\n", event_id);
  write(req_fd, &event_id, sizeof(unsigned int));
  printf("Sending num_rows: %ld\n", num_rows);
  write(req_fd, &num_rows, sizeof(size_t));
  printf("Sending num_cols: %ld\n", num_cols);
  write(req_fd, &num_cols, sizeof(size_t));

  // Handle server response
  printf("Handling server response.\n");
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    return 1;
  }
  int result;
  read(resp_fd, &result, sizeof(int));

  if (result == 1) {
    perror("Server couldnt create.");
  }

  close(req_fd);
  close(resp_fd);

  return result;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t *xs, size_t *ys) {
  // Send reserve request to server through named pipe
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }

  char op_code = 4;
  write(req_fd, &op_code, sizeof(char));
  write(req_fd, &event_id, sizeof(unsigned int));
  write(req_fd, &num_seats, sizeof(size_t));
  write(req_fd, xs, num_seats * sizeof(size_t));
  write(req_fd, ys, num_seats * sizeof(size_t));

  // Handle server response
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    return 1;
  }
  int result;
  read(resp_fd, &result, sizeof(int));

  if (result == 1) {
    perror("Server couldn't reserve.");
  }

  close(req_fd);
  close(resp_fd);

  return result;
}

int ems_show(int out_fd, int event_id) {
  // Send show request to server through named pipe
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }

  char op_code = 5;
  write(req_fd, &op_code, sizeof(char));
  write(req_fd, &event_id, sizeof(unsigned int));

  // Handle server response
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    return 1;
  }
  int result;

  read(resp_fd, &result, sizeof(int));

  if (result == 1) {
    perror("Server couldn't show.");
  }

  size_t num_rows;
  size_t num_cols;

  read(resp_fd, &num_rows, sizeof(size_t));

  printf("num_rows: %ld\n", num_rows);

  read(resp_fd, &num_cols, sizeof(size_t));

  printf("num_cols: %ld\n", num_cols);

  for (size_t i = 0; i < num_rows; i++) {
    for (size_t j = 0; j < num_cols; j++) {
      unsigned int seat;
      read(resp_fd, &seat, sizeof(unsigned int));
      char seat_str[64];
      snprintf(seat_str, 64, "%u ", seat);
      write(out_fd, seat_str, strlen(seat_str));
    }
    // Add a newline after each row
    char newline = '\n';
    write(out_fd, &newline, 1);
  }

  close(req_fd);
  close(resp_fd);

  return result;
}

int ems_list_events(int out_fd) {
  printf("Sending list events request to server.\n");
  // Send list events request to server through named pipe
  int req_fd = open(session.req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    printf("Failed to open request pipe.\n");
    return 1;
  }
  char op_code = 6;
  write(req_fd, &op_code, sizeof(char));

  printf("Sending list events request.\n");
  // Handle server response
  int resp_fd = open(session.resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    printf("Failed to open response pipe.\n");
    return 1;
  }

  int result;
  read(resp_fd, &result, sizeof(int));

  printf("result: %d\n", result);

  if (result == 1) {
    perror("Server couldn't list events.");
    return 1;
  }

  write(out_fd, "Events:", strlen("Events:"));

  printf("Reading events from server.\n");
  // Read events from server and write them to out_fd
  if (result == 0) {
    size_t num_events;
    read(resp_fd, &num_events, sizeof(size_t));
    for (size_t i = 0; i < num_events; i++) {
      unsigned int event_id;
      read(resp_fd, &event_id, sizeof(unsigned int));
      char id_str[64];
      snprintf(id_str, 64, " %u", event_id);  // Fix here
      write(out_fd, id_str, strlen(id_str));
    }
    // Add a newline after listing all events
    char newline = '\n';
    write(out_fd, &newline, 1);
  }

  close(req_fd);
  close(resp_fd);

  return result;
}