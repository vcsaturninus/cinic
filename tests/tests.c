#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

#include "cinic.h"

static uint32_t tests_run = 0;
static uint32_t tests_passed = 0;
static bool passed = false;

/* defined in src/cinic.c */
extern const char *Cinic_err2str(unsigned int errnum);
bool is_comment(unsigned char c);
uint32_t char_occurs(char c, char *s, bool through_comments);
uint32_t read_line(FILE *f, char **buff, size_t *buffsz);
bool is_section(char *line, char **name);
bool is_empty_line(char *line);
bool is_comment_line(char *line);
bool is_record_line(char *line, char **k, char **v);
bool is_list_head(char *line, char **k);
bool is_list_end(char *line);
bool is_list_entry(char *line, char **v, bool *islast);


#define run_test(f, ...) \
    tests_run++; \
    passed = f(__VA_ARGS__); \
    printf(" * test %i %s\n", tests_run, passed ? "PASSED" : "FAILED !!!"); \
    if (passed) tests_passed++;

/* check that lines consisting of only a comment are correctly recognized */
bool test_comment_line(char *s, bool expected){
    return (is_comment_line(s) == expected);
}

/* check that lines consisting only of whitespace are correctly recognized */
bool test_empty_line(char *s, bool expected){
    return (is_empty_line(s) == expected);
}

/* verify that section names are correctly identified in .ini config files 
 * s must be a char array NOT a read only a char pointer, otherwise the program
 * will segfault as is_section will try to MANGLE the string*/
bool test_section_name(char *str, bool expected, char *expected_name){
    assert(str);
    char s[strlen(str)+1];
    memcpy(s, str, strlen(str));
    s[strlen(str)] = '\0';

    char *ret_name = NULL;
    if (is_section(s, &ret_name) != expected) return false;
    if (expected){
        printf(" --> '%s'\n", ret_name);
        return matches(ret_name, expected_name);
    }
    return true;
}

bool test_kv_line(char *str, bool expected, char *expk, char *expv){
    assert(str);
    char s[strlen(str)+1];
    memcpy(s, str, strlen(str));
    s[strlen(str)] = '\0';

    char *k = NULL, *v = NULL;
    if (is_record_line(s, &k, &v) != expected) return false;
    if (expected){
        printf(" --> k='%s', v='%s'\n", k, v);
        return ( matches(k, expk) && matches(v, expv));
    }
    return true;
}

bool test_list_header(char *str, bool expected, char *expected_name){
    assert(str);
    char s[strlen(str)+1];
    memcpy(s, str, strlen(str));
    s[strlen(str)] = '\0';

    char *ret_name = NULL;
    if (is_list_head(s, &ret_name) != expected) return false;
    if (expected){
        printf(" --> '%s'\n", ret_name);
        return matches(ret_name, expected_name);
    }
    return true;
}

bool test_list_end(char *str, bool expected){
    return (is_list_end(str) == expected);
}

bool test_list_entry(char *str, bool expected, bool islast, char *expv){
    assert(str);
    char s[strlen(str)+1];
    memcpy(s, str, strlen(str));
    s[strlen(str)] = '\0';

    char *v = NULL;
    bool last = false;
    if (is_list_entry(s, &v, &last) != expected) return false;
    if (expected && last != islast) return false;

    if (expected){
        printf(" --> i='%s', islast='%d'\n", v, islast);
        return ( matches(v, expv) );
    }
    return true;
}


