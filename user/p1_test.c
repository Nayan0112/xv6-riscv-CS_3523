// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"

// //Since there is no sleep for the delay 
// //we are use using a delay 
// const long long DELAY = 1e9;

// int 
// main(void)
// {
//     /**
//      * This is a test file which verifies the 
//      * implementation of all the features men
//      * tioned in p1
//      * A1 -> hello syscall
//      * A2 -> custom implementation of getpid()
//      * B1 -> implementation of getppid()
//      * B2 -> implementation of getnumchild()
//      * C1 -> each processes will store the number of system calls invoked
//      * C2 -> implementation of getsyscall()
//      * C3 -> implementation of getchildsyscount()
//      */

//     printf("Total syscalls invoked : %d\n", getsyscount());
//     int rc = fork();
//     int syscallcount = -2;
//      //int ppid = getpid();
//     if(rc == 0){
//         char* args[] = {};
//         //int cpid = getpid();
//         //printf("[Child]\t Child PID: %d\n", cpid);
//         printf("cpid : %d\nppid : %d\n", getpid(), getppid());
//         if(exec("hello", args) < 0){
//             fprintf(2, "hello failed\n");
//             exit(1);
//         }
//     }else{
//         //just to check if child0 execuited first 
//         //printf("ch\n");
//         //volatile so that it doesnt get optimised and removed
//         volatile long long i;
//         for(i=1; i<=DELAY; i++);
//         syscallcount = getchildsyscount(rc);
//         wait(0);
//         //this should output 0
//         printf("[PID:%d]\tnum of children: %d\n",getpid(), getnumchild());
//         if(!fork()){
//             //child 1
//             for(int i = 0; i < 1e9; i++);
//             exit(0);
//         }else{
//             //parent
//             if(!fork()){
//                 //child  2
//                 for(int i = 0; i < 1e9; i++);
//                 exit(0);
//             }else{
//                 //expecting {0,1,2}
//                 printf("[PID:%d]\tnum of children: %d\n", getpid(), getnumchild());
//                 wait(0);

//             }
//             wait(0);    
//         }
//         //should output 0
//         printf("[PID:%d]\tnum of children: %d\n", getpid(), getnumchild());
//     }
//     //kill(1);
//     //testing kill to check if it returns -1 when the process has no parent 
//     //since any orphan process would be assigned innit as the parent
//     int PID = -2;
//     do{
//         PID = getppid();
//         if(PID == -1){
//             printf("Parent id now INIT\n");
//             break;
//         }
//         printf("Parent PID: %d\n, killing  it ...\n", PID);
//         kill(PID);
//         volatile long long i= 0;
//         for(i = 0; i < DELAY; i++);
//     }while(1);
//     /*
//         observed behaviour is  that the process parent is the shell
//         which gets killed when i try to kill it so init restarts the
//         sh then init adopts the parent process hence getppid returns
//         -1. This is assumed that in the question that no parents mean
//         that process has its parent as init else we can change
//         getppid code such that shell is not considered as 
//         a parent or it doesnt return -1 for init as parent, this is 
//         purely a design choice.
//     */

//     printf("Total syscalls invoked : %d\n", getsyscount());
//     printf("Syscall invoked by child0 : %d\n", syscallcount);

//     if(getpid() == getpid2()) printf("Success! getpid2 working\n");
//     else printf("Getpid2 failed\n");
// }


// #include "kernel/types.h"
// #include "user/user.h"

// int
// main()
// {
//     printf("---- BASIC TEST ----\n");

//     hello();

//     int pid1 = getpid();
//     int pid2 = getpid2();

//     if(pid1 == pid2)
//         printf("getpid2() correct\n");
//     else
//         printf("getpid2() WRONG\n");

//     int ppid = getppid();
//     printf("PID: %d  PPID: %d\n", pid1, ppid);

//     exit(0);
// }


// #include "kernel/types.h"
// #include "user/user.h"
// #define sleep pause
// int
// main()
// {
//     printf("---- CHILD TEST ----\n");

//     //int parent = getpid();

//     int pid1 = fork();
//     if(pid1 == 0){
//         sleep(10);
//         exit(0);
//     }

//     int pid2 = fork();
//     if(pid2 == 0){
//         sleep(20);
//         exit(0);
//     }

//     sleep(5);

//     int n = getnumchild();
//     printf("Number of children (expected 2): %d\n", n);

//     int sc = getchildsyscount(pid1);
//     printf("Child syscall count (valid child): %d\n", sc);

//     int invalid = getchildsyscount(9999);
//     printf("Invalid child syscall (expected -1): %d\n", invalid);

//     wait(0);
//     wait(0);

//     int n2 = getnumchild();
//     printf("Number of children after wait (expected 0): %d\n", n2);

//     exit(0);
// }


// #include "kernel/types.h"
// #include "user/user.h"

// int
// main()
// {
//     printf("---- FORK SYSCALL TEST ----\n");

//     int pid = fork();

//     if(pid == 0){
//         int c = getsyscount();
//         printf("Child syscall count: %d\n", c);
//         exit(0);
//     }
//     else{
//         wait(0);
//         int p = getsyscount();
//         printf("Parent syscall count: %d\n", p);
//     }

//     exit(0);
// }


#include "kernel/types.h"
#include "user/user.h"
#define sleep pause
int
main()
{
    printf("---- SYSCALL COUNT TEST ----\n");

    int before = getsyscount();
    printf("Initial syscall count: %d\n", before);

    // Make known number of syscalls
    getpid();
    getpid();
    sleep(1);
    getpid();

    int after = getsyscount();
    printf("After syscalls: %d\n", after);

    if(after >= before + 4)
        printf("Syscall counter working\n");
    else
        printf("Syscall counter WRONG\n");

    exit(0);
}