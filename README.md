# cinic
C library for ini config file parsing with bindings for Lua5.3.

## Overview

The project provides a parser for `.ini`-style config files. The
public interface to the parser is presented in `src/cinic.h`.
The core - and most basic - library is a C library. Bindings are
available for Lua5.3.

Using the parser is different in C and Lua, so details on building the
software for each and using the parser are provided in separate
sections below.

## Make targets for building the software

**make**
 * `clean`  : remove all built artifacts
 * `all`    : (or no argument) build Lua5.3 and C libraries
 * `clib`   : build _only_ the `C` library (`libcinic.so`).
 * `lualib` : build _only_ the `Lua5.3` C module (`cinic.so`).
 * `tests`  : build and run `C` and plain Lua tests
 * `example`: compile example cli program


## C library

The C library gets built as a dynamic library programs can link against.

Using the parser is as simple as:
```C
#include <stdio.h>
#include <stdlib.h>

#include "cinic.h"

int mycb(uint32_t ln, enum cinic_list_state list, const char *section, const char *k, const char *v){
    fprintf(stdout, "called [%u]: [%s], %s=%s, list=%d\n", ln, section, k, v, list);
    return 0;
}

int main(int argc, char **argv){
    if (argc != 2){
        fprintf(stderr, " FATAL : sole argument must be path to a config file to parse\n");
        exit(EXIT_FAILURE);
    }

    Cinic_init(true, false, ".");
    char *path = argv[1];
    int rc = Cinic_parse(path, mycb);
    return rc;
}
```
To build this very example, run `make example`. The binary built has
all code included (no dynamic linking against `libcinic.so` for ease
of use).

As a summary:
 * the `cinic.h` header file is included, which presents the `cinic`
   interface
 * a callback is defined, with the prototype defined in `src/cinic.h`.
   This gets called by the parser on relevant entries in the config
   file.
 * The cinic library is _initialized_. This step is _optional_.
   Defaults are used if no explcit initialization is done. By default,
   global values (key-value pairs that appear before any section) are
   illegal but the above initialization makes it so that they are
   allowed.
 * the parser is called to parse the file found at `path`.

To run this example, you can call it like this:
```sh
./out/example <file>
```
where file can be one of the sample files provided in `sample/` e.g.
```sh
└─$ ./out/example samples/globals.ini
called [3]: [], global1=randomstuff, list=0
called [5]: [], global2=1, list=2
called [6]: [], global2=2, list=2
called [7]: [], global2=3, list=2
called [8]: [], global2=5, list=1
called [10]: [], global3=last, list=0
called [13]: [summary], keycount=3, list=0
called [14]: [summary], notes=this is a multi word line with interspersed whitepace, list=0
```

## Lua library

The lua library is a compiled C module that can be `require`d from lua
scripts.
```lua
local cinic = require("cinic") -- must be found e.g. must be on package.cpath

local inifile = "myinifile"
local parsed = cinic.parse(inifile)
```
The result returned to lua is more convenient and 'batteries-included'
compared to C. The parser returns a table that contains keys and values and
potentially nested tables and/or arrays. The result is always a table
(possibly empty) and never nil. Parsing errors are considered
deal-breakers and an error is thrown, so parsing either succeeds or
fails obviously and the `.ini` config file must be fixed.

Note that the `parse` function has another 2 _optional_ parameters,
other than the mandatory `ini` config file path:
 * a boolean that allows or disallows global entries
 * a one-char string that specifies the section title namespace
   delimiter.

See the comments for `parse_ini_config_file()` in `lua/luacinic.c` for
details and `tests/tests.lua` for examples.


## Parsing behavior and capability

### Parser results

The `C` parser is the core of everything but usage in C is more
primitive than in higher-level languages such as `Lua` (or `C++`).

Specificaly:
 * in C the user must register a _callback_ which gets
   called on relevant entries. It's completely up to the user to decide
   what to do with the values the callback gets called with e.g. print
   them out, add them to a suitable data structure such as a hash table
   or linked list, set flags based on them, etc. This gives the user both
   more freedom but there is also a lot more work to do.
 * Lua comes with a builtin table/dictionary data type. It has more
   batteries included relative to C. The parser in this case does more
   of the work -- and makes more of the decisions -- for the user. The
   parsing results are stored in a table as key-value pairs and nested
   tables as required.

