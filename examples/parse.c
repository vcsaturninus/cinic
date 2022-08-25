#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "cinic.h"
#include "utils__.h"

/* NOTE
 * gcc -I../src/ ../src/cinic.c parse.c  -o q */

int mycb(uint32_t ln, enum cinic_list_state list, const char *section, const char *k, const char *v){
    fprintf(stdout, "called [%u]: [%s], %s=%s, list=%d\n", ln, section, k, v, list);
    return 0;
}

int main(int argc, char **argv){
    Cinic_init(false, false, ".");
    Cinic_parse("/home/vcsaturninus/common/repos/others/cinic/samples/single_line_list.ini", mycb);

#if 0
    //char test[] = "mylist = [1,2,3,4]";
    char test[] = "mylist = [ one, two , three   , four  ] ";
    char buff[1024] = {0};
    char *next = test;
    while (( next = get_list_token(next, buff, 1024)) ){
        printf("got '%s', left: '%s'\n", buff, next);
        if (is_list_head(buff, NULL, 0)){
            puts("it is head");
        }
        else if (is_list_end(buff)){
            puts("it is end");
        }
        else if(is_list_entry(buff, NULL, 0, NULL)){
            puts("it is entry");
        }
    }
    printf("got '%s', left: '%s'\n", buff, next);
    if (is_list_end(buff)){
        puts("it is end");
    }
#endif
}
