#include "io.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Prints an error message to stderr in a thread-safe manner.
 *
 * This function acquires a mutex before printing to prevent race conditions
 * when multiple threads are printing error messages.
 *
 * @param msg The error message to print.
 */
void print_error(const char* msg) {
    pthread_mutex_lock(&print_mutex);
    fprintf(stderr, "%s", msg);
    pthread_mutex_unlock(&print_mutex);
}

/**
 * Reads data from a file descriptor into a buffer.
 *
 * This function attempts to read `size` bytes of data from the file descriptor `fd`
 * into the buffer `buffer`. If the read operation is interrupted by a signal, it returns -2.
 * If any other read error occurs, it returns -1.
 *
 * @param fd The file descriptor to read from.
 * @param buffer The buffer to read data into.
 * @param size The maximum number of bytes to read.
 * @return The number of bytes read, or -1 or -2 if an error occurred.
 */
ssize_t my_read(int fd, void* buffer, size_t size) {
    ssize_t done = 0;
    ssize_t new_size = (ssize_t) size;
    while (done < new_size) {
      ssize_t bytes_read = read(fd, (char*)buffer + done, (size_t)(new_size - done));
      if (bytes_read < 0) {
          if (errno == EINTR) {
              return -2;
          } else {
              return -1;
          }
      }
      
      // if we read 0 bytes, we're done
      if (bytes_read == 0)
          break;

      done += bytes_read;
    }

    return done;
}


/**
 * Writes data from a buffer to a file descriptor.
 *
 * This function attempts to write `size` bytes of data from the buffer `buffer`
 * to the file descriptor `fd`. If the write operation is interrupted by a signal, it returns -1.
 *
 * @param fd The file descriptor to write to.
 * @param buffer The buffer to write data from.
 * @param size The number of bytes to write.
 * @return The number of bytes written, or -1 if an error occurred.
 */
ssize_t my_write(int fd, const void* buffer, size_t size) {
    ssize_t done = 0;
    ssize_t new_size = (ssize_t) size;
    while (new_size > 0) {
        ssize_t bytes_written = write(fd, (const char*)buffer + done, (size_t)new_size);
        if (bytes_written < 0) {
            return -1;
        }

        new_size -= bytes_written;
        done += bytes_written;
    }

    return done;
}


int parse_uint(int fd, unsigned int *value, char *next) {
  char buf[16];

  int i = 0;
  while (1) {
    ssize_t read_bytes = read(fd, buf + i, 1);
    if (read_bytes == -1) {
      return 1;
    } else if (read_bytes == 0) {
      *next = '\0';
      break;
    }

    *next = buf[i];

    if (buf[i] > '9' || buf[i] < '0') {
      buf[i] = '\0';
      break;
    }

    i++;
  }

  unsigned long ul = strtoul(buf, NULL, 10);

  if (ul > UINT_MAX) {
    return 1;
  }

  *value = (unsigned int)ul;

  return 0;
}

/**
 * Parses an unsigned integer from a file descriptor.
 *
 * This function reads characters from the file descriptor `fd` until it encounters
 * a character that is not a digit. It then converts the characters read into an
 * unsigned integer and stores it in `value`. The character that caused the parsing
 * to stop is stored in `next`.
 *
 * @param fd The file descriptor to read from.
 * @param value Pointer to an unsigned int where the parsed value will be stored.
 * @param next Pointer to a char where the next non-digit character will be stored.
 * @return 0 if the parsing was successful, or 1 if an error occurred.
 */
int print_uint(int fd, unsigned int value) {
  char buffer[16];
  size_t i = 16;

  for (; value > 0; value /= 10) {
    buffer[--i] = (char)('0' + (value % 10));
  }

  if (i == 16) {
    buffer[--i] = '0';
  }

  while (i < 16) {
    ssize_t written = write(fd, buffer + i, 16 - i);
    if (written == -1) {
      return 1;
    }

    i += (size_t)written;
  }

  return 0;
}

/**
 * Writes a string to a file descriptor.
 *
 * This function writes the string `str` to the file descriptor `fd`. It uses a loop
 * to ensure that the entire string is written, even if `write` writes less data than requested.
 *
 * @param fd The file descriptor to write to.
 * @param str The string to write.
 * @return The number of bytes written, or -1 if an error occurred.
 */
int print_str(int fd, const char *str) {
  size_t len = strlen(str);
  while (len > 0) {
    ssize_t written = write(fd, str, len);
    if (written == -1) {
      return 1;
    }

    str += (size_t)written;
    len -= (size_t)written;
  }

  return 0;
}
