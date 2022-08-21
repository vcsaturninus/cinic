#ifndef CINIC_UTILS__H
#define CINIC_UTILS__H

#include <stdio.h>

#include "cinic.h"

/*---------------------------------------------------------
 * This header declares variables and function prototypes |
 * only meant for internal use. The public API of         |
 * interest to the user is defined in cinic.h instead.    |
 *--------------------------------------------------------*/

#define UNUSED(x) do{ (void)x; } while(0)

//#define DEBUG_MODE
#ifdef DEBUG_MODE
#   define say(...) fprintf(stderr, __VA_ARGS__);
#else 
#   define say(...) 
#endif

/* defined in cinic.h */
//extern int ALLOW_GLOBAL_RECORDS;
//extern int ALLOW_EMPTY_LISTS;

const char *Cinic_err2str(enum cinic_error errnum);
void cinic_exit_print(enum cinic_error error, uint32_t ln);
uint32_t read_line(FILE *f, char **buff, size_t *buffsz);

bool is_empty_line(char *line);
bool is_comment_line(char *line);
bool is_section(char *line, char name[], size_t buffsz);
bool is_record_line(char *line, char k[], char v[], size_t buffsz);
bool is_list_head(char *line, char k[], size_t buffsz);
bool is_list_end(char *line);
bool is_list_entry(char *line, char v[], size_t buffsz, bool *islast);
int Cinic_parse(const char *path, config_cb cb);
void Cinic_init(bool allow_globals, bool allow_empty_lists);




#endif
