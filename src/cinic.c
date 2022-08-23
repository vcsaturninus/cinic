#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>  /* ssize_t */
#include <ctype.h>      /* ASCII char type functions */
#include <inttypes.h>   /* PRIu32 etc */

#include "cinic.h"
#include "utils__.h"

/*
 * By default each key-value entry must appear following a section
 * declaration. Setting this to true makes it legal (where it would
 * otherwise produce an error) to have 'global' key-value entries
 * (or lists) i.e. entries that precede any section declarations.
 */
bool ALLOW_GLOBAL_RECORDS=0;

/*
 * By default an empty list (list without elements) produces an error.
 * Setting this to true will make such list occurences in an .ini file
 * legal. This is still discouraged practice though as empty lists
 * are useless.
 */
bool ALLOW_EMPTY_LISTS=0;

/*
 * Set the namespace delimiter in section titles e.g.
 * section.subsection.subsubsection; '.' is the default.
 * Note this is only significant for the lua or C++ wrappers as
 * there the parser returns a table/map where namespace nesting
 * is represented by table/map nesting. In C code however the
 * parser simply calls a user-registered callback and section names
 * are returned as whole strings. It's up to the user whether
 * they want to split it up into a namespace hierarchy or not.
 */
const char *SECTION_NS_SEP = ".";

/*
 * By default, lists are opened and closed using square brackets
 * ('[', ']'). The user can however customize this by setting
 * LIST_BRACKET to something else, such as curly braces ('{'. '}').
 * The symbol chosen MUST NOT appear anywhere else in that list.
 *
 * LIST_BRACKET MUST be a 2-char string, with LIST_BRACKET[0]
 * being the opening bracket and LIST_BRACKET[1] being the closing
 * bracket.
 */
const char *LIST_BRACKET = "{}";

/*
 * Map of cinic error number to error string;
 * The last entry is a sentinel marking the greatest
 * index in the array. This makes it possible to catch
 * attempts to index out of bounds (see Cinic_err2str()). */
static const char *cinic_error_strings[] = {
    [CINIC_SUCCESS]         = "Success",
    [CINIC_NOSECTION]       = "entry without section",
    [CINIC_MALFORMED]       = "malformed/syntacticaly incorrect",
    [CINIC_TOOLONG]         = "line length exceeds maximum acceptable length(" tostring(MAX_LINE_LEN) ")",
    [CINIC_NESTED]          = "illegal nesting (unterminated list?)",
    [CINIC_NOLIST]          = "list item without list",
    [CINIC_EMPTY_LIST]      = "malformed list (empty list?)",
    [CINIC_MISSING_COMMA]   = "malformed list entry (previous missing comma?)",
    [CINIC_REDUNDANT_COMMA] = "malformed list entry (redundant comma?)",
    [CINIC_REDUNDANT_BRACE] = "malformed list (redundant brace?)",
    [CINIC_SENTINEL]     =  NULL
};


const char *Cinic_err2str(enum cinic_error errnum){
    if (errnum > CINIC_SENTINEL){  /* guard against out-of-bounds indexing attempt */
        fprintf(stderr, "Invalid cinic error number: '%d'\n", errnum);
        exit(errnum);
    }
    return cinic_error_strings[errnum];
}


void cinic_exit_print(enum cinic_error error, uint32_t ln){
    assert(error < CINIC_SENTINEL && error > CINIC_SUCCESS);

    fprintf(stderr, "Cinic: failed to parse line %u -- %s\n", ln, Cinic_err2str(error));
    exit(EXIT_FAILURE);
}

/*
 * Strip leading whitespace from s */
static inline char *strip_lws(char *s){
    assert(s);
    while (*s && isspace(*s)) ++s;
    return s;
}

/*
 * Strip trailing whitespace from s */
static inline char *strip_tws(char *s){
    assert(s);
    char *end = s+ (strlen(s) - 1); /* char before NUL */
    //printf("end = %c\n", *end);
    while (*end != *s && isspace(*end)) --end;
    *(end+1) = '\0';

    return end;
}

/*
 * Return true if c is a comment symbol (# or ;), else false */
static inline bool is_comment(unsigned char c){
    if (c == ';' || c == '#'){
        return true;
    }
    return false;
}

/*
 * Strip everything after (and including) the first comment
 * symbol found on the line */
