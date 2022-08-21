#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "cinic.h"

int mycb(uint32_t ln, enum cinic_list_state list, const char *section, const char *k, const char *v){
    fprintf(stdout, "[%u]: [%s], %s=%s, list=%d\n", ln, section, k, v, list); 
    return 0;
}

int main(int argc, char **argv){
    Cinic_init(false, false);
    Cinic_parse("/home/vcsaturninus/common/repos/others/cinic/tests/what.ini", mycb);
}
