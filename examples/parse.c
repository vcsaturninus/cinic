#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
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
