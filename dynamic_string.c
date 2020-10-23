#include "dynamic_string.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


void string_init(char** string_ptr) {
  // Create a string pointer and allocate memory for it
  char *ptr = malloc(16);
  // Dereference our pointer and set its address to the new contiguous block of memory
  *string_ptr = ptr;
}


void string_free(char** string_ptr) {
  free(*string_ptr);
}

void string_set(char** destination, char* value) {
    int new_size = strlen(value);

    // Add 1 to account for '\0' null terminator
    *destination = realloc(*destination, sizeof(char)*new_size + 1);

    if(*destination == NULL) {
      printf("Unable to realloc() ptr!");
    } else {
      strcpy(*destination, value);
    }
}

/* Appends the contents of src to dest, reallocating when necessary */
int dyn_concat(char** dest, const char *src) {
    size_t curr_len = strlen(*dest);
    size_t extra_len = strlen(src);

    *dest = realloc(*dest, sizeof(char) * (curr_len + extra_len + 1));

    if (*dest == NULL) {
      return 1;
    }
    strcat(*dest, src);
    return 0;

}