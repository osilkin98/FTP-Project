//
// Created by oleg on 10/11/20.
//

#ifndef FTP_PROJECT_STRFUN_H
#define FTP_PROJECT_STRFUN_H

/* strtok: tokenizes the input `str` by the characters in `delim`;
 *         returns a pointer to the token
 *
 * subsequent calls to strtok with a NULL in the `str` param will continue
 * tokenizing `str` from an internal reference, returning the next token each time.
 * Once no more tokens remain, the function will return a NULL pointer.
 *
 * Note: This function modifies the state of `str`, so it is wise to
 *
 * */
char *strtok(char * str, const char * delim);


/* strcpy: copies the contents of `t` into `s` */
char *strcpy(char *s, const char *t);

/* returns 1 if `c` is in `str`, else 0 */
int in_str(char c, const char * str);


#endif //FTP_PROJECT_STRFUN_H
