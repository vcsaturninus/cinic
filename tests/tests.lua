#!/usr/bin/lua5.3


package.cpath = package.cpath .. ";../out/?.so;./out/?.so;"

local cinic = require("cinic")

local tests_run    = 0
local tests_passed = 0

-- Below is a list of tables manually created, known to be correct
-- representations of input files in samples/; these represent the
-- expected output, meant to be compared with the actual output returned
-- by the parser. Each table should have the same name as its corresponding
-- file in samples/
local empty = {}
local flat = {
    ["sect_1"] = {
        first = "1st",
        second = "2nd"
    },
    ["sect-2"] = {
        third = "3rd",
        fourth = "4th"
    }
}
local nested = {
    top = {
        toplevel = "true",
        sub = {
            nesting_level = "2",
            id = "123131",
            sub = {
                id = "23132131",
                nesting_level = "3"
            }
        },
        sub2 = {
            nesting_level = "2",
            id = "erfe2fewfw"
        }
    },
    notes = {
        toplevel = "true",
        extra = {
            count = "0"
        }
    }
}
local lists = {
    app = {
        name = "monitor",
        path = "/usr/sbin/monit"
    },
    ports = {
        ports = {
            "3000",
            "4000",
            "4832",
            "6666",
            "5221"
        },
        main = "3000",
        proto = "tcp"
    },
    paths = {
        home = "/home/dev",
        bin = "/home/dev/local/bin",
        user = {
            username = "dev",
            uids = {
                "0",
                "11",
                "1000",
                "45626"
            }
        }
    }
}
local globals = {
    global1 = "randomstuff",
    global2 = {"1","2","3","5"},
    global3 = "last",
    summary = {
        keycount = "3",
        notes = "this is a multi word line with interspersed whitepace"
    }
}
local ns_delim = {
    top = {
        id = "1",
        sub1 = {
            id = "2"
        },
        sub2 = {
            id = "3",
            status = "closed",
            ["2"] = {
                status = "new",
                id = "231232131"
            }
        }
    },
    root = {
        home = {
            path = "/root"
        }
    }
}
local lists_from_hell = {
    comment = "lists that are tough to parse",
    lists = {
        desc = "contains single-line and multiline lists",
        single = {
           desc = "single-line list",
           list1 = {
                "FIRST",
                "second",
                "3",
                "4",
                "5th"
           },
           list2 = {
            "FIRST",
            "second",
            "3",
            "4",
            "5th"
           },
           length = "5"
        },
        multi = {
            list1 = {
                "first",
                "second",
                "third",
                "fourth"
            },
            list2 = {
                "one",
                "two",
                "three",
                "four",
                "five"
            },
            list3 = {"one", "two", "three"},
            list4={"one","two","three","four"}
        }
    }
}

-- deep compare two tables each with an arbitrary number of nested tables
local function deep_compare(a,b)
    if type(a) ~= "table" or type(b) ~= "table" then
        return false
    end
    if #a ~= #b then return false end

    -- is a superset of b
    for k,v in pairs(a) do
        if type(v) == "table" then
            if not deep_compare(v, b[k]) then
                return false
            end
        else
            if b[k] ~= v then
                return false
            end
        end
    end

    -- is b a superset of a
    for k,v in pairs(b) do
        if type(v) == "table" then
            if not deep_compare(v, a[k]) then
                return false
            end
        else
            if a[k] ~= v then
                return false
            end
        end
    end

    -- equivalent sets
    return true
end


-- parse file and compare the result with EXPECTED;
-- if different, the test failed.
local function run(expected, file, allow_globals, sep)
    assert(expected and file)
    local sep = sep and sep or '.'
    local globals = allow_globals or false
    local file = "./samples/" .. file
    local actual = cinic.parse(file, globals, sep)

    tests_run = tests_run+1
    local passed = deep_compare(expected, actual)
    if passed then
        tests_passed = tests_passed + 1
        print(string.format(" ~ Test %s passed  -- %s", tests_run, file))
    else
        print(string.format(" ~ Test %s FAILED !!  -- %s", tests_run, file))
    end
end

--===============================================================
--
print(" ~~~~ Running Lua tests ~~~~ ")
run(empty, "empty.ini")
run(flat, "flat.ini")
run(nested, "nested.ini")
run(lists, "lists.ini")
run(globals, "globals.ini", true)
run(ns_delim, "ns_delim.ini", false, "/")
run(lists_from_hell, "lists_from_hell.ini", true)


print(string.format("Tests passed: %s of %s", tests_passed, tests_run))
if tests_passed ~= tests_run then os.exit(3) end
