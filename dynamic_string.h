#pragma once
#ifndef DYNAMIC_STRING_H
#define DYNAMIC_STRING_H
/* 
    Dynamic String Library, 
    author: Jesse Lawson,
    source: https://jesselawson.org/blog/2019/04/dynamic-strings-in-c-and-a-crash-course-in-pointers/
*/

/* Initializes an empty pointer to a block of memory to store a string 

Usage:
    char *mystring;
    string_init(&mystring);

*/
void string_init(char** string_ptr);


/* Cleaner function to dereference the pointer & 
 * free the address of memory it was pointing to 
 * */
void string_free(char **string_ptr);


/* Determines how much space must be reallocated to store
    the new pointer, reallocates the pointer to fit the new size,
    and copies the value into the new space 
    */
void string_set(char **destination, char *value);

/* Appends the contents of src to dest, reallocating when necessary */
int dyn_concat(char **dest, const char *src);

#endif