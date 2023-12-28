#include <fcntl.h>
#include <pthread.h>
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
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client(void *arg) {
  Session *session = (Session *)arg;

  // Open client request and response pipes
  int req_fd = open(session->req_pipe_path, O_RDONLY);
  int resp_fd = open(session->resp_pipe_path, O_WRONLY);

  // Handle client requests
  char op_code;
  while (read(req_fd, &op_code, sizeof(char)) > 0) {
    switch (op_code) {
      case 1:  // ems_setup
        write(resp_fd, &session->session_id, sizeof(int));
        break;
      case 2:  // ems_quit
        close(req_fd);
        close(resp_fd);
        pthread_mutex_lock(&mutex);
        active_sessions--;
        pthread_mutex_unlock(&mutex);
        pthread_exit(NULL);
        break;
      case 3:  // ems_create
        unsigned int event_id;
        size_t num_rows, num_cols;
        read(req_fd, &event_id, sizeof(unsigned int));
        read(req_fd, &num_rows, sizeof(size_t));
        read(req_fd, &num_cols, sizeof(size_t));
        int result = ems_create(event_id, num_rows, num_cols);
        write(resp_fd, &result, sizeof(int));
        break;
      case 4:  // ems_reserve
        unsigned int event_id;
        size_t num_seats;
        read(req_fd, &event_id, sizeof(unsigned int));
        read(req_fd, &num_seats, sizeof(size_t));
        size_t xs[num_seats], ys[num_seats];
        read(req_fd, xs, num_seats * sizeof(size_t));
        read(req_fd, ys, num_seats * sizeof(size_t));
        int result = ems_reserve(event_id, num_seats, xs, ys);
        write(resp_fd, &result, sizeof(int));
        break;
      case 5:  // ems_show
        unsigned int event_id;
        read(req_fd, &event_id, sizeof(unsigned int));
        size_t num_rows, num_cols;
        unsigned int *seats;
        int result = ems_show(event_id, &num_rows, &num_cols, &seats);
        write(resp_fd, &result, sizeof(int));
        if (result == 0) {
          write(resp_fd, &num_rows, sizeof(size_t));
          write(resp_fd, &num_cols, sizeof(size_t));
          write(resp_fd, seats, num_rows * num_cols * sizeof(unsigned int));
        }
        free(seats);
        break;
      case 6:  // ems_list_events
        size_t num_events;
        unsigned int *event_ids;
        int result = ems_list_events(&num_events, &event_ids);
        write(resp_fd, &result, sizeof(int));
        if (result == 0) {
          write(resp_fd, &num_events, sizeof(size_t));
          write(resp_fd, event_ids, num_events * sizeof(unsigned int));
        }
        free(event_ids);
        break;
        // Handle other operations...
    }
  }

  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s server_pipe_path\n", argv[0]);
    return 1;
  }

  // Create server pipe
  mkfifo(argv[1], 0666);

  // Accept client connections
  int server_fd = open(argv[1], O_RDONLY);
  while (1) {
    char req_pipe_path[MAX_PATH], resp_pipe_path[MAX_PATH];
    if (read(server_fd, req_pipe_path, MAX_PATH) > 0 && read(server_fd, resp_pipe_path, MAX_PATH) > 0) {
      pthread_mutex_lock(&mutex);
      if (active_sessions >= S) {
        pthread_mutex_unlock(&mutex);
        continue;
      }
      sessions[active_sessions].session_id = active_sessions;
      strcpy(sessions[active_sessions].req_pipe_path, req_pipe_path);
      strcpy(sessions[active_sessions].resp_pipe_path, resp_pipe_path);
      active_sessions++;
      pthread_mutex_unlock(&mutex);

      // Start a new thread to handle the client
      pthread_t thread;
      pthread_create(&thread, NULL, handle_client, &sessions[active_sessions - 1]);
    }
  }

  return 0;
}