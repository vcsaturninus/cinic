#ifndef CINIC_H__
#define CINIC_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>     /* memset(), strcmp() */

/* stringify token x */
#define tostring__(x) #x
#define tostring(x) tostring__(x)


#define matches(a, b) (!strcmp(a, b))

/* maximum allowable line length in config file */
#define MAX_LINE_LEN 1024U

/*
 * Used to communicate the state of a list. The parser is line-oriented
 * and it does NOT parse the whole file in one go or ahead of time.
 * That is to say, the parser can only know if there has been an error
 * on a preceding line or whether the current line is erroneous but does
 * *not* know whether an error is caused by any subsequent line.
 *
 * To deal with lists, state such as the below must be maintained: the head
 * of a list sets LIST_START, all susequent items BUT THE LAST set
 * LIST_ONGOING, and the last item sets LIST_END. Finally, the closing brace
 * resets back to NOLIST.
 */
enum cinic_list_state{
    LIST_START   = 3,   /* current line/entry is the first in (starts) a list */
    LIST_ONGOING = 2,   /* current line/entry is a regular entry in a list */
    LIST_END     = 1,   /* current line/entry is the last in (ends) a list */
    NOLIST       = 0    /* current line/entry is *not* part of a list */
};

/* Cinic error numbers;
 * also see cinic_error_strings in cinic.c */
enum cinic_error{
	CINIC_SUCCESS = 0,
	CINIC_NOSECTION,
	CINIC_MALFORMED,
	CINIC_TOOLONG,
    CINIC_NESTED,
    CINIC_NOLIST,
    CINIC_EMPTY_LIST,
    CINIC_MISSING_COMMA,
    CINIC_REDUNDANT_COMMA,
    CINIC_REDUNDANT_BRACE,
	CINIC_SENTINEL        /* max index in cinic_error_strings */
};


/* Callback to be called by Cinic_parse (and Cinic_parse_string)
 * on every .ini config line being parsed. The callback will
 * get called with the arguments shown below.
 *
 * If errnum is > 0, there has been a parsing error. The callback
 * decides whether errors are ignored or whether they are fatal:
 *   - If the callback returns 0, the parser continues.
 *   - If the callback returns a value > 0, the parser in turn
 *     immediately returns that error.
 *
 * errnum can be converted to an error string using `Cinic_err2str()`.
 * The error string has local storage and can only be used until the
 * callback returns.
 */
typedef
int (* config_cb)(
		uint32_t ln,                /* line number in the config file, starting from 1 */
        enum cinic_list_state list, /* used for dealing with lists; see enum definition  */
		const char *section,        /* ini config section */
		const char *k,              /* section key / list name */
		const char *v               /* section value / list entry */
		);

/*
 * Parse the .ini config file specified by path.
 *
 * The parsing is carried out line by line and for each
 * line config_cb is called. If there's been an error, this is passed
 * to config_cb as a value > 0. If config_cb returns 0, the parsing
 * continues; otherwise if config_cb returns a value > 0, the parsing
 * is stopped and Cinic_parse returns, in turn, that value. */
int Cinic_parse(
		const char *path,    /* path to .ini config file */
		config_cb
		);

/*
 * Same as Cinic_parse() but a string is parsed instead of a file */
int Cinic_parse_string(
		const char *line,    /* a single line in .ini config file format */
		config_cb
		);

/*
 * Convert errnum to an error string */
const char *Cinic_err2str(
		unsigned int errnum          /* an enum cinic_error */
		);

#endif



void Cinic_init(bool allow_globals,
                bool allow_empty_lists,
                const char *section_delim
        );
