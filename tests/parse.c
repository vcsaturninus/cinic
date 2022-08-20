#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "cinic.h"

int mycb(uint32_t ln, const char *section, const char *k, const char *v, int errnum){
    fprintf(stdout, "[%u]: [%s], %s=%s\n", ln, section, k, v); 
    return 0;
}

int main(int argc, char **argv){
    Cinic_init(false);
    Cinic_parse("/home/vcsaturninus/common/repos/others/cinic/tests/what.ini", mycb);
}
