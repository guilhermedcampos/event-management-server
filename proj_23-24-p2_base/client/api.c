#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define S 10
#define MAX_PATH 40

typedef struct {
  int session_id;
  char req_pipe_path[MAX_PATH];
  char resp_pipe_path[MAX_PATH];
} Session;

Session sessions[S];
int active_sessions = 0;

int ems_setup(char const *req_pipe_path, char const *resp_pipe_path, char const *server_pipe_path) {
  // Create request and response pipes
  mkfifo(req_pipe_path, 0666);
  mkfifo(resp_pipe_path, 0666);

  // Connect to server pipe
  int server_fd = open(server_pipe_path, O_WRONLY);
  if (server_fd < 0) {
    return 1;
  }

  // Send session start request
  char op_code = 1;
  write(server_fd, &op_code, sizeof(char));
  write(server_fd, req_pipe_path, MAX_PATH);
  write(server_fd, resp_pipe_path, MAX_PATH);

  // Store session_id in sessions array
  int resp_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    return 1;
  }
  read(resp_fd, &sessions[active_sessions].session_id, sizeof(int));
  strcpy(sessions[active_sessions].req_pipe_path, req_pipe_path);
  strcpy(sessions[active_sessions].resp_pipe_path, resp_pipe_path);
  active_sessions++;

  close(server_fd);
  close(resp_fd);

  return 0;
}

int ems_quit() {
  // Send session end message to server
  int req_fd = open(sessions[active_sessions - 1].req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }
  char op_code = 2;
  write(req_fd, &op_code, sizeof(char));

  // Close named pipes
  close(req_fd);
  int resp_fd = open(sessions[active_sessions - 1].resp_pipe_path, O_RDONLY);
  if (resp_fd >= 0) {
    close(resp_fd);
  }

  // Delete client named pipe
  unlink(sessions[active_sessions - 1].req_pipe_path);
  unlink(sessions[active_sessions - 1].resp_pipe_path);
  active_sessions--;

  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  // Send create request to server through named pipe
  int req_fd = open(sessions[active_sessions - 1].req_pipe_path, O_WRONLY);
  if (req_fd < 0) {
    return 1;
  }
  char op_code = 3;
  write(req_fd, &op_code, sizeof(char));
  write(req_fd, &event_id, sizeof(unsigned int));
  write(req_fd, &num_rows, sizeof(size_t));
  write(req_fd, &num_cols, sizeof(size_t));

  // Handle server response
  int resp_fd = open(sessions[active_sessions - 1].resp_pipe_path, O_RDONLY);
  if (resp_fd < 0) {
    return 1;
  }
  int result;
  read(resp_fd, &result, sizeof(int));

  close(req_fd);
  close(resp_fd);

  return result;
}

// Similar modifications for ems_reserve, ems_show, and ems_list_events