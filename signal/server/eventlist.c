#include <pthread.h>
#include <stdlib.h>

#include "eventlist.h"

/**
 * @brief Creates a new event list.
 *
 * This function allocates memory for a new EventList structure, initializes a read-write lock,
 * and sets the head and tail pointers to NULL.
 *
 * @return A pointer to the new list, or NULL if memory allocation failed or the lock could not be initialized.
 */
struct EventList* create_list() {
  struct EventList* list = (struct EventList*)malloc(sizeof(struct EventList));
  if (!list) return NULL;
  if (pthread_rwlock_init(&list->rwl, NULL) != 0) {
    free(list);
    return NULL;
  }
  list->head = NULL;
  list->tail = NULL;
  return list;
}

/**
 * @brief Appends an event to the end of an event list.
 *
 * This function creates a new list node for the event and appends it to the end of the list.
 * If the list is currently empty, the new node becomes both the head and tail of the list.
 *
 * @param list The list to append to.
 * @param event The event to append.
 * @return 0 if the operation was successful, or 1 if memory allocation failed or the list is NULL.
 */
int append_to_list(struct EventList* list, struct Event* event) {
  if (!list) return 1;

  struct ListNode* new_node = (struct ListNode*)malloc(sizeof(struct ListNode));
  if (!new_node) return 1;

  new_node->event = event;
  new_node->next = NULL;

  if (list->head == NULL) {
    list->head = new_node;
    list->tail = new_node;
  } else {
    list->tail->next = new_node;
    list->tail = new_node;
  }

  return 0;
}

/**
 * @brief Frees the memory used by an event.
 *
 * This function frees the memory used by the event's data field, then frees the event itself.
 * If the event is NULL, the function does nothing.
 *
 * @param event The event to free.
 */
static void free_event(struct Event* event) {
  if (!event) return;
  free(event->data);
  free(event);
}

/**
 * @brief Frees the memory used by an event list.
 *
 * This function iterates over the list, freeing each node and the event it contains.
 * It then frees the list itself and destroys the read-write lock.
 *
 * @param list The list to free.
 */
void free_list(struct EventList* list) {
  if (!list) return;

  struct ListNode* current = list->head;
  while (current) {
    struct ListNode* temp = current;
    current = current->next;

    free_event(temp->event);
    free(temp);
  }

  free(list);
}


/**
 * @brief Retrieves an event with a specific ID from an event list.
 *
 * This function iterates over the list from the node `from` to the node `to`,
 * looking for an event with the ID `event_id`. If it finds such an event, it returns it.
 * If it doesn't find such an event, or if any of the parameters are NULL, it returns NULL.
 *
 * @param list The list to search.
 * @param event_id The ID of the event to search for.
 * @param from The node to start searching from.
 * @param to The node to stop searching at.
 * @return The event with the specified ID, or NULL if no such event was found or an error occurred.
 */
struct Event* get_event(struct EventList* list, unsigned int event_id, struct ListNode* from, struct ListNode* to) {
  if (!list || !from || !to) return NULL;
  struct ListNode* current = from;

  while (1) {
    if (current->event->id == event_id) {
      return current->event;
    }

    if (current == to) {
      return NULL;
    }

    current = current->next;
  }
}