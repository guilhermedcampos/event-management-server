#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/io.h"
#include "eventlist.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_us = 0;

/**
 * Gets the event with the given ID from the state.
 *
 * @note Will wait to simulate a real system accessing a costly memory resource.
 * @param event_id The ID of the event to get.
 * @param from First node to be searched.
 * @param to Last node to be searched.
 * @return Pointer to the event if found, NULL otherwise.
 */
static struct Event* get_event_with_delay(unsigned int event_id, struct ListNode* from, struct ListNode* to) {
  struct timespec delay = {0, state_access_delay_us * 1000};
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id, from, to);
}

/**
 * Gets the index of a seat.
 *
 * @note This function assumes that the seat exists.
 * @param event Event to get the seat index from.
 * @param row Row of the seat.
 * @param col Column of the seat.
 * @return Index of the seat.
 */
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

/**
 * @brief Initializes the Event Management System (EMS) state.
 *
 * This function must be called before using any other EMS functions.
 *
 * @param delay_us The delay in microseconds to simulate access to a costly memory resource.
 * @return 0 on success, 1 on failure.
 */
int ems_init(unsigned int delay_us) {
  if (event_list != NULL) {
    print_error("EMS state has already been initialized.\n");
    return 1;
  }

  event_list = create_list();

  state_access_delay_us = delay_us;

  return event_list == NULL;
}

/**
 * Terminates the Event Management System (EMS) state.
 *
 * This function should be called when the EMS is no longer needed.
 *
 * @return 0 on success, 1 on failure.
 */
int ems_terminate() {
  if (event_list == NULL) {
    print_error("EMS state must be initialized.\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    print_error("Error locking list rwl.\n");
    return 1;
  }

  free_list(event_list);

  if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
    print_error("Error unlocking list rwl.\n");
    return 1;
  }
  return 0;
}

struct EventList* get_event_list() { return event_list; }

/**
 * Creates a new event with the specified ID, number of rows, and number of columns.
 *
 * @param event_id The ID of the new event.
 * @param num_rows The number of rows in the event.
 * @param num_cols The number of columns in the event.
 * @return 0 on success, 1 on failure.
 */
int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  if (event_list == NULL) {
    print_error("EMS state must be initialized.\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    print_error("Error locking list rwl.\n");
    return 1;
  }

  if (get_event_with_delay(event_id, event_list->head, event_list->tail) != NULL) {
    print_error("Event already exists\n");
    if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
      print_error("Error unlocking list rwl.\n");
    }
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    print_error("Error allocating memory for event.\n");
    if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
      print_error( "Error unlocking list rwl.\n");
    }
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;

  if (pthread_mutex_init(&event->mutex, NULL) != 0) {
    if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
      print_error( "Error unlocking list rwl.\n");
    }
    free(event);
    return 1;
  }
  event->data = calloc(num_rows * num_cols, sizeof(unsigned int));
  if (event->data == NULL) {
    print_error( "Error allocating memory for event data.\n");
    if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
      print_error( "Error unlocking list rwl.\n");
    }
    free(event);
    return 1;
  }

  if (append_to_list(event_list, event) != 0) {
    print_error( "Error appending event to list.\n");
    if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
      print_error( "Error unlocking list rwl.\n");
    }
    free(event->data);
    free(event);
    return 1;
  }

  if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
    print_error( "Error unlocking list rwl.\n");
  }
  return 0;
}

/**
 * Reserves seats for a specified event.
 *
 * @param event_id The ID of the event to reserve seats for.
 * @param num_seats The number of seats to reserve.
 * @param xs An array containing the row indices of the seats.
 * @param ys An array containing the column indices of the seats.
 * @return 0 on success, 1 on failure.
 */
int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  if (event_list == NULL) {
    print_error( "EMS state must be initialized.\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    print_error( "Error locking list rwl.\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
    print_error( "Error unlocking list rwl.\n");
    return 1;
  }

  if (event == NULL) {
    print_error( "Event not found.\n");
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    print_error("Error locking mutex.\n");
    return 1;
  }

  for (size_t i = 0; i < num_seats; i++) {
    if (xs[i] <= 0 || xs[i] > event->rows || ys[i] <= 0 || ys[i] > event->cols) {
      print_error("Seat out of bounds\n");
      if (pthread_mutex_unlock(&event->mutex) != 0) {
        print_error("Error unlocking mutex.\n");
      }

      return 1;
    }
  }

  for (size_t i = 0; i < event->rows * event->cols; i++) {
    for (size_t j = 0; j < num_seats; j++) {
      if (seat_index(event, xs[j], ys[j]) != i) {
        continue;
      }

      if (event->data[i] != 0) {
        print_error("Seat already reserved.\n");
        if (pthread_mutex_unlock(&event->mutex) != 0) {
          print_error("Error unlocking mutex.\n");
        }
        return 1;
      }

      break;
    }
  }

  unsigned int reservation_id = ++event->reservations;

  for (size_t i = 0; i < num_seats; i++) {
    event->data[seat_index(event, xs[i], ys[i])] = reservation_id;
  }

  if (pthread_mutex_unlock(&event->mutex) != 0) {
    print_error("Error unlocking mutex.\n");
  }
  return 0;
}

/**
 * Sends information about a specified event to the client through a given file descriptor.
 *
 * @param response_fd The file descriptor to send the information to.
 * @param event_id The ID of the event to get information about.
 * @return 0 on success, 1 on failure.
 */