int main(int argc, char **argv){
    printf("[ ] Parsing empty lines ... \n");
    run_test(test_empty_line, " ;", false);
    run_test(test_empty_line, "\0", true);
    run_test(test_empty_line, " ", true);
    run_test(test_empty_line, " waf", false);
    run_test(test_empty_line, " .", false);
    run_test(test_empty_line, "# one", false);
    run_test(test_empty_line, "                    ", true);
    run_test(test_empty_line, "\t\n\t    \t\n               ", true);

    printf("[ ] Parsing comment-only lines ... \n");
    run_test(test_comment_line, " ;", true);
    run_test(test_comment_line, " #   ", true);
    run_test(test_comment_line, " # bla blah ;", true);
    run_test(test_comment_line, " ; ;;;", true);
    run_test(test_comment_line, " #;# ;oneaw;;", true);
    run_test(test_comment_line, "   ", false);
    run_test(test_comment_line, " one two three # some", false);
    run_test(test_comment_line, "fdewfw;", false);

    printf("[ ] Parsing section headers ... \n");
    run_test(test_section_name, " ;", false, NULL);
    run_test(test_section_name, " [one two;]", false, NULL);
    run_test(test_section_name, "# [mysection]", false, NULL);
    run_test(test_section_name, "[mysection]", true, "mysection");
    run_test(test_section_name, "    [mysection]  ", true, "mysection");
    run_test(test_section_name, "    [mysection  ] ", true, "mysection");
    run_test(test_section_name, "    [    mysection  ]", true, "mysection");
    run_test(test_section_name, "    [mysection one]", false, NULL);
    run_test(test_section_name, " [  sect.subsect  ]", true, "sect.subsect");
    run_test(test_section_name, " [sect.subsect.subsub.sub4]  # mycomment", true, "sect.subsect.subsub.sub4");
    run_test(test_section_name, " [ my-sec.sub_1.sub_2. ];whatever", true, "my-sec.sub_1.sub_2.");
    run_test(test_section_name, " .[ my-sec.sub_1.sub_2. ];whatever", false, NULL);
    run_test(test_section_name, " [ .my-sec.sub_1- ] ###", true, ".my-sec.sub_1-");
    run_test(test_section_name, "[]", false, NULL);
    run_test(test_section_name, "[ ]", false, NULL);
    run_test(test_section_name, "[.]", true, ".");
    run_test(test_section_name, "[   _ ]", true, "_");
    run_test(test_section_name, "[ .   _ ]", false, NULL);

    printf("[ ] Parsing key=value lines ... \n");
    run_test(test_kv_line, " ;", false, NULL, NULL);
    run_test(test_kv_line, "", false, NULL, NULL);
    run_test(test_kv_line, "= ", false, NULL, NULL);
    run_test(test_kv_line, ".=", false, NULL, NULL);
    run_test(test_kv_line, "===", false, NULL, NULL);
    run_test(test_kv_line, "3=#", false, NULL, NULL);
    run_test(test_kv_line, "# k=v", false, NULL, NULL);
    run_test(test_kv_line, "one=[two] ", false, NULL, NULL);
    run_test(test_kv_line, "one = { ", false, NULL, NULL);
    run_test(test_kv_line, "one=two=three", false, NULL, NULL);
    run_test(test_kv_line, " key = val* ", false, NULL, NULL);
    run_test(test_kv_line, " k=v # ", true, "k", "v");
    run_test(test_kv_line, "one=two", true, "one", "two");
    run_test(test_kv_line, "mykey     =myval # mycomment, k=v", true, "mykey", "myval");
    run_test(test_kv_line, " __key__ = ---val.val.val- ", true, "__key__", "---val.val.val-");
    run_test(test_kv_line, "key1-=-2val ", true, "key1-", "-2val");

    printf("[ ] Parsing list headers ... \n");
    run_test(test_list_header, " ", false, NULL);
    run_test(test_list_header, " # one", false, NULL);
    run_test(test_list_header, " [ ]", false, NULL);
    run_test(test_list_header, "a=[] ", false, NULL);
    run_test(test_list_header, " my_list = {. ", false, NULL);
    run_test(test_list_header, "mylist = [ ",false, NULL);
    run_test(test_list_header, "={", false, NULL);
    run_test(test_list_header, "#mylist={", false, NULL);
    run_test(test_list_header, "mylist={=", false, NULL); 
    run_test(test_list_header, "mylist={", true, "mylist");
    run_test(test_list_header, "mylist={ ; some comment", true, "mylist");
    run_test(test_list_header, "mylist={#{{{", true, "mylist");
    run_test(test_list_header, "mylist ={", true, "mylist");
    run_test(test_list_header, "  mylist      ={  ", true, "mylist");
    run_test(test_list_header, "mylist=  { ", true, "mylist");
    run_test(test_list_header, "my.list- = {", true, "my.list-");
    run_test(test_list_header, "__ = {  ", true, "__");

    printf("[ ] Parsing list closing lines ... \n");
    run_test(test_list_end, " ", false);
    run_test(test_list_end, " # one", false);
    run_test(test_list_end, " # }", false);
    run_test(test_list_end, ";}", false);
    run_test(test_list_end, "a}", false);
    run_test(test_list_end, "----}", false);
    run_test(test_list_end, "}", true);
    run_test(test_list_end, "   }", true);
    run_test(test_list_end, " }      ", true);
    run_test(test_list_end, "} ; some comment", true);
    run_test(test_list_end, "  } # comment", true);

    printf("[ ] Parsing list item lines ... \n");
    run_test(test_list_entry, " ", false, false, NULL);
    run_test(test_list_entry, " # ", false, false, NULL);
    run_test(test_list_entry, "; some comment ", false, false, NULL);
    run_test(test_list_entry, " } ", false, false, NULL);
    run_test(test_list_entry, " {", false, false, NULL);
    run_test(test_list_entry, "[ section ]", false, false, NULL);
    run_test(test_list_entry, ", ", false, false, NULL);
    run_test(test_list_entry, " ,,", false, false, NULL);
    run_test(test_list_entry, ",some", false, false, NULL);
    run_test(test_list_entry, "item ,", false, false, NULL);
    run_test(test_list_entry, "some", true, true, "some");
    run_test(test_list_entry, "item ; ", true, true, "item");
    run_test(test_list_entry, "    item.one.two_three#, ", true, true, "item.one.two_three");
    run_test(test_list_entry, "item, ", true, false, "item");
    run_test(test_list_entry, "--item_,; ", true, false, "--item_");
    run_test(test_list_entry, "item___,    ", true, false, "item___");

    printf("Passed: %u of %u\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