static inline void strip_comment(char *s){
    assert(s);
    say(" ~ stripping comment from '%s'\n", s);
    while (*s && !is_comment(*s)) ++s;  /* find comment */
    if (is_comment(*s)) *s = '\0';
}


/*
 * Return true if c is a valid/acceptable/allowed char, else false.
 *
 * An acceptable char is one that can appear in a section title,
 * i.e it is part of the regex charset [a-zA-Z0-9-_.@] */
static inline bool is_allowed(unsigned char c){
    if (c == '.' ||
        c == '-' ||
        c == '_' ||
        c == '@' ||
        c == '/' ||
        c == '*' ||
        c == '?' ||
        c == '%' ||
        c == '&' ||
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
static inline uint32_t char_occurs(char c, char *s, bool through_comments){
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
 * True if line consists of just whitespace, else false */
bool is_empty_line(char *line){
    assert(line);
    line = strip_lws(line);
    return (*line == '\0');
}

/*
 * True if line consists of just whitespace and a comment, else false;
 *
 * Acceptable comment symbols are '#' and ';'. Inline comments are
 * possible. Everything following a comment symbol and until the end
 * of the line is considered to be a comment, and the comment symbol
 * can freely be part of an already-started comment without being escaped.
 */
bool is_comment_line(char *line){
    assert(line);
    line = strip_lws(line);
    /* if line has ended or the current char is not a comment symbol */
    return ( *line && is_comment(*line) );
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
        if (!feof(f)){
            perror("Failed to read line (getline())");
            exit(EXIT_FAILURE);
        }else rc = 0;
    }
    return rc;
}

static void cp_to_buff(char dst[], char *src, size_t buffsz, size_t null_at){
    assert(dst && src);
    memset(dst, 0, buffsz);
    strncpy(dst, src, buffsz-1);
    if (buffsz < null_at){
        dst[buffsz-1] = '\0';
    }else{
        dst[null_at] = '\0';
    }
}

/*
 * Return true if line is a .ini section title, else False.
 *
 * IFF line IS an .ini section title, and if name is NOT NULL,
 * the string IS MODIFIED such that it's NUL terminated where the
 * section name ends, and name is made to point to the section
 * title string.
 *
 * A section title line is expected to have the following format:
 *  [section.subsection.subsubsection]
 * Whitespace can come before and after each bracket and inline comments
 * are allowed last on the line.
 */
bool is_section(char *line, char name[], size_t buffsz){
    assert(line);
    char *end=NULL, *sectname=NULL;
    say(" ~~ parsing '%s'\n", line);

    /* strip leading and trailing whitespace and comment */
    line = strip_lws(line);
    strip_comment(line);
    strip_tws(line);

    /* first non-whitespace char must be '[' */
    if (! *line || *line++ != '[') return false;

    /* section NAME (text between [ ]) must be in the allowable set */
    line = strip_lws(line);  /* ws between opening bracket and title is allowed */
    if (! *line || !is_allowed(*line)){
        return false;
    }else{
        sectname = line; /* start of section name */
    }

    /*  go to end of section name; first char after section name must be
     *  either whitespace or the closing bracket */
    while (*line && is_allowed(*line)) ++line;
    if (! *line || ( !isspace(*line) && *line != ']' ) ){
        return false;
    }else{
        end = line;  /* section title end: NUL terminate here */
    }

    line = strip_lws(line); /* ws between title and closing bracket is allowed */
    if (! *line || *line++ != ']') return false;

    /* line has been stripped of leading and trailing ws and any comment,
     * so it should be ending here */
    if (*line) return false; /* should be NUL */

    /* is name != NULL, user wants to mangle the string and extract section name */
    if (name){
        cp_to_buff(name, sectname, buffsz, end-sectname);
    }
    return true;
}

/*
 * true if the line is a key=value entry, else false.
 *
 * the '=' sign must NOT be part of the key or value; it can
 * only be used as the separator between the key-value pair.
 * It can be freely used as part of a comment, however.
 *
 * If k and/or v are not NULL, the string is modified such
 * that NULs are inserted to tokenize the string, and k
 * (if k != NULL) and/or v (if v != NULL) are made to point to
 * the key and value, respectively.
 */
bool is_record_line(char *line, char k[], char v[], size_t buffsz){
    assert(line);
    say(" ~~ parsing '%s'\n", line);
    char *key=NULL, *val=NULL;
    char *key_end=NULL, *val_end=NULL;

    /* strip leading and trailing ws and any comment */
    line = strip_lws(line);
    strip_comment(line);
    strip_tws(line);

    /* first char must be from the allowable set */
    if (! *line || !is_allowed(*line)) return false;
    key = line;  /* start of key */

    /* go to the end of the key */
    while (*line && is_allowed(*line)) ++line;
    if (! *line) return false;
    key_end = line;

    /* intervening whitespace between key, =, and value is allowed */
    line = strip_lws(line);
    if (! *line || *line++ != '=') return false;
    line = strip_lws(line);
    if (! *line || !is_allowed(*line)) return false;
    val = line; /* start of value */

    /* go to the end of value */
    while (*line && is_allowed(*line)) ++line;
    if (*line && !isspace(*line) && !is_comment(*line)) return false;
    val_end = line;

    if (*line) return false;

    /* user wants key or/and val extracted and wants the string mangled for that */
    if (k){
        cp_to_buff(k, key, buffsz, key_end - key);
    }
    if (v){
        cp_to_buff(v, val, buffsz, val_end - val);
    }

    return true;
}

/*
 * true iff the line is the start of a list.
 *
 * This is of the following form:
 *    listTitle = {
 * where whitespace everywhere on the line is ignored and an
 * inline comment can appear last on the line.
 *
 * If k is not NULL, then line is modified to extract the actual
 * key (list name) and k is made to point to it.
 */
bool is_list_head(char *line, char k[], size_t buffsz){
    assert(line);
    char *key_start=NULL, *key_end=NULL;
    say(" ~~ parsing '%s'\n", line);

    /* strip leading and trailing ws and any comment */
    line = strip_lws(line);
    strip_comment(line);
    strip_tws(line);

    /* first char must be in the allowable set */
    if (! *line || !is_allowed(*line)) return false;
    key_start = line;  /* start of key */

    /* go to the end of the key */
    while (*line && is_allowed(*line)) ++line;
    if (! *line) return false;
    key_end = line; /* end of key */

    /* intervening whitespace between key, =, and value is allowed */
    line = strip_lws(line);
    if (! *line || *line++ != '=') return false;
    line = strip_lws(line);
    if (! *line || *line++ != *LIST_BRACKET) return false;

    /* line should be ending here */
    if (*line) return false;

    /* user wants the key name extracted and the line mangled to that end */
    if (k){
        cp_to_buff(k, key_start, buffsz, key_end - key_start);
    }
    return true;
}

/*
 * True iff the line marks the end of a list, else false.
 *
 * A list ends with a closing curly brace on its own line e.g.
 *    }
 * An inline comment is alow allowed as usual provided it comes last
 * on the line.
 */
bool is_list_end(char *line){
    assert(line);
    say(" ~~ parsing '%s'\n", line);

    /* strip leading and trailing ws and any comment */
    line = strip_lws(line);
    strip_comment(line);
    strip_tws(line);

    /* first char must be closing brace */
    if (! *line || (*line++ != LIST_BRACKET[1])) return false;

    /* line should be ending here */
    if (*line) return false;

    return true;
}

/*
 * True iff the line represents a list entry, else false.
 *
 * A list entry is of the following form:
 *   someentry # some comment
 *
 * The comment is of course optional.
 * Whitespace can appear before and/or after the value, but the
 * value itself must be a continuous string with no whitespace gaps.
 *
 * All but the last value in the list should be comma-terminated.
 * There can be arbitrary intervening whitespace between the value and the
 * comma. The comma should not appear anywhere else as part of the value,
 * but can as usual appear as part of the optional comment.
 *
 * If this is the last value in the list and islast is not NULL, a true
 * boolean value will be written to `islast`.
 * If v is not NULL, line will be modified the value substring is extracted
 * from it and v is made to point to it.
 */
bool is_list_entry(char *line, char v[], size_t buffsz, bool *islast){
    assert(line);
    char *val_start = NULL, *val_end = NULL;
    bool last_in_list = false;
    say(" ~~ parsing '%s'\n", line);

    /* strip leading and trailing ws and any comment */
    line = strip_lws(line);
    strip_comment(line);
    strip_tws(line);

    /* ensure the comma only appears ONCE in uncommented text */
    if (char_occurs(',', line, false) > 1) return false;

    say(" ~~ parsing '%s'\n", line);
    /* first char must be from the allowable set */
    if (! *line || !is_allowed(*line)) return false;
    val_start = line;

    /* go to the end of value */
    while (*line && is_allowed(*line)) ++line;
    val_end = line;

    /* strip any whitespace */
    line = strip_lws(line);

    /* char here must be either NUL, a comma, or a comment symbol */
    if (! *line || is_comment(*line)){
        last_in_list = true;
    }else if (*line == ','){
        last_in_list = false;
        ++line;
    }

    /* line should be ending here */
    if (*line) return false;

    /* user wants value extracted and line mangled to that end */
    if (v){
        cp_to_buff(v, val_start, buffsz, val_end - val_start);
    }

    /* user wants to know if this value is the last in the list or not */
    if (islast) *islast = last_in_list;

    return true;
}

/*
 * Parse the .ini config file found at PATH.
 *
 * For each line parsed that is NOT a comment or
 * an empty line, or a section title, or a list start,
 * or list end -- the CB callback gets called.
 *
 * @todo add more detail
 */
int Cinic_parse(const char *path, config_cb cb){
    assert(path && cb);

    int rc = 0;
    char key[MAX_LINE_LEN]     = {0};
    char val[MAX_LINE_LEN]     = {0};
    char section[MAX_LINE_LEN] = {0};

    enum cinic_list_state list = NOLIST;
    bool islast = false;
    uint32_t ln = 0;  /* line number */
    uint32_t bytes_read = 0;

    /* allocated and resized by `getline()` as needed */
    char *buff = NULL;
    size_t buffsz = 0;

    FILE *f = fopen(path, "r");
    if (!f){
        fprintf(stderr, "Failed to open file:'%s'\n", path);
        exit(EXIT_FAILURE);
    }

    while ( ( bytes_read = read_line(f, &buff, &buffsz)) ){
        ++ln;

        if(bytes_read > MAX_LINE_LEN){
            cinic_exit_print(CINIC_TOOLONG, ln);
        }
        else if (is_empty_line(buff) || is_comment_line(buff)){
            continue;
        }
        else if(is_section(buff, section, MAX_LINE_LEN)){
            if (list){
                cinic_exit_print(CINIC_NESTED, ln);
            }
            continue;
        }
        else if (is_record_line(buff, key, val, MAX_LINE_LEN)){
            if (! *section && !ALLOW_GLOBAL_RECORDS){
                cinic_exit_print(CINIC_NOSECTION, ln);
            }else if (list){
                cinic_exit_print(CINIC_NESTED, ln);
            }
        }
        else if(is_list_head(buff, key, MAX_LINE_LEN)){
            list = LIST_START;
            islast = false;
            continue;
        }
        else if(is_list_entry(buff, val, MAX_LINE_LEN, &islast)){
            if (!list){
                cinic_exit_print(CINIC_NOLIST, ln);
            }else if(list == LIST_END){
                cinic_exit_print(CINIC_MISSING_COMMA, ln);
            }

            if (islast){
                list = LIST_END ; /* reset :  */
            }else{
                list = LIST_ONGOING;  /* regular entry in list */
            }
        }
        else if(is_list_end(buff)){
            if (list == LIST_ONGOING){
                cinic_exit_print(CINIC_REDUNDANT_COMMA, ln);
            }
            else if(list == NOLIST){
                cinic_exit_print(CINIC_REDUNDANT_BRACE, ln);
            }
            else if(list == LIST_START && !ALLOW_EMPTY_LISTS){
                cinic_exit_print(CINIC_EMPTY_LIST, ln);
            }
            else if(list == LIST_END){
                list = NOLIST;
            }
            continue;
        }
        else{
            cinic_exit_print(CINIC_MALFORMED, ln);
        }
        rc = cb(ln, list, section, key, val);
        if (rc) return rc;
    }

    fclose(f);  /* fopen resources */
    free(buff); /* getline buffer */
    return 0;   /* success */
}

void Cinic_init(bool allow_globals,
                bool allow_empty_lists,
                const char *section_delim
                )
{
    ALLOW_GLOBAL_RECORDS = allow_globals;
    ALLOW_EMPTY_LISTS = allow_empty_lists;

    if (strlen(section_delim) > 1){
        fprintf(stderr, "Invalid section delimiter specified: '%s' -- must be a single char\n", section_delim);
        exit(EXIT_FAILURE);
    }
    SECTION_NS_SEP = section_delim;
}
