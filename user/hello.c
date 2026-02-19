#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char* argv[])
{
    if(argc > 1){
        fprintf(2, "Usage: [no inputs required] prints hello by calling a syscall\n");
        exit(1);
    }
    hello();
    // printf("hello\n");
    exit(0);
}