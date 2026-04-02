#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void print_stats(int pid){
    struct mlfqinfo info;
    if (getmlfqinfo(pid, &info) == 0){
        printf("\n--- PID:%d STATS ---\n", pid);
        printf("Priority Level: %d\n", info.level);
        printf("Times Scheduled: %d\n", info.times_scheduled);
        printf("Ticks at Levels: [%d, %d, %d, %d]\n", 
               info.ticks[0], info.ticks[1], info.ticks[2], info.ticks[3]);
    } else{
        printf("Could not get info for PID %d\n", pid);
    }
}

int main(void){
    volatile long int t2 = (long)8 * 1e9;
    volatile long int t1 = 1e5;

    if (fork() == 0){
        while (t1--){
            getsyscount();
        }
        print_stats(getpid());
        exit(0);
    }
    for(int i = 0; i < 5; i++){
        fork(); 
    }

    while (t2--){
    }

    int my_pid = getpid();
    if (my_pid > 4){
        print_stats(my_pid);
        exit(0);
    }

    for (int i = 0; i < 32; i++){
        wait(0);
    }

    printf("\n[FINAL PARENT STATS]");
    print_stats(getpid());

    exit(0);
}


// void cpu_bound()
// {
//     for(volatile int i = 0; i < 100000000; i++);
//     printf("[CPU] PID %d Level: %d\n", getpid(), getlevel());
// }

// void syscall_heavy()
// {
//     for(volatile int i = 0; i < 2000; i++){
//         getpid();
//     }
//     printf("[SYSCALL] PID %d Level: %d\n", getpid(), getlevel());
// }

// void mixed()
// {
//     for(volatile int i = 0; i < 500; i++){
//         for(int j = 0; j < 10000; j++);
//         getpid();
//     }
//     printf("[MIXED] PID %d Level: %d\n", getpid(), getlevel());
// }

// int
// mai1()
// {
//     printf("=== Test 1: Core Scheduling Behavior ===\n");

//     printf("Initial Level (parent): %d\n", getlevel());

//     if(fork() == 0){
//         cpu_bound();
//         exit(0);
//     }

//     if(fork() == 0){
//         syscall_heavy();
//         exit(0);
//     }

//     if(fork() == 0){
//         mixed();
//         exit(0);
//     }

//     for(int i = 0; i < 3; i++)
//         wait(0);

//     return 0;
// }

// int
// mai2()
// {
//     printf("=== Test 2: Priority Boost & Fairness ===\n");

//     for(int i = 0; i < 3; i++){
//         if(fork() == 0){
//             // Make them CPU heavy so they get demoted
//             for(volatile int j = 0; j < 150000000; j++);

//             int lvl_before = getlevel();
//             printf("Child %d level BEFORE boost: %d\n", getpid(), lvl_before);

//             // Wait for boost (using pause)
//             for(volatile int k = 0; k < 200; k++){
//                 pause(1);
//             }

//             int lvl_after = getlevel();
//             printf("Child %d level AFTER boost: %d\n", getpid(), lvl_after);

//             if(lvl_after != 0){
//                 printf("ERROR: Boost failed for PID %d\n", getpid());
//             }

//             exit(0);
//         }
//     }

//     for(int i = 0; i < 3; i++)
//         wait(0);

//     return 0;;
// }

// struct mlfqinfo info;

// int
// mai3()
// {
//     printf("=== Test 3: getmlfqinfo + Edge Cases ===\n");

//     int pid = getpid();

//     // Generate activity
//     for(volatile int i = 0; i < 1000; i++){
//         getpid(); // syscalls
//         for(volatile int j = 0; j < 10000; j++);
//     }

//     if(getmlfqinfo(pid, &info) < 0){
//         printf("ERROR: getmlfqinfo failed\n");
//         exit(1);
//     }

//     printf("Level: %d\n", info.level);
//     printf("Times scheduled: %d\n", info.times_scheduled);
//     printf("Total syscalls: %d\n", info.total_syscalls);

//     for(int i = 0; i < 4; i++){
//         printf("Ticks[%d]: %d\n", i, info.ticks[i]);
//     }

//     // Basic sanity checks
//     if(info.total_syscalls == 0){
//         printf("ERROR: syscall count not tracked\n");
//     }

//     // Invalid PID test
//     if(getmlfqinfo(99999, &info) != -1){
//         printf("ERROR: invalid PID not handled\n");
//     } else {
//         printf("Invalid PID handled correctly\n");
//     }

//     return 0;;
// }



// int main(){
//     mai1();
//     mai2();
//     mai3();
//     return 0;
// }