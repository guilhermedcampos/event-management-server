#ifndef COMMON_IO_H
#define COMMON_IO_H

#include <stddef.h>
#include <sys/types.h>

/// Prints an error message to stderr.
/// @param msg The message to print.
void print_error(const char* msg);

/// Writes data from a buffer to a file descriptor.
/// @param fd The file descriptor to write to.
/// @param buffer The buffer to write from.
/// @param size The number of bytes to write.
/// @return The number of bytes written, or -1 if an error occurred.
ssize_t my_write(int fd, const void* buffer, size_t size);

/// Reads data from a file descriptor into a buffer.
/// @param fd The file descriptor to read from.
/// @param buffer The buffer to read into.
/// @param size The number of bytes to read.
/// @return The number of bytes read, or -1 if an error occurred.
ssize_t my_read(int fd, void* buffer, size_t size);

/// Parses an unsigned integer from the given file descriptor.
/// @param fd The file descriptor to read from.
/// @param value Pointer to the variable to store the value in.
/// @param next Pointer to the variable to store the next character in.
/// @return 0 if the integer was read successfully, 1 otherwise.
int parse_uint(int fd, unsigned int *value, char *next);

/// Prints an unsigned integer to the given file descriptor.
/// @param fd The file descriptor to write to.
/// @param value The value to write.
/// @return 0 if the integer was written successfully, 1 otherwise.
int print_uint(int fd, unsigned int value);

/// Writes a string to the given file descriptor.
/// @param fd The file descriptor to write to.
/// @param str The string to write.
/// @return 0 if the string was written successfully, 1 otherwise.
int print_str(int fd, const char *str);

#endif  // COMMON_IO_H
