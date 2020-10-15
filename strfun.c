//
// Created by oleg on 10/11/20.
//

#include "strfun.h"
#include <stddef.h>

char *strtok(char* str, const char* delimeters) {

    // create the index variable that we'll use to scan through the str

    // store an internal reference to the string
    static char *str_ref;
    if (!str)
        str = str_ref;
    else
        str_ref = str;

    // if the original string has nothing left
    if (!str && !str_ref)
        return NULL;

    // set the token's starting position
    char c;
    do {
        c = *str++;
        if (c == '\0') {
            return NULL;
        }
    } while(in_str(c, delimeters));

    // readjust string
    str--;
    str_ref = str;

    // move the reference string to the end of the token
    while(*str_ref != '\0' && !in_str(*str_ref, delimeters))
        str_ref++;

    // set the end of the token with a null terminator
    if (*str_ref != '\0') {
        *str_ref = '\0';
        str_ref++;
    }

    // return the beginning of the pointer
    return str;
}


char *strcpy(char *s, const char *t) {
    for (size_t i = 0; (s[i] = t[i]) != '\0'; ++i)
        ;
    return s;
}


int in_str(char c, const char * str) {
    for(size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == c) {
            return 1;
        }
    }
    return 0;
}