Because a table is returned in `Lua` and table nesting is trivial, Lua
supports _nested sections_. Section titles are of the following form.
```C
[ mysection ]
[ mysection.mysubsection.third ]
```
The `dot` is the _default_ `namespace delimiter`. This can be changed
via the `Cinic_init()` or (`.parse()` in `Lua`) functions.

In C, a section title like `a.b.c` will simply retuns a string:
`a.b.c`. It's up to the user via their registered callback to further
parse this or assign _namespace_ meaning to it. To the parser, it's
simply a string and nothing more.

In `Lua` however, a section title like `a.b.c` is seen as a set of
nested tables i.e. `a["b"]["c"]` or `a.b.c` (equivalent in `Lua`).


### Parsing Errors

If the parser encounters an error, it prints a diagnostic message and
exits the program immediately. Error conditions in other words cannot
be recovered from. This is a design decision: config files are either
correct or they should be made correct. Returning an error code could
possibly be neglected by the user which may then introduce bugs in
programs making use of the parser. It's faster, easier, and more
convenient to fix up a syntactically incorrect config file than to
spend time debugging a C program.

An error is similarly thrown in Lua on parsing failure.

### `.ini` syntax

The parser understands an `.ini` config file that has:
 * **global records**: key-value pairs that precede any section title.
   This is illegal by default but can be made allowable via the init
   function. For the syntax, see `key-value pairs` below.

 * **section titles** of the form `[mysection]`. The section title itself
   must be a contiguous string of characters but otherwise whitespace
   can appear in arbitrary amounts anywhere else on the line.

 * **key-value pairs** ('records') of the form `mykey=mypair`. The key
   must - like the section title - be a contiguous string of
   characters with no intervening whitespace. Whitespace can otherwise
   appear in arbitrary amounts anywhere else on the line. The value
   itself can have whitespace in it.

   In `C`, the callback registered by the user gets called with
   the section title, as well as key and value. If global entries are
   allowed and found in the config file being parsed, the section
   title argument to the callback will be an empty string (`\0`).

 * **comments**: everything following a `;` or `#` is considered a
   comment. Comments can appear anywhere but must come _last_ on the
   line.

 * **lists**. These consist of a key followed by an `=` sign. The key must
   be a contiguous string as described above. The key and the equals sign
   must appear on the same line but can be whitespace separated. The
   list of items per se is the value associated with the key. The list
   starts with an opening bracket, ends with a closing bracket, and
   contains items that are all -- except for the last one -- followed
   by (an arbitrary amount of whitespace and) a `,`.

   The parsing is pretty flexible as to the arrangement of
   the list: it's considered syntactically correct as long as the list
   is opened by an opening bracket and closed by a closing bracket and
   the items are each followed by a comma (but the last item). The
   brackets can appear on their own line or on the same line with
   other items. One or more items can appear on the same line. The
   comma must of course appear on the same line after the item it
   corresponds to.

   The bracket markers used by default are the _square brackets_ :
   `[]`. This can however be customized by manually setting the
   `LIST_BRACKET` variable in `src/cinic.c`.

    In `Lua` lists are represented as _arrays_ i.e. tables with
    numeric indices. In `C`, the user-registered callback gets called
    with the `list` parameter set to `0` for any entry that is not a
    list/part of a list. Otherwise, the `list` param is non-zero, the
    key represents the list key (the list head), and the value is the
    current item in the list. The callback gets called like this for
    each item in the list.

    **see samples/ for a number of .ini config files that are
    understood by the parser**. Here is an example:
```ini
; this contains both single-line as well as multi-line lists
;

comment = lists that are tough to parse

 [ lists ]
 desc = contains single-line and multiline lists
#
#
 [lists.single] # single-line list
 desc = single-line list
 list1 = [FIRST, second, 3, 4, 5th ] ; comment
 list2=  [    FIRST , second    ,3,4 ,5th]; comment
 length = 5

[lists.multi]
list1 = [ ; comment
	first  ,
	second, third,
	fourth
   ] # comment

list2 = [one,two,
        three,
        four,
        five]

list3 = [
        one,two,three
        ]

list4 =[
        one,two,
        three,four   ]
```

The example above is deliberately made ugly and hard to parse as it's
used in a test (see `tests/tests.lua`). Average files are expected to
look much cleaner.

## Todo
[ ] add soname-versioning
[ ] add C++ bindings
