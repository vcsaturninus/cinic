#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>  /* ssize_t */
#include <ctype.h>      /* ASCII char type functions */

#include "cinic.h"


#define UNUSED(x) do{ (void)x; } while(0)

/* map of cinic error number to error string;
 * The last entry is a sentinel marking the greatest
 * index in the array. This makes it possible to catch
 * attempts to index out of bounds (see Cinic_err2str()). */
const char *cinic_error_strings[] = {
	[CINIC_SUCCESS]      = "Success",
	[CINIC_NOSECTION]    = "Entry without section",
	[CINIC_MALFORMED]    = "Syntactically incorrect line",
	[CINIC_TOOLONG]      = "Line length exceeds maximum acceptable length",
	[CINIC_NONEWLINE]    = "Newline not found",
	[CINIC_SENTINEL]     =  NULL 
};

const char *Cinic_err2str(unsigned int errnum){
	if (errnum > CINIC_SENTINEL){
		fprintf(stderr, "Invalid cinic error number: '%d'\n", errnum);
		exit(EXIT_FAILURE);
	}
	return cinic_error_strings[errnum];
}

static inline char *strip_ws(char *s){
    assert(s);
    while (*s && isspace(*s)) ++s;
    return s;
}

/* 
 * Return true if c is a comment symbol (# or ;), else false */
bool is_comment(unsigned char c){
	if (c == ';' || c == '#'){
		return true;
	}
	return false;
}

/*
 * Return true if c is a valid/acceptable char, else false.
 *
 * An acceptable char is one that can appear in a section title,
 * i.e it is part of the regex charset [a-zA-Z0-9-_.] */
static inline bool is_allowed(unsigned char c){
    if (c == '.' ||
        c == '-' ||
        c == '_' ||
        c == '@' ||
        isalnum(c)
        )
    {
        return true;
    }

    return false;
}

/* 
 * Return the number of occurences of c in s.
 * s must not be NULL.
 * if through_comments is false, then this function
 * stops as soon as it runs into a comment symbol,
 * otherwise it keeps going even through a comment. */
uint32_t char_occurs(char c, char *s, bool through_comments){
	assert(s);
	uint32_t count = 0;

	while (*s){
		if (!through_comments && is_comment(*s)) return count;
		if (*s == c) ++count;
		++s;
	}
	return count;
}


/*
 * Wrapper around getline()
 *
 * This function should never fail; on failure, it exits the program.
 * buff should initially be a NULL pointer and buffsz should initially
 * point to a size_t type that is set to 0. `getline()` will allocate enough
 * memory to accomodate a string of any length.
 *
 * <-- @return
 *     The result returned by `getline()`. 
 */
uint32_t read_line(FILE *f, char **buff, size_t *buffsz){
	assert(f && buff && buffsz);
	ssize_t rc = getline(buff, buffsz, f);
	if (rc < 0 /* -1 */){
		perror("Failed to read line (getline())");
		exit(EXIT_FAILURE);
	}
	return rc;
}

/* Return true if line is a .ini section, else False.
 *
 * A section line is expected to have the following format:
 * <whitespace> [section.subsection.subsubsection]<whitespace>
 */
bool is_section(char *line, char **name){
	assert(line);
	assert(name);

    printf("here1: %s, [0]=%c\n", line, *line);

	/* strip leading whitespace, if any */
	while (*line && isspace(*line)) ++line;

    printf("here2: %s, [0]=%c\n", line, *line);

	/* first non-whitespace char must be '[' */
	if (! *line || *line != '[') return false;
    ++line;
    printf("here3: %s, [0]=%c\n", line, *line);
	
	/* section NAME (text between [ ]) must be alphanumeric */
	while (*line && isspace(*line)) ++line;  // leading whitespace is allowed though
    printf("here4: %s, [0]=%c\n", line, *line);
	if (! *line || !is_allowed(*line)) return false;
    *name = line; /* start of section name */
	while (*line && is_allowed(*line)) ++line;  // go to end of section name
    printf("here5: %s, [0]=%c\n", line, *line);
    if (! *line || (!isspace(*line) && *line != ']')) return false;
    if (isspace(*line)) *line++ = '\0';

    printf("here5.5: %s, [0]=%c\n", line, *line);
	while (*line && isspace(*line)) ++line;  // ditto for trailing whitespace
    printf("here6: %s, [0]=%c\n", line, *line);
	if (! *line || *line != ']') return false;
    *line++ = '\0';
    printf("here7: %s, [0]=%c\n", line, *line);

	/* there must be NO other text (other than whitespace or a comment)
	 * after closing bracket */
	while (*line && isspace(*line)) ++line;
    printf("here8: %s, [0]=%c\n", line, *line);
	if (*line && !is_comment(*line)) return false;  // must be a comment symbol
	
    printf("here9: %s, [0]=%c\n", line, *line);
	return true;
}

/* True if line consists of just whitespace, else false */
bool is_empty_line(char *line){
	assert(line);
    line = strip_ws(line);
	if (*line == '\0') return true;
	return false;
}

