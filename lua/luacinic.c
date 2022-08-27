#include <lua5.3/lua.h>
#include <lua5.3/lualib.h>
#include <lua5.3/lauxlib.h>

#include <stdbool.h>
#include <stdlib.h>     /* free() */
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>  /* ssize_t */
#include <string.h>     /* memset(), strcmp() */

#include "cinic.h"
#include "utils__.h"

/*
 * Convert cinic error number to error string and use that to throw an error in Lua */
void dispatch_lua_error(lua_State *L, enum cinic_error error, uint32_t ln){
    assert(error < CINIC_SENTINEL && error > CINIC_SUCCESS);
    luaL_error(L, "Cinic: failed to parse line %d -- %s\n", ln, Cinic_err2str(error));
}

/*
 * get t[k] or create t and assign t[k] = <empty table> if it doesn't exist.
 *
 * This is used to easily create nested tables to represent nested
 * section namespaces. For example,the [a.b.c] .ini section title would be
 * represented as a["b"]["c"] (or a.b.c -- using dot syntax in Lua), where
 * all of a, b, and c are tables.
 *
 * This function therefore expects the outermost table to already be
 * on the stack. It then tries to get the nested table with key K and
 * if it doesn't exist (or K is associated with something other than
 * a table) it creates it.
 *
 * The nested table retrieved (already exists) or created (didn't already
 * exist) is always left on top of the stack.
 */
void get_or_create(lua_State *L, char *k){
    /* t[k] does not exist or is not a table */
    if (lua_getfield(L, -1, k) != LUA_TTABLE){
        lua_remove(L, -1);  /* pop what we got off the stack -- not a table */
        lua_newtable(L);    /* create new table */
        lua_setfield(L, -2, k); /* assign it to t[k] */
        lua_getfield(L, -1, k); /* and leave it on top of the stack */
    }
    /* else t[k] already exists and is a table */
}

/*
 * Callback to be called by parse_ini_config_file() on relevant entries.
 *
 * The callback manipulates the lua stack such that tables are created
 * and populated to reflect the parsed .ini config file.
 *
 * The prototype of this function is the same as that of a normal callback
 * called by Cinic_parse(). See config_cb in cinic.h FMI.
 *
 * At the end, only the outermost table is left on the stack; the next
 * call to this function repopulates to stack with the (already-created)
 * nested tables or/and creates new nested tables as required.
 */
int populate_lua_state(lua_State *L,
                       uint32_t ln,
                       enum cinic_list_state list,
                       char *section,
                       char *k,
                       char *v
                       )
{
    say(" ~ populating lua state with (list = %i) section='%s', k='%s', v='%s'\n", list, section, k, v);

    /* .ini lists are represented as arrays ie using numeric indices */
    static LUA_INTEGER idx = 1;

    /* make copy of section title because strtok mangles it */
    uint32_t len = strlen(section)+1;
    char sect[len];
    memset(sect, 0, len);
    memcpy(sect, section, len);

    /* create as needed nested lua tables to represent all the namespaces denoted
     * in the .ini section title and place the innermost table on top of the stack */
    char *s = strtok(sect, SECTION_NS_SEP);
    while (s){
        get_or_create(L, s);
        s = strtok(NULL, SECTION_NS_SEP);
    }

    /* k=v pair (aka a record) in an already-started section */
    if (!list){
        lua_pushstring(L, v);
        lua_setfield(L, -2, k);
    }
    /* start of a list/array */
    else if(list == LIST_HEAD){
        idx = 1;   /* use numeric indices for arrays */
        lua_newtable(L);
        lua_setfield(L, -2, k);
    }
    else if(list == LIST_ONGOING || list == LIST_LAST){
        if (lua_getfield(L, -1, k) != LUA_TTABLE){ /* get array nested in outermost section table */
            luaL_error(L, "expected table value (array) for key '%s', got something else", k);
        }
        lua_pushinteger(L, idx);
        lua_pushstring(L, v);
        lua_settable(L, -3);
        ++idx;
    }
    else{
        luaL_error(L, "internal logic error when parsing list");
    }

    lua_settop(L,1); /* only leave the outermost parent table */
    return 0;
}

/*
 * Parse the specified .ini config file and return a lua table.
 *
 * <-- path, @lua; <string>
 *     Path to .ini file to parse
 *
 * <-- allow_globals, @lua; <bool>
 *     If true, global entries (entries that precede any section
 *     declarations) are considered legal, where they would
 *     otherwise produce an error by default.
 *
 * <-- section_delim, @lua; <char>
 *     The namespace separator to use in section titles. DOT ('.')
 *     is the default.
 *
 * Note that for lua other initialization is done implicitly rather
 * than being left up to the user:
 *  * empty lists are allowed
 *
 *  See Cinic_parse() in cinic.c for comments on the variables used
 *  in this function. This function is essentially equivalent to
 *  Cinic_parse(), but serves to interface with Lua.
 */
