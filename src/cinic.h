#ifndef CINIC_H__
#define CINIC_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>     /* memset(), strcmp() */

/* maximum allowable line length in config file */
#define MAX_LINE_LEN 1024U

#define matches(a, b) (!strcmp(a, b))

/* Cinic error numbers;
 * also see cinic_error_strings in cinic.c */
enum cinic_error{
	CINIC_SUCCESS = 0,
	CINIC_NOSECTION,
	CINIC_MALFORMED,
	CINIC_TOOLONG,
	CINIC_NONEWLINE,
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
		uint32_t ln,         /* line number in the config file, starting from 1 */
		const char *section, /* ini config section */		
		const char *k,       /* section key */
		const char *v,       /* section value */
		int errnum           /* ini config error number; can be converted to string using err2str(); */
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



void Cinic_init(int allow_globals);
