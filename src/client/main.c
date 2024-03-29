#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "api.h"
#include "common/constants.h"
#include "common/io.h"
#include "parser.h"

/**
 * The main function for the EMS client program.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return 0 if the program executed successfully, 1 otherwise.
 */
int main(int argc, char* argv[]) {
  // Check if the required number of command-line arguments is provided
  if (argc < 5) {
    fprintf(stderr, "Usage: %s <request pipe path> <response pipe path> <server pipe path> <.jobs file path>\n",
            argv[0]);
    return 1;
  }

  // Set up communication with the EMS server
  if (ems_setup(argv[1], argv[2], argv[3])) {
    print_error("Failed to set up EMS\n");
    return 1;
  }

  // Validate the provided .jobs file path
  const char* dot = strrchr(argv[4], '.');
  if (dot == NULL || dot == argv[4] || strlen(dot) != 5 || strcmp(dot, ".jobs") ||
      strlen(argv[4]) > MAX_JOB_FILE_NAME_SIZE) {
    fprintf(stderr, "The provided .jobs file path is not valid. Path: %s\n", argv[1]);
    return 1;
  }

  // Create an output file path by replacing the extension with .out
  char out_path[MAX_JOB_FILE_NAME_SIZE];
  strcpy(out_path, argv[4]);
  strcpy(strrchr(out_path, '.'), ".out");

  // Open input file
  int in_fd = open(argv[4], O_RDONLY);
  if (in_fd == -1) {
    fprintf(stderr, "Failed to open input file. Path: %s\n", argv[4]);
    return 1;
  }

  // Open output file;
  int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (out_fd == -1) {
    fprintf(stderr, "Failed to open output file. Path: %s\n", out_path);
    return 1;
  }

  // Main command processing loop
  while (1) {
    unsigned int event_id;
    size_t num_rows, num_columns, num_coords;
    unsigned int delay = 0;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    switch (get_next(in_fd)) {
      case CMD_CREATE:
        if (parse_create(in_fd, &event_id, &num_rows, &num_columns) != 0) {
          print_error("Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_create(event_id, num_rows, num_columns)) print_error("Failed to create event\n");
        break;
      case CMD_RESERVE:
        num_coords = parse_reserve(in_fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

        if (num_coords == 0) {
          print_error("Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_reserve(event_id, num_coords, xs, ys)) print_error("Failed to reserve seats\n");
        break;

      case CMD_SHOW:
        if (parse_show(in_fd, &event_id) != 0) {
          print_error("Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_show(out_fd, event_id)) print_error("Failed to show event\n");
        break;

      case CMD_LIST_EVENTS:
        if (ems_list_events(out_fd)) print_error("Failed to list events\n");
        break;

      case CMD_WAIT:
        if (parse_wait(in_fd, &delay, NULL) == -1) {
          print_error("Invalid command. See HELP for usage\n");
          continue;
        }

        if (delay > 0) {
          printf("Waiting...\n");
          sleep(delay);
        }
        break;

      case CMD_INVALID:
        print_error("Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP:
        printf(
            "Available commands:\n"
            "  CREATE <event_id> <num_rows> <num_columns>\n"
            "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
            "  SHOW <event_id>\n"
            "  LIST\n"
            "  WAIT <delay_ms>\n"
            "  HELP\n");

        break;

      case CMD_EMPTY:
        break;

      case EOC:
        if (close(in_fd) == -1) {
          fprintf(stderr, "Failed to close input file. Path: %s\n", argv[4]);
          return 1;
        }
        if (close(out_fd) == -1) {
          fprintf(stderr, "Failed to close output file. Path: %s\n", out_path);
          return 1;
        }
        if (ems_quit()) {
          print_error("Failed to quit EMS\n");
          return 1;
        }
        return 0;
    }
  }
}