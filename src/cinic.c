#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     /* meset(), strlen() */
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
const char *LIST_BRACKET = "[]";

/*
 * Map of cinic error number to error string;
 * The last entry is a sentinel marking the greatest
 * index in the array. This makes it possible to catch
 * attempts to index out of bounds (see Cinic_err2str()). */
static const char *cinic_error_strings[] = {
    [CINIC_SUCCESS]           = "Success",
    [CINIC_NOSECTION]         = "entry without section",
    [CINIC_MALFORMED]         = "malformed/syntacticaly incorrect",
    [CINIC_MALFORMED_LIST]    = "malformed/syntacticaly incorrect list",
    [CINIC_TOOLONG]           = "line length exceeds maximum acceptable length(" tostring(MAX_LINE_LEN) ")",
    [CINIC_NESTED]            = "illegal nesting (unterminated list?)",
    [CINIC_NOLIST]            = "list item without list",
    [CINIC_EMPTY_LIST]        = "malformed list (empty list?)",
    [CINIC_MISSING_COMMA]     = "malformed list entry (previous missing comma?)",
    [CINIC_REDUNDANT_COMMA]   = "malformed list entry (redundant comma?)",
    [CINIC_REDUNDANT_BRACKET] = "malformed list (redundant bracket ?)",
    [CINIC_LIST_NOT_STARTED]  = "malformed list (missing opening bracket ?)",
    [CINIC_LIST_NOT_ENDED]    = "malformed list (unterminated list ?)",
    [CINIC_SENTINEL]          =  NULL
};

/*
 * Convert errnum to error string. Errnum must be < CINIC_SENTINEL */
const char *Cinic_err2str(enum cinic_error errnum){
    if (errnum >= CINIC_SENTINEL){  /* guard against out-of-bounds indexing attempt */
        fprintf(stderr, "Invalid cinic error number: '%d'\n", errnum);
        exit(13);
    }
    return cinic_error_strings[errnum];
}

/*
 * Figure out if list state transition makes sense.
 *
 * If the transition from the previous list state to the next one is as
 * expected, return CINIC_SUCCESS. Otherwise return an error number.
 */
enum cinic_error Cinic_get_list_error(enum cinic_list_state prev, enum cinic_list_state next){
    say("Assessing list state transition from  prev=%i to next=%i\n", prev, next);
    switch(prev){

        case NOLIST:
            if (next == LIST_HEAD){
                return CINIC_SUCCESS;
            }else if (next == NOLIST){
                return CINIC_REDUNDANT_BRACKET;
            }else return CINIC_NOLIST;
            break;

        case LIST_HEAD:
            if (next == LIST_OPEN){
                return CINIC_SUCCESS;
            }else if (next == LIST_HEAD){
                return CINIC_MALFORMED_LIST;
            }else return CINIC_LIST_NOT_STARTED;
            break;

        case LIST_OPEN:
            if (next == LIST_ONGOING || next == LIST_LAST){
                return CINIC_SUCCESS;
            }else if (next == LIST_HEAD){
                return CINIC_NESTED;
            }else if (next == LIST_OPEN){
                return CINIC_REDUNDANT_BRACKET;
            }else if (next == NOLIST && !ALLOW_EMPTY_LISTS){
                return CINIC_EMPTY_LIST;
            }else return CINIC_MALFORMED_LIST;
            break;

        case LIST_ONGOING:
            if (next == LIST_ONGOING || next == LIST_LAST){
                return CINIC_SUCCESS;
            }else if (next == NOLIST){
                return CINIC_REDUNDANT_COMMA;
            }else return CINIC_LIST_NOT_ENDED;
            break;

        case LIST_LAST:
            if (next == NOLIST){
                return CINIC_SUCCESS;
            }else if (next == LIST_HEAD){
                return CINIC_NESTED;
            }else if (next == LIST_OPEN){
                return CINIC_MALFORMED_LIST;
            }else return CINIC_MISSING_COMMA;
            break;
    }
    return CINIC_SUCCESS;
}

/*
 * Print Cinic error message associated with error, and exit with the same error code. */
void cinic_exit_print(enum cinic_error error, uint32_t ln){
    assert(error < CINIC_SENTINEL && error > CINIC_SUCCESS);

    fprintf(stderr, "Cinic: failed to parse line %" PRIu32 " -- %s\n", ln, Cinic_err2str(error));
    exit(EXIT_FAILURE);
}

/*
 * Strip leading whitespace from s
 * @destructive.
 * */