/* True if line consists of just whitespace and a comment, else false;
 *
 * Acceptable comment symbols are '#' and ';'. Inline comments are 
 * possible. Everything following a comment symbol and until the end
 * of the line is considered to be a comment, and ; or # can freely
 * be part of an already-started comment without being escaped.
 */
bool is_comment_line(char *line){
	assert(line);
    line = strip_ws(line);
    /* if line has ended or the current char is not a comment symbol */
	if (! *line || !is_comment(*line) ){
		return false;
	}
	return true;
}

/* 
 * true if the line is a key=value entry, else false.
 *
 * the '=' sign must NOT be part of the key and it must 
 * FIRST appear as a separator between key and value. 
 * subsequent occurences are then considered to be part of the 
 * value (unless part of a comment) so they can be freely 
 * included without needing to be escaped.
 */
bool is_record_line(char *line, char **k, char **v){
	assert(line);
    assert(k && v);

	/* strip leading whitespace */
    line = strip_ws(line);

	/* first char must be alphanumeric */
    if (! *line || !(*line)) return false;
	*k = line;  /* start of key */

	/* go to the end of the key */
	while (*line && is_allowed(*line)) ++line;
	if (! *line) return false;
	if (isspace(*line)) *line++ = '\0';  /* end of key */
	
	/* intervening whitespace between key, =, and value is allowed */
    line = strip_ws(line);
	if (! *line || *line != '=') return false;
	if(*line == '=' ) *line++ = '\0';
    line = strip_ws(line);
	if (! *line || !is_allowed(*line)) return false;
	*v = line; /* start of value */

	/* go to the end of value */
	while (*line && is_allowed(*line)) ++line;
	if (! *line) return true;
    if (!isspace(*line) && !is_comment(*line)) return false;
	*line++ = '\0';  /* end of value */
	
	/* following the value must only be whitespace and optional comment */
    line = strip_ws(line);
	if (*line && !is_comment(*line)) return false;
    
    puts("returning true");
	return true;
}

bool is_list_head(char *line, char **k){
	assert(line);
    
    char *key_end = NULL;

	/* strip leading whitespace */
    line = strip_ws(line);

	/* first char must be alphanumeric */
	if (! *line || !is_allowed(*line)) return false;
	*k = line;  /* start of key */

	/* go to the end of the key */
	while (*line && is_allowed(*line)) ++line;
	if (! *line) return false;
    key_end = line; /* end of key */
	
	/* intervening whitespace between key, =, and value is allowed */
    line = strip_ws(line);
	if (! *line || *line != '=') return false;
	++line;
    line = strip_ws(line);
	if (! *line || *line != '{') return false;
	++line;

	/* go to the end of value */
    line = strip_ws(line);
	if (*line && !is_comment(*line)) return false;
    
    *key_end = '\0'; /* NUL-terminate the key */
	return true;
}

bool is_list_end(char *line){
	assert(line);

	/* strip leading whitespace */
    line = strip_ws(line);

	/* first char must be closing brace */
	if (! *line || (*line != '}')) return false;

	return true;
}

bool is_list_entry(char *line, char **v, bool *islast){
    assert(line && v && islast);
    printf("validating '%s'\n", line);
    
    char *val_end = NULL;

	/* strip leading whitespace */
    line = strip_ws(line);
	if (! *line || !is_allowed(*line)) return false;

    *v = line; /* start of item name */
	while (*line && is_allowed(*line)) ++line;
    printf("line here = '%s'\n", line);
    if (! *line){
        *islast = true; 
        return true;
    }else if (*line == ','){
        *islast = false;
        val_end = line++;
    }else if (!isspace(*line) && !is_comment(*line)){
        return false;
    }else if (isspace(*line) || is_comment(*line)){
        *islast = true;
        val_end = line;
    }
    
    //printf("val_end='%s'\n", val_end);

    line = strip_ws(line);
    printf("'%c', %d\n", *line, is_comment(*line));
    if (*line && !is_comment(*line)) return false;
    *val_end = '\0';

    printf("returning true, '%s', islast=%d\n", *v, *islast);
    return true;
}


int Cinic_parse(const char *path, config_cb cb){
	assert(path && cb);

	int rc = 0;
    UNUSED(rc);
	enum cinic_error errnum = CINIC_SUCCESS;
	char *key = NULL, *val = NULL, *section = NULL;
    UNUSED(key);
    UNUSED(val);
    UNUSED(section);
    UNUSED(errnum);
	uint32_t ln = 1;  /* line number */

	/* allocated and resized by `getline()` as needed */
	char *buff = NULL;
	size_t buffsz = 0;
	
	FILE *f = fopen(path, "r");
	if (!f){
		fprintf(stderr, "Failed to open file:'%s'\n", path);
		exit(EXIT_FAILURE);
	}

	while (read_line(f, &buff, &buffsz)){
        // call callback for each line
		++ln;
	}

	return 0;   /* success */
}
