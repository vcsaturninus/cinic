#ifndef CINIC_H__
#define CINIC_H__

#include <stdint.h>
#include <stdbool.h>

/* ===============================================================================*\
 |  BSD 2-Clause License                                                           |
 |                                                                                 |
 |  Copyright (c) 2022, vcsaturninus -- vcsaturninus@protonmail.com                |
 |  All rights reserved.                                                           |
 |                                                                                 |
 |  Redistribution and use in source and binary forms, with or without             |
 |  modification, are permitted provided that the following conditions are met:    |
 |                                                                                 |
 |  1. Redistributions of source code must retain the above copyright notice, this |
 |     list of conditions and the following disclaimer.                            |
 |                                                                                 |
 |  2. Redistributions in binary form must reproduce the above copyright notice,   |
 |     this list of conditions and the following disclaimer in the documentation   |
 |     and/or other materials provided with the distribution.                      |
 |                                                                                 |
 |  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"    |
 |  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE      |
 |  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE |
 |  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE   |
 |  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL     |
 |  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR     |
 |  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER     |
 |  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,  |
 |  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE  |
 |  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.           |
 \*===============================================================================*/


/* compare 2 strings - a and b; ret=true if equal */
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
 * of a list sets LIST_HEAD, the opening bracket sets LIST_OPEN, all susequent
 * items BUT THE LAST set LIST_ONGOING, and the last item sets LIST_LAST.
 * Finally, the closing brace resets back to NOLIST.
 */
enum cinic_list_state{
    LIST_HEAD    = 5,   /* current line/entry is the head (key) of a list */
    LIST_OPEN    = 4,   /* current line/entry is an opening bracket for a list */
    LIST_ONGOING = 2,   /* current line/entry is a regular entry in a list */
    LIST_LAST    = 1,   /* current line/entry is the last item in a list */
    NOLIST       = 0    /* current line/entry is a closing bracket for a list */
};

/* Cinic error numbers;
 * also see cinic_error_strings in cinic.c */
enum cinic_error{
    CINIC_SUCCESS = 0,  /* no error */
    CINIC_NOSECTION,
    CINIC_MALFORMED,
    CINIC_MALFORMED_LIST,
    CINIC_TOOLONG,
    CINIC_NESTED,
    CINIC_NOLIST,
    CINIC_EMPTY_LIST,
    CINIC_MISSING_COMMA,
    CINIC_REDUNDANT_COMMA,
    CINIC_REDUNDANT_BRACKET,
    CINIC_LIST_NOT_STARTED,
    CINIC_LIST_NOT_ENDED,
    CINIC_SENTINEL        /* max index in cinic_error_strings */
};

/*
 * Callback to be called by Cinic_parse (and Cinic_parse_string)
 * on every .ini config line being parsed. The callback will
 * get called with the arguments shown below.
 *
 * Note error conditions cause the program to exit with an error/diagnostic
 * message. The callback does NOT get to recover from errors or ignore them.
 */
typedef
int (* config_cb)(
        uint32_t ln,                /* line number in the config file, starting from 1 */
        enum cinic_list_state list, /* used for dealing with lists; see `enum cinic_list_state` */
        const char *section,        /* ini config section */
        const char *k,              /* section key (if !list) / list name (if list > 0) */
        const char *v               /* section value (if !list) / list entry (if list > 0) */
        );

/*
 * Parse the .ini config file specified by path.
 *
 * The parsing is carried out line by line and for each
 * relevant entry config_cb is called.
 *  - Empty lines and lines containing only a comment are skipped.
 *  - lines not understood result in a fatal error. The callback
 *    does not get to ignore or recover from such errors. Instead,
 *    the config line should be fixed to be syntactically
 *    correct/understood.
 */
int Cinic_parse(
        const char *path,    /* path to .ini config file */
        config_cb cb
        );

/*
 * Initialize various internal Cinic variables.
 *
 * These allow customization of the parser as described
 * in the comments.
 */
void Cinic_init(bool allow_globals,       /* consider key-value pairs that appear before any section title legal */
                bool allow_empty_lists,   /* consider lists without any list items to be legal */
                const char *section_delim /* char that represents section nesting e.g. a.b.c; see SECTION_NS_SEP in cinic.c */
        );

#endif




