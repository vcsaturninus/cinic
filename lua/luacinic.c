#include <lua5.3/lua.h>
#include <lua5.3/lualib.h>
#include <lua5.3/lauxlib.h>

#include <stdio.h>
#include <assert.h>
#include <sys/types.h>  /* ssize_t */
#include <ctype.h>      /* ASCII char type functions */
#include <inttypes.h>   /* PRIu32 etc */
#include <string.h>     /* memset(), strcmp() */

#include "cinic.h"
#include "utils__.h"

extern int ALLOW_EMPTY_LISTS;
extern int ALLOW_GLOBAL_RECORDS;

void dispatch_lua_error(lua_State *L, enum cinic_error error, uint32_t ln){
    assert(error < CINIC_SENTINEL && error > CINIC_SUCCESS);
    luaL_error(L, "Cinic: failed to parse line %u -- %s\n", ln, Cinic_err2str(error));
}

/* get t[k] and leave it on top of the stack.
 * this table is crated if it does not exist */
void get_or_create(lua_State *L, char *k){
    //printf("top=%u, type=%d, string=%d\n", lua_gettop(L), lua_type(L, 1), LUA_TSTRING);
    printf("called with %s\n", k);
    if (lua_getfield(L, -1, k) != LUA_TTABLE){
        lua_remove(L, -1);
        lua_newtable(L);
        lua_setfield(L, -2, k);
        lua_getfield(L, -1, k);
    }
    printf("top=%u, type=%d, string=%d, table=%d\n", lua_gettop(L), lua_type(L, -1), LUA_TSTRING, LUA_TTABLE);
}

int populate_lua_state(lua_State *L, 
                       uint32_t ln,
                       enum cinic_list_state list,
                       char *section,
                       char *k,
                       char *v
                       )
{
    printf("1, section=%s, k=%s, v=%s\n", section, k, v);
    static LUA_INTEGER idx = 1;

    /* make copy of section title because strtok mangles it */
    uint32_t len = strlen(section)+1;
    char sect[len];
    memset(sect, 0, len);
    memcpy(sect, section, len);

    char *s = strtok(sect, ".");
    while (s){
        printf("s is %s\n", s);
        get_or_create(L, s);
        s = strtok(NULL, ".");
    }

    /* record mode, k=v pair in an already-started section */
    if (!list){
        lua_pushstring(L, v);
        lua_setfield(L, -2, k);
    }
    /* start of a list/array */
    else if(list == LIST_START){
        idx = 1;
        lua_newtable(L);
        lua_setfield(L, -2, k);
    }
    else if(list == LIST_ONGOING || list == LIST_END){
        if (lua_getfield(L, -1, k) != LUA_TTABLE){
            luaL_error(L, "expected table value for key '%s', got something else", k);
        }
        lua_pushinteger(L, idx);
        lua_pushstring(L, v);
        lua_settable(L, -3);
        ++idx;
    }
    else{
        luaL_error(L, "internal logic error when parsing list");
    }

    lua_settop(L,1); /* only leave the outermost table */
    return 0;
}

int parse_ini_config_file(lua_State *L){
    /* get path to config file to parse */
    lua_settop(L, 1);   /* path is the only argument */
    char *path = NULL;

    const char *arg = luaL_checkstring(L, 1);
    if (! (path = calloc(1, strlen(arg) + 1)) ){
        luaL_error(L, "Memory allocation error (calloc())");
    }
    memcpy(path, arg, strlen(arg));

    /* parser state */
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
		luaL_error(L, "Failed to open file:'%s'", path);
	}

    /* start new lua table to hold the config data */
    lua_settop(L, 0);
    lua_newtable(L);

	while ( ( bytes_read = read_line(f, &buff, &buffsz)) ){
        printf("==========================%s=======================\n", section);
		++ln;

        if(bytes_read > MAX_LINE_LEN){
            dispatch_lua_error(L, CINIC_TOOLONG, ln);
        }
        else if (is_empty_line(buff) || is_comment_line(buff)){
            continue;
        }
        else if(is_section(buff, section, MAX_LINE_LEN)){
            if (list){
                dispatch_lua_error(L, CINIC_NESTED, ln);
            }
            continue;
        }
        else if (is_record_line(buff, key, val, MAX_LINE_LEN)){
            if (! *section && !ALLOW_GLOBAL_RECORDS){
                dispatch_lua_error(L, CINIC_NOSECTION, ln);
            }else if (list){
                dispatch_lua_error(L, CINIC_NESTED, ln);
            }
        }
        else if(is_list_head(buff, key, MAX_LINE_LEN)){
            list = LIST_START;
            islast = false;
        }
        else if(is_list_entry(buff, val, MAX_LINE_LEN, &islast)){
            if (!list){
                dispatch_lua_error(L, CINIC_NOLIST, ln);
            }else if(list == LIST_END){
                dispatch_lua_error(L, CINIC_MISSING_COMMA, ln);
            }

            if (islast){
                list = LIST_END ; /* reset :  */
            }else{
                list = LIST_ONGOING;  /* regular entry in list */
            }
        }
        else if(is_list_end(buff)){
            if (list == LIST_ONGOING){
                dispatch_lua_error(L, CINIC_REDUNDANT_COMMA, ln);
            }
            else if(list == NOLIST){
                dispatch_lua_error(L, CINIC_REDUNDANT_BRACE, ln);
            }
            else if(list == LIST_START && !ALLOW_EMPTY_LISTS){
                dispatch_lua_error(L, CINIC_EMPTY_LIST, ln);
            }
            else if(list == LIST_END){
                list = NOLIST;
            }
            continue;
        }
        else{
            dispatch_lua_error(L, CINIC_MALFORMED, ln);
        }
        rc = populate_lua_state(L, ln, list, section, key, val);
        if (rc) return rc;    
	}

    fclose(f);  /* fopen resources */
    free(buff); /* getline buffer */
	return 1;   /* table holding config data */
}


/* Module functions */
const struct luaL_Reg cinic[] = {
    {"parse", parse_ini_config_file},
    {NULL, NULL}
};

/* Open/initialize module */
int luaopen_libluacinic(lua_State *L){
    luaL_newlib(L, cinic);
    return 1;
}