char *strip_lws(char *s){
    assert(s);
    while (*s && isspace(*s)) ++s;
    return s;
}

/*
 * Strip trailing whitespace from s.
 * @destructive
 * */
void strip_tws(char *s){
    assert(s);
    if (! *s){  /* empty line */
        return;
    }else{
        char *end = s + (strlen(s) - 1);  /* char before NUL */
        while (end >= s && isspace(*end)) --end;
        *(end+1) = '\0';
    }
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
 * symbol found on the line.
 * @destructive
 * */
void strip_comment(char *s){
    assert(s);
    say(" ~ stripping comment from '%s'\n", s);
    while (*s && !is_comment(*s)) ++s;  /* find comment/NUL */
    *s = '\0';
}

/*
 * Return true if c is a valid/acceptable/allowed char, else false.
 *
 * An acceptable char is one that can appear in a e.g. section title.
 *
 * If ws_allowed is true, then a whitespace character is considered
 * legal. This is useful for example when the value is meant to be
 * a string such as a description of something. Whitespace SHOULD NOT
 * appear in a section title; it's only allowable in record values.
 */
static inline bool is_allowed(char c, bool ws_allowed){
    if (c == '.' ||
        c == '-' ||
        c == '_' ||
        c == '@' ||
        c == '/' ||
        c == '*' ||
        c == '?' ||
        c == '%' ||
        c == '&' ||
        isalnum(c) ||
        (isspace(c) && ws_allowed)
       )
    {
        return true;
    }

    return false;
}

/*
 * Return the number of occurences of c in the non-NULL string s.
 *
 * if through_comments is false, then this function
 * stops as soon as it runs into a comment symbol,
 * otherwise it keeps going even through a comment.
 * */
static inline uint32_t char_occurs(char c, char *s, bool through_comments){
    assert(s);
    uint32_t count = 0;

    while (*s){
        if (!through_comments && is_comment(*s)) return count;
        if (*s++ == c) ++count;
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
 * of the line is considered part of the comment, and the comment symbol
 * can freely be part of an already-started comment without being escaped.
 */
bool is_comment_line(char *line){
    assert(line);
    line = strip_lws(line);
    /* FALSE if line has ended or the current char is not a comment symbol */
    return ( *line && is_comment(*line) );
}

/*
 * Wrapper around getline()
 *
 * This function should never fail; on failure, it exits the program.
 * buff should initially be a NULL pointer and buffsz should initially
 * point to a size_t type that is set to 0.
 * `getline()` will allocate enough memory to accomodate a string of any
 * length and will point buff to a chunk of dynamic memory the size of
 * which is writen to buffsz.
 *
 * The user is responsible for freeing *buff.
 *
 * <-- @return
 *     the value returned by getline() except if it's < 0; in that case
 *     the program will just exit with error.
 */
uint32_t read_line(FILE *f, char **buff, size_t *buffsz){
    assert(f && buff && buffsz);
    ssize_t rc = getline(buff, buffsz, f);
    if (rc < 0 /* -1 */){
        if (!feof(f)){
            perror("Failed to read line (getline())");
            exit(EXIT_FAILURE);
        }else rc=0;  /* EOF */
    }
    return rc;
}

/*
 * Copy src to dst (as much of src as fits in buffsz, which must
 * indicate the size of buf) and NUL-terminate either at null_at
 * (if null_at is smaller than buffsz) or otherwize at buffsz-1.
 *
 * buffsz must indicate the size of dst.
 */
static void cp_to_buff(char dst[], char *src, size_t buffsz, size_t null_at){
    assert(dst && src);
    memset(dst, 0, buffsz);
    strncpy(dst, src, buffsz-1);
    if (null_at < buffsz){
        dst[null_at] = '\0';
    }else{
        dst[buffsz-1] = '\0';
    }
}

/*
 * Return true if line is a .ini section title, else False.
 *
 * IFF line IS an .ini section title, and if name is NOT NULL,
 * the section title string exctracted is written to NAME whose
 * size is specified by buffsz.
 *
 * A section title line is expected to have the following format:
 *  [section.subsection.subsubsection]
 *
 * NOTES:
 *  - line must not be NULL
 *  - line must be STRIPPED of comment, and leading and trailing whitespace
 */
bool is_section_line(char *line, char name[], size_t buffsz){
    assert(line);
    char *end=NULL, *start=NULL;
    say(" ~~ is_section_line ? : '%s'\n", line);

    /* first non-whitespace char must be '[' */
    if (! *line || *line++ != '[') return false;

    /* section NAME (text between [ ]) must be in the allowable set */
    line = strip_lws(line);  /* ws between opening bracket and title is allowed */
    if (! *line || !is_allowed(*line, false)){
        return false;
    }else{
        start = line; /* start of section name */
    }

    /*  go to end of section name; first char after section name must be
     *  either whitespace or the closing bracket */
    while (*line && is_allowed(*line, false)) ++line;
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

    /* if name != NULL, user wants to extract section name */
    if (name){
        cp_to_buff(name, start, buffsz, end-start);
    }
    return true;
}

/*
 * True if the line is a key=value entry (i.e. a 'record'), else false.
 *
 * the '=' sign must NOT be part of the key or value; it can
 * only be used as the separator between the key-value pair.
 * It can be freely used as part of a comment, however.
 *
 * If k and/or v are not NULL, the key and/or value are written to
 * K and/or V, respectively. Each of k and v (if not NULL) must have
 * size BUFFSZ.
 *
 * NOTES:
 *  - line must not be NULL
 *  - line must be STRIPPED of comment, and leading and trailing whitespace
 */
bool is_record_line(char *line, char k[], char v[], size_t buffsz){
    assert(line);
    say(" ~~ is_record_line? : '%s'\n", line);
    char *key=NULL, *val=NULL;
    char *key_end=NULL, *val_end=NULL;

    /* first char must be from the allowable set */
    if (! *line || !is_allowed(*line, false)) return false;
    key = line;  /* start of key */

    /* go to the end of the key */
    while (*line && is_allowed(*line, false)) ++line;
    if (! *line) return false;
    key_end = line;

    /* intervening whitespace between key, =, and value is allowed */
    line = strip_lws(line);
    if (! *line || *line++ != '=') return false;
    line = strip_lws(line);
    if (! *line || !is_allowed(*line, false)) return false;
    val = line; /* start of value */

    /* go to the end of value */
    while (*line && is_allowed(*line, true)) ++line;
    if (*line) return false;
    val_end = line;

    /* user wants key or/and val extracted */
    if (k){
        cp_to_buff(k, key, buffsz, key_end - key);
    }
    if (v){
        cp_to_buff(v, val, buffsz, val_end - val);
    }

    return true;
}

/*
 * Get list token from the non-NULL line.
 *
 * This function is used to retrieve list tokens from a line, one by one.
 * The current token is written to buff -- which must not be NULL and whose
 * size must be indicated via buffsz. A pointer to the start of the next
 * token is returned. When there are no more tokens left in line, NULL
 * is returned.
 *
 * This function can be used in a loop, continuing for as long as the
 * return value is != NULL.
 *
 * A list token is one of: <list head>, '=', <opening/closing bracket>,
 * <list item (regular or final)>.
 */
char *get_list_token(char *line, char buff[], size_t buffsz){
    assert(line);
    line = strip_lws(line);
    strip_comment(line);
    strip_tws(line);

    char *start = line;  /* start of current token */
    char *end = NULL;
    char *next = NULL;

    /* empty list */
    if (!*line) return NULL;

    say(" ~ looking for list token in '%s'\n", line);
    while (*line && is_allowed(*line, false)) ++line;  /* find end of token */
    line = strip_lws(line);

    /* equals sign, opening bracket, or comma */
    if (*line == '=' || *line == *LIST_BRACKET || *line == ','){
        end = line;
        cp_to_buff(buff, start, buffsz, (end-start) + 1);
    }
    /* list item */
    else if(is_allowed(*line, false)){
        end = line - 1;
        cp_to_buff(buff, start, buffsz, (end - start) + 1);
    }
    /* closing bracket ... */
    else if(*line == LIST_BRACKET[1]){
        if (is_allowed(*start, false)){ /* preceded by list item */
            end = line-1;
        }else end = line;  /* without preceding item */
        cp_to_buff(buff, start, buffsz, (end - start) + 1);
    }
    /* unrecognized token; return to higher-level decider */
    else{
        end = start + strlen(start) - 1; /* do not include NUL */
        cp_to_buff(buff, start, buffsz, (end - start) + 1);
    }

    if (! *end ){
        next = NULL;
    }else{
        next = end+1;  /* might be empty string with just NUL; handled at the top */
    }

    return next;
}

/*
 * true iff the line is the start of a list.
 *
 * This is of the following form:
 *    listTitle =
 * where whitespace between listTitle and '=' is ignored.
 *
 * If k is not NULL, then it must be a buffer of size buffsz which
 * the list head string is going to be extracted and written into.
 *
 * NOTES:
 *  - line must not be NULL
 *  - line must be STRIPPED of comment, and leading and trailing whitespace
 */
bool is_list_head(char *line, char k[], size_t buffsz){
    assert(line);
    char *key_start=NULL, *key_end=NULL;
    say(" ~~ is_list_head ? : '%s'\n", line);

    /* first char must be in the allowable set */
    if (! *line || !is_allowed(*line, false)) return false;
    key_start = line;  /* start of key */

    /* go to the end of the key */
    while (*line && is_allowed(*line, false)) ++line;
    if (! *line) return false;
    key_end = line; /* end of key */

    /* intervening whitespace between key and = is allowed */
    line = strip_lws(line);
    if (! *line || *line++ != '=') return false;

    /* line should be ending here */
    if (*line) return false;

    /* user wants the key name extracted */
    if (k){
        cp_to_buff(k, key_start, buffsz, key_end - key_start);
    }
    return true;
}

/*
 * True if the single-char line is a closing bracket to terminate a list.
 *
 * NOTES:
 *  - line must not be NULL
 *  - line must be STRIPPED of comment, and leading and trailing whitespace
 */
bool is_list_end(char *line){
    assert(line);
    say(" ~~ is_list_end ? : '%s'\n", line);

    /* only char must be closing bracket */
    if (strlen(line) != 1 || *line != LIST_BRACKET[1]){
        return false;
    }

    return true;
}

/*
 * True if the single-char line is an opening bracket to begin a list.
 *
 * NOTES:
 *  - line must not be NULL
 *  - line must be STRIPPED of comment, and leading and trailing whitespace
 */
bool is_list_start(char *line){
    assert(line);
    say(" ~~ is_list_start ? : '%s'\n", line);

    /* only char must be opening bracket */
    if (strlen(line) != 1 || *line != *LIST_BRACKET){
        return false;
    }

    return true;
}

/*
 * True iff the line represents a list entry, else false.
 *
 * A list entry is a contigous string of characters that are in the
 * allowed set. If the string ends with a comma, it's a regular list
 * item, otherwise it's a 'final' list item. All items in a list but
 * the last must be followed by a comma. An arbitrary amount of
 * whitespace can come after the item and before the comma.
 * but can as usual appear as part of the optional comment.
 *
 * If the value is NOT followed by a coma, it is considered to be a 'final'
 * list item and 'true' is written to 'islast'. The string representing the
 * list item is written to v, which must be a buffer of size buffsz.

 * NOTES:
 *  - line, v, and islast must not be NULL
 *  - line must be STRIPPED of comment, and leading and trailing whitespace
 */
bool is_list_entry(char *line, char v[], size_t buffsz, bool *islast){
    assert(line);
    char *val_start = NULL, *val_end = NULL;
    bool last_in_list = false;
    say(" ~~ is_list_entry ? : '%s'\n", line);

    /* first char must be from the allowable set */
    if (! *line || !is_allowed(*line, false)) return false;
    val_start = line;

    /* go to the end of value */
    while (*line && is_allowed(*line, false)) ++line;
    val_end = line;

    /* strip any whitespace */
    line = strip_lws(line);

    /* char here must be either NUL or a comma */
    if (*line && *line != ','){
        return false;
    }
    else if (! *line){
        last_in_list = true;
    }else if (*line == ','){
        last_in_list = false;
        ++line;
    }

    /* line should be ending here */
    assert(! *line);

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
 * For each line parsed that is NOT a comment / an empty line
 * / a section title / a list head / a list opening or closing
 * bracket -- the CB callback gets called. Empty lines and
 * comment-only lines are skipped. A line can contain multiple list
 * items: in this case, the callback gets called for each item in
 * the list on that line.
 *
 * On error, the program EXITS with error. Error conditions are
 * not recoverable from. The .ini config file should instead be
 * fixed and made syntactically compliant.
 * The following cause errors:
 *  - lines that exceed the maximum permissible length
 *  - global record lines IFF ALLOW_GLOBAL_RECORDS is false
 *  - empty lists IFF ALLOW_EMPTY_LISTS is false
 *  - malformed list entries
 *  - lines that are not recognized as syntactically correct
 *
 * Beyond these fatal errors, the callback can itself also signal an
 * error condition by returning a non-zero value. If such a value is
 * returned, Cinic_parse will return immediately with the same value.
 *
 * NOTES:
 *  - cb and path must not be NULL
 *  - path must specify the absolute path to an .ini config file
 */
int Cinic_parse(const char *path, config_cb cb){
    assert(path && cb);

    int rc = 0;
    char key[MAX_LINE_LEN]     = {0};
    char val[MAX_LINE_LEN]     = {0};
    char section[MAX_LINE_LEN] = {0};

    enum cinic_list_state list = NOLIST;    /* to assess list state transitions */
    bool islast = false;                    /* final list item */
    uint32_t ln = 0;                        /* line number */
    uint32_t bytes_read = 0;                /* bytes read by getline; 0 on EOF */

    /* allocated and resized by `getline()` as needed */
    char *buff__ = NULL;   // pointer must not change; kept intact for eventual free() call
    char *buff = buff__;   // change this instead
    size_t buffsz = 0;

    FILE *f = fopen(path, "r");
    if (!f){
        fprintf(stderr, "Failed to open file:'%s'\n", path);
        exit(EXIT_FAILURE);
    }

    /* for each line read from config file */
    while ( ( bytes_read = read_line(f, &buff__, &buffsz)) ){
        ++ln;
        buff = buff__;
        say(" ~ read line %u: '%s'", ln, buff);

        /* line too long */
        if(bytes_read > MAX_LINE_LEN){
            cinic_exit_print(CINIC_TOOLONG, ln);
        }

        if (is_empty_line(buff) || is_comment_line(buff) ){
            continue;
        }

        buff = strip_lws(buff);
        strip_comment(buff);
        strip_tws(buff);

        /* section title line */
        if(is_section_line(buff, section, MAX_LINE_LEN)){
            say(" ~ line %u is a section title\n", ln);
            if (list){
                cinic_exit_print(CINIC_NESTED, ln);
            }
        }

        /*  key-value line */
        else if (is_record_line(buff, key, val, MAX_LINE_LEN)){
            say(" ~ line %u is a record line\n", ln);
            if (! *section && !ALLOW_GLOBAL_RECORDS){
                cinic_exit_print(CINIC_NOSECTION, ln);
            }else if (list){
                cinic_exit_print(CINIC_NESTED, ln);
            }
            if ( (rc = cb(ln, list, section, key, val)) ) return rc;
        }

        /* else, try list */
        else {
            say(" ~ trying list parsing on line %u \n", ln);
            char curr_token_buff[MAX_LINE_LEN] = {0};
            char *next_token = buff; /* initialize */
            enum cinic_error cerr;

            while ((next_token = get_list_token(next_token, curr_token_buff, MAX_LINE_LEN))){
                say("---> current token = '%s'\n", curr_token_buff);

                /* list head */
                if(is_list_head(curr_token_buff, key, MAX_LINE_LEN)){
                    if ( (cerr = Cinic_get_list_error(list, LIST_HEAD)) ){
                        cinic_exit_print(cerr, ln);
                    }
                    list = LIST_HEAD;
                    islast = false;
                    continue;
                }

                /* opening bracket */
                else if (is_list_start(curr_token_buff)){
                    if ( (cerr = Cinic_get_list_error(list, LIST_OPEN)) ){
                        cinic_exit_print(cerr, ln);
                    }
                    list = LIST_OPEN;
                    continue;
                }

                /* list entry */
                else if(is_list_entry(curr_token_buff, val, MAX_LINE_LEN, &islast)){
                    if ( (cerr = Cinic_get_list_error(list, islast ? LIST_LAST : LIST_ONGOING)) ){
                        cinic_exit_print(cerr, ln);
                    }
                    list = islast ? LIST_LAST : LIST_ONGOING; /* reset :  */
                }

                /* list end */
                else if(is_list_end(curr_token_buff)){
                    if ( (cerr = Cinic_get_list_error(list, NOLIST)) ){
                        cinic_exit_print(cerr, ln);
                    }
                    list = NOLIST;
                    continue;
                }

                /* not a list component/token recognized as valid */
                /* not any kind of line recognized as valid */
                else{
                    cinic_exit_print(CINIC_MALFORMED, ln);
                }

                if ( (rc = cb(ln, list, section, key, val)) ) return rc;
            } /* while: list token parsing */
        } /* if: try list parsing */
    } /* while getline() */

    fclose(f);
    free(buff__);
    return 0;     /* success */
}

/*
 * Initialize Cinic internal state.
 *
 * There are various internal variables that can be used to customize
 * parsing behavior. See the declaration in cinic.h.
 *
 * Some other variables are not settable through this init function but
 * could be modified manually, including:
 *  - LIST_BRACKET
 */
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
