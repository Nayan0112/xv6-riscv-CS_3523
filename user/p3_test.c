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
#define TEST_PAGES 120

void
print_vmstats(int pid, char *label)
{
  struct vmstats info;
  if(getvmstats(pid, &info) < 0) return;
  printf("\n--- %s ---\n", label);
  printf("  Res: %d | Faults: %d | Evicted: %d | Out: %d | In: %d\n", 
          info.resident_pages, info.page_faults, info.page_evicted,
          info.pages_swapped_out, info.pages_swapped_in);
}

int
main(int argc, char *argv[])
{
  int pid = getpid();
  char *buffer;

  //fork();

  //printf("Starting Data Integrity Swap Test...\n");

  ///1. Allocate space
  buffer = sbrk(TEST_PAGES * PGSIZE);
  if(buffer == (char*)-1) exit(1);

  // 2. WRITE unique data to each page
  // This triggers Page Faults -> vmfault -> kalloc
  printf("Step 1: Writing unique patterns to %d pages...\n", TEST_PAGES);
  for(int i = 0; i < TEST_PAGES; i++){
    // Each page gets a start byte equal to its index
    buffer[i * PGSIZE] = (char)(i % 256);
    // Write a "tail" byte to ensure the whole page is handled
    buffer[i * PGSIZE + PGSIZE - 1] = (char)(0xFF - (i % 256));
  }

  print_vmstats(pid, "AFTER WRITING (Should see Evictions if RAM/Swap is tight)");

  // 3. READ and VERIFY
  // This triggers Page Faults -> vmfault -> evict -> swap_in
  //printf("Step 2: Verifying data integrity...\n");
  for(int i = 0; i < TEST_PAGES; i++){
    char expected_head = (char)(i % 256);
    char expected_tail = (char)(0xFF - (i % 256));
    
    if(buffer[i * PGSIZE] != expected_head || buffer[i * PGSIZE + PGSIZE - 1] != expected_tail){
      printf("CRITICAL ERROR: Data corruption at page %d!\n", i);
      printf("Expected [%d, %d], Got [%d, %d]\n", 
              expected_head, expected_tail, buffer[i * PGSIZE], buffer[i * PGSIZE + PGSIZE - 1]);
      exit(1);
    }
  }
  wait(0);
  print_vmstats(pid, "FINAL STATE (Should see Swap-In count increase)");
  printf("\nSUCCESS: All %d pages verified. Integrity maintained.\n", TEST_PAGES);
  
  exit(0);
}