int parse_ini_config_file(lua_State *L){
    /* get lua params */
    lua_settop(L, 3);
    char *path = NULL;

    /* make local copy as lua's string may be garbage collected */
    const char *arg = luaL_checkstring(L, 1);
    if (! (path = calloc(1, strlen(arg) + 1)) ){
        luaL_error(L, "Memory allocation error (calloc())");
    }
    memcpy(path, arg, strlen(arg));

    /* defaults if unspecified by the caller */
    bool allow_globals      = false;
    bool allow_empty_lists  = true;  /* implcitly allow this for lua */
    const char *ns_delim    = ".";

    /* check initialization flags */
    if (lua_type(L, 2) != LUA_TNIL){
        luaL_checktype(L, 2, LUA_TBOOLEAN);
        allow_globals = lua_toboolean(L,2);
    }
    if (lua_type(L, 3) != LUA_TNONE){
        const char *delim = luaL_checkstring(L, 3);
        /* must be a single char! */
        if (strlen(delim) > 1){
            luaL_error(L, "Invalid delimiter provided: '%s' -- must be a single char", delim);
        }
        ns_delim = delim;
    }
    /* initialize cinic parser */
    Cinic_init(allow_globals, allow_empty_lists, ns_delim);

    /* parser state */
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
		luaL_error(L, "Failed to open file:'%s'", path);
	}

    /* start new lua table to hold the config data; returned at the end */
    lua_settop(L, 0);
    lua_newtable(L);

    /* for each line read from config file */
    while ( ( bytes_read = read_line(f, &buff__, &buffsz)) ){
        ++ln;
        buff = buff__;
        say(" ~ read line %u: '%s'", ln, buff);

        /* line too long */
        if(bytes_read > MAX_LINE_LEN){
            dispatch_lua_error(L, CINIC_TOOLONG, ln);
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
                dispatch_lua_error(L, CINIC_NESTED, ln);
            }
        }

        /*  key-value line */
        else if (is_record_line(buff, key, val, MAX_LINE_LEN)){
            say(" ~ line %u is a record line\n", ln);
            if (! *section && !ALLOW_GLOBAL_RECORDS){
                dispatch_lua_error(L, CINIC_NOSECTION, ln);
            }else if (list){
                dispatch_lua_error(L, CINIC_NESTED, ln);
            }
			if ( (rc = populate_lua_state(L, ln, list, section, key, val)) ) return rc;
        }

        /* else, try list */
		else {
            say(" ~ trying list parsing on line %u\n", ln);
			char curr_token_buff[MAX_LINE_LEN] = {0};
			char *next_token = buff; /* initialize */
            enum cinic_error cerr;

			while ((next_token = get_list_token(next_token, curr_token_buff, MAX_LINE_LEN))){
                say("---> current token = '%s'\n", curr_token_buff);

                /* list head */
				if(is_list_head(curr_token_buff, key, MAX_LINE_LEN)){
                    if ( (cerr = Cinic_get_list_error(list, LIST_HEAD)) ){
                        dispatch_lua_error(L, cerr, ln);
                    }
					list = LIST_HEAD;
					islast = false;
				}

                /* opening bracket */
                else if (is_list_start(curr_token_buff)){
                    if ( (cerr = Cinic_get_list_error(list, LIST_OPEN)) ){
                        dispatch_lua_error(L, cerr, ln);
                    }
                    list = LIST_OPEN;
                    continue;
                }

                /* list entry */
				else if(is_list_entry(curr_token_buff, val, MAX_LINE_LEN, &islast)){
                    if ( (cerr = Cinic_get_list_error(list, islast ? LIST_LAST : LIST_ONGOING)) ){
                        dispatch_lua_error(L, cerr, ln);
                    }
					list = islast ? LIST_LAST : LIST_ONGOING; /* reset :  */
				}

                /* list end */
				else if(is_list_end(curr_token_buff)){
                    if ( (cerr = Cinic_get_list_error(list, NOLIST)) ){
                        dispatch_lua_error(L, cerr, ln);
                    }
					list = NOLIST;
					continue;
				}

                /* not a list component/token recognized as valid */
                /* not any kind of line recognized as valid */
				else{
                    dispatch_lua_error(L, CINIC_MALFORMED, ln);
				}

				if ( (rc = populate_lua_state(L, ln, list, section, key, val)) ) return rc;
			} /* while: list token parsing */
		} /* if: try list parsing */
    } /* while getline() */

    fclose(f);
    free(buff__);
    return 1;   /* success */
}


/* Module functions */
const struct luaL_Reg cinic[] = {
    {"parse", parse_ini_config_file},
    {NULL, NULL}
};

/* Open/initialize module */
int luaopen_cinic(lua_State *L){
    luaL_newlib(L, cinic);
    return 1;
}