int ems_show(int response_fd, unsigned int event_id) {
  // result: (int) success (0 to 1) | (size_t) num_rows | (size_t) num_cols | (unsigned int[num_rows * num_cols]) seats
  int result = 1;

  if (event_list == NULL) {
    print_error("EMS state must be initialized.\n");
    if (my_write(response_fd, &result, sizeof(int)) == -1) {
      print_error("Error writing to fd.\n");
    }
    return 1;
  }


  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    if (my_write(response_fd, &result, sizeof(int)) == -1) {
      print_error("Error writing to fd.\n");
    }
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
    print_error("Error unlocking list rwl.\n");
  }

  if (event == NULL) {
    print_error("Event not found.\n");
    if (my_write(response_fd, &result, sizeof(int)) == -1) {
      print_error("Error writing to fd.\n");
    }
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    print_error("Error locking mutex.\n");
    if (my_write(response_fd, &result, sizeof(int)) == -1) {
      print_error("Error writing to fd.\n");
    }
    return 1;
  }

  // No more possible errors, write success code
  result = 0;

  // Write the result, rows, and cols to the buffer
  if (my_write(response_fd, &result, sizeof(int)) == -1) {
    print_error("Error writing to fd.\n");
    if (pthread_mutex_unlock(&event->mutex) != 0) {
      print_error("Error unlocking mutex.\n");
    }
    return 1;
  }
  if (my_write(response_fd, &event->rows, sizeof(size_t)) == -1) {
    print_error("Error writing to fd.\n");
    if (pthread_mutex_unlock(&event->mutex) != 0) {
      print_error("Error unlocking mutex.\n");
    }
    return 1;
  }
  if (my_write(response_fd, &event->cols, sizeof(size_t)) == -1) {
    print_error("Error writing to fd.\n");
    if (pthread_mutex_unlock(&event->mutex) != 0) {
      print_error("Error unlocking mutex.\n");
    }
    return 1;
  }

  // Write the seat data to the buffer
  for (size_t i = 0; i < event->rows * event->cols; i++) {
    if (my_write(response_fd, &event->data[i], sizeof(unsigned int)) == -1) {
      print_error("Error writing to fd.\n");
      if (pthread_mutex_unlock(&event->mutex) != 0) {
        print_error("Error unlocking mutex.\n");
      }
      return 1;
    }
  }

  if (pthread_mutex_unlock(&event->mutex) != 0) {
    print_error("Error unlocking mutex.\n");
  }
  return 0;
}

/**
 * Sends information about a specified event to the standard output.
 *
 * @param event_id The ID of the event to get information about.
 * @return 0 on success, 1 on failure.
 */
int ems_show_stdout(unsigned int event_id) {

  if (event_list == NULL) {
    print_error("EMS state must be initialized.\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    print_error("Error locking list rwl.\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
    print_error("Error unlocking list rwl.\n");
  }

  if (event == NULL) {
    print_error("Event not found.\n");
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    print_error("Error locking mutex.\n");
    return 1;
  }

  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      char buffer[16];
      sprintf(buffer, "%u", event->data[seat_index(event, i, j)]);

      if (print_str(STDOUT_FILENO, buffer)) {
        print_error("Error writing to file descriptor.\n");
        pthread_mutex_unlock(&event->mutex);
        return 1;
      }

      if (j < event->cols) {
        if (print_str(STDOUT_FILENO, " ")) {
          print_error("Error writing to file descriptor.\n");
          pthread_mutex_unlock(&event->mutex);
          return 1;
        }
      }
    }

    if (print_str(STDOUT_FILENO, "\n")) {
      print_error("Error writing to file descriptor.\n");
      pthread_mutex_unlock(&event->mutex);
      return 1;
    }
  }

  pthread_mutex_unlock(&event->mutex);
  return 0;
}

/**
 * Lists all events and their IDs, sending the information to the specified file descriptor.
 *
 * @param out_fd The file descriptor to send the information to.
 * @return 0 on success, 1 on failure.
 */
int ems_list_events(int out_fd) {
  int result = 1;
  if (event_list == NULL) {
    print_error("EMS state must be initialized.\n");

    if (my_write(out_fd, &result, sizeof(int)) == -1) {
      print_error("Error writing to fd.\n");
    }
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    print_error("Error locking list rwl.\n");

    if (my_write(out_fd, &result, sizeof(int)) == -1) {
      print_error("Error writing to fd.\n");
    }

    return 1;
  }

  struct ListNode* to = event_list->tail;
  struct ListNode* current = event_list->head;

  result = 2;

  if (current == NULL) {
    my_write(out_fd, &result, sizeof(int));
    if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
      print_error("Error unlocking list rwl.\n");
    }
    return 1;
  }

  result = 0;
  // If there are events, write 0 followed by the number of events followed by the event ids
  if (my_write(out_fd, &result, sizeof(int)) == -1) {
    print_error("Error writing to fd.\n");
    if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
      print_error("Error unlocking list rwl.\n");
    }
    return 1;
  }
  size_t num_events = 0;

  // Iterate through the list and count the number of events
  while (1) {
    num_events++;
    if (current == to) {
      break;
    }

    current = current->next;
  }

  if (my_write(out_fd, &num_events, sizeof(size_t)) == -1) {
    print_error("Error writing to fd.\n");
    if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
      print_error("Error unlocking list rwl.\n");
    }
    return 1;
  }

  current = event_list->head;

  while (1) {
    if (my_write(out_fd, &(current->event)->id, sizeof(unsigned int)) == -1) {
      print_error("Error writing to fd.\n");
      if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
        print_error("Error unlocking list rwl.\n");
      }
      return 1;
    }

    if (current == to) {
      break;
    }

    current = current->next;
  }

  if (pthread_rwlock_unlock(&event_list->rwl) != 0) {
    print_error("Error unlocking list rwl.\n");
  }
  return 0;
}