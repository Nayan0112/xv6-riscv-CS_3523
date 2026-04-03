#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define sbrk sbrklazy

struct vmstats {
  int page_faults;
  int page_evicted;
  int pages_swapped_in;
  int pages_swapped_out;
  int resident_pages;
};

#define PGSIZE 4096
#define TEST_PAGES 2048

void
print_vmstats(int pid, char *label)
{
  struct vmstats info;
  if(getvmstats(pid, &info) < 0) return;
  printf("\n--- %s (PID: %d) ---\n", label, pid);
  printf("  Res: %d | Faults: %d | Evicted: %d | Out: %d | In: %d\n", 
          info.resident_pages, info.page_faults, info.page_evicted,
          info.pages_swapped_out, info.pages_swapped_in);
}

int
main(int argc, char *argv[])
{
  char *buffer;

  // Fork to stress the memory/swap system with two processes concurrently
  int child_pid = -1;
  //child_pid = fork();
  int pid = getpid();

  // 1. Allocate space
  buffer = sbrk(TEST_PAGES * PGSIZE);
  if(buffer == (char*)-1) {
    printf("PID %d: sbrk failed\n", pid);
    exit(1);
  }

  // 2. WRITE initial unique data to each page
  // This triggers Page Faults -> vmfault -> kalloc
  printf("PID %d: Step 1 - Writing initial patterns to %d pages...\n", pid, TEST_PAGES);
  for(int i = 0; i < TEST_PAGES; i++){
    buffer[i * PGSIZE] = (char)(i % 256);
    buffer[i * PGSIZE + PGSIZE - 1] = (char)(0xFF - (i % 256));
  }

  print_vmstats(pid, "AFTER FIRST WRITE (Should see Evictions if RAM/Swap is tight)");

  // 3. READ and VERIFY initial data
  // This triggers Page Faults -> vmfault -> evict -> swap_in
  for(int i = 0; i < TEST_PAGES; i++){
    char expected_head = (char)(i % 256);
    char expected_tail = (char)(0xFF - (i % 256));
    
    if(buffer[i * PGSIZE] != expected_head || buffer[i * PGSIZE + PGSIZE - 1] != expected_tail){
      printf("CRITICAL ERROR PID %d: Data corruption at page %d during first read!\n", pid, i);
      printf("Expected [%d, %d], Got [%d, %d]\n", 
              expected_head, expected_tail, buffer[i * PGSIZE], buffer[i * PGSIZE + PGSIZE - 1]);
      exit(1);
    }
  }

  // 4. WRITE AGAIN: Modify data to check integrity after a read/write cycle
  // This tests if swapped-in pages correctly become dirty again and retain new data
  printf("PID %d: Step 2 - Modifying data to test re-swap integrity...\n", pid);
  for(int i = 0; i < TEST_PAGES; i++){
    // Using a different pattern (multiplied by 2) to ensure old swap data isn't just lingering
    buffer[i * PGSIZE] = (char)((i * 2) % 256); 
    buffer[i * PGSIZE + PGSIZE - 1] = (char)(0xFF - ((i * 2) % 256));
  }

  print_vmstats(pid, "AFTER SECOND WRITE");

  // 5. READ and VERIFY modified data
  // This guarantees that re-swapped pages hold the newly modified data
  for(int i = 0; i < TEST_PAGES; i++){
    char expected_head = (char)((i * 2) % 256);
    char expected_tail = (char)(0xFF - ((i * 2) % 256));
    
    if(buffer[i * PGSIZE] != expected_head || buffer[i * PGSIZE + PGSIZE - 1] != expected_tail){
      printf("CRITICAL ERROR PID %d: Data corruption at page %d during second read!\n", pid, i);
      printf("Expected [%d, %d], Got [%d, %d]\n", 
              expected_head, expected_tail, buffer[i * PGSIZE], buffer[i * PGSIZE + PGSIZE - 1]);
      exit(1);
    }
  }

  print_vmstats(pid, "FINAL STATE");
  printf("\nSUCCESS PID %d: All %d pages verified. Read/Write Integrity maintained.\n", pid, TEST_PAGES);
  
  // If we are the parent, wait for the child before exiting to prevent zombie processes
  if(child_pid > 0){
    wait(0);
  }
  
  exit(0);
}