#!/usr/bin/lua5.3


package.cpath = package.cpath .. ";../out/?.so;./out/?.so;"

local cinic = require("cinic")
local motab = require("motab")


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
        keycount = "3"
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

-- deep compare two tables each with an arbitrary number of nested tables
local function deep_compare(a,b)
    if type(a) ~= "table" or type(b) ~= "table" then
        return false
    end
    if #a ~= #b then return false end

    -- is a superset of b
    for k,v in pairs(a) do
        if type(v) == "table" then
            if not M.deep_compare(v, b[k]) then
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
            if not M.deep_compare(v, a[k]) then
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
    local file = "./samples/" .. file
    local actual = cinic.parse(file, allow_globals, sep)

    tests_run = tests_run+1
    local passed = deep_compare(expected, actual)
    if passed then
        tests_passed = tests_passed + 1
        print(string.format(" ~ Test %s passed", tests_run))
    else
        print(string.format(" ~ Test %s FAILED !! ", tests_run))
        print(motab.ptable(expected, 3, "expected"))
        print(motab.ptable(actual, 3, "actual"))
        print("here")
    end
end

--===============================================================
--
run(empty, "empty.ini")
run(flat, "flat.ini")
run(nested, "nested.ini")
run(lists, "lists.ini")
run(globals, "globals.ini", true)
run(ns_delim, "ns_delim.ini", false, "/")


print(string.format("Tests passed: %s of %s", tests_passed, tests_run))
if tests_passed ~= tests_run then os.exit(3) end
