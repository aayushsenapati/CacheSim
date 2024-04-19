#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CACHE_SIZE 2
#define MEMORY_SIZE 24

typedef char byte;

enum cache_state
{
  Invalid,
  Shared,
  Exclusive,
  Modified
};
enum operation_type
{
  Read = 0,
  Write = 1
};

typedef enum cache_state cache_state;
typedef enum operation_type operation_type;

struct cache_entry
{
  byte address;      // Memory address.
  byte data;         // Stored value.
  cache_state state; // State for MESI protocol.
};

struct instruction
{
  operation_type operation; // 0 for Read, 1 for Write.
  byte address;
  byte data; // Only used for Write.
};

typedef struct cache_entry cache_entry;
typedef struct instruction instruction;

byte *global_memory;

instruction parse_instruction(const char *line)
{
  instruction instr;
  char op_type[3];
  sscanf(line, "%s", op_type);
  if (!strcmp(op_type, "RD"))
  {
    int addr = 0;
    sscanf(line, "%s %d", op_type, &addr);
    instr.operation = Read;
    instr.data = -1;
    instr.address = addr;
  }
  else if (!strcmp(op_type, "WR"))
  {
    int addr = 0;
    int val = 0;
    sscanf(line, "%s %d %d", op_type, &addr, &val);
    instr.operation = Write;
    instr.address = addr;
    instr.data = val;
  }
  return instr;
}

void display_cache_entries(const cache_entry *cache, int cache_size)
{
  for (int i = 0; i < cache_size; i++)
  {
    cache_entry entry = *(cache + i);
    char state_str[10];
    switch (entry.state)
    {
    case Invalid:
      strncpy(state_str, "Invalid", sizeof(state_str));
      break;
    case Shared:
      strncpy(state_str, "Shared", sizeof(state_str));
      break;
    case Exclusive:
      strncpy(state_str, "Exclusive", sizeof(state_str));
      break;
    case Modified:
      strncpy(state_str, "Modified", sizeof(state_str));
      break;
    }
    printf("\t\tAddress: %d, State: %s, Data: %d\n", entry.address, state_str, entry.data);
  }
}

void process_instruction(cache_entry **core_cache, int num_cores, int core_id, instruction instr)
{
  int hash = instr.address % CACHE_SIZE;

  if (core_cache[core_id][hash].address != instr.address &&
      (core_cache[core_id][hash].state == Modified ||
       core_cache[core_id][hash].state == Shared))
  {
    // Flush current cache entry to memory.
    global_memory[core_cache[core_id][hash].address] = core_cache[core_id][hash].data;
    core_cache[core_id][hash].data = global_memory[instr.address];
  }

  if (instr.operation == 1)
  { // Write operation
    core_cache[core_id][hash].address = instr.address;
    core_cache[core_id][hash].data = instr.data;
    core_cache[core_id][hash].state = Modified;

    // Invalidate other caches if data is not exclusive.
    if (core_cache[core_id][hash].state != Exclusive)
    {
      for (int i = 0; i < num_cores; i++)
      {
        if (i == core_id)
          continue;
        if (core_cache[i][hash].address == instr.address)
        {
          core_cache[i][hash].state = Invalid;
        }
      }
    }
  }
  else
  { // Read operation
    if (core_cache[core_id][hash].address != instr.address ||
        core_cache[core_id][hash].state == Invalid)
    {
      bool found = false;
      for (int i = 0; i < num_cores; i++)
      {
        if (i == core_id || core_cache[i][hash].address != instr.address ||
            core_cache[i][hash].state == Invalid)
          continue;
        core_cache[core_id][hash] = core_cache[i][hash];
        core_cache[i][hash].state = Shared;
        core_cache[core_id][hash].state = Shared;
        found = true;
      }
      if (!found)
      {
        core_cache[core_id][hash].data = global_memory[instr.address];
        core_cache[core_id][hash].state = Exclusive;
        core_cache[core_id][hash].address = instr.address;
      }
    }
  }

  switch (instr.operation)
  {
  case 0:
    printf("Core %d Reading from address %02d: %02d\n", core_id, core_cache[core_id][hash].address, core_cache[core_id][hash].data);
    break;
  case 1:
    printf("Core %d Writing   to address %02d: %02d\n", core_id, core_cache[core_id][hash].address, core_cache[core_id][hash].data);
    break;
  }
}

void cpu_loop(int num_cores)
{
  // Allocate memory for cache of each core.
  cache_entry **core_cache = (cache_entry **)calloc(num_cores, sizeof(cache_entry *));
  for (int i = 0; i < num_cores; i++)
  {
    *(core_cache + i) = (cache_entry *)calloc(CACHE_SIZE, sizeof(cache_entry));
  }

#pragma omp parallel num_threads(num_cores)
  {
    int core_id = omp_get_thread_num();
    char file_name[20];
    sprintf(file_name, "input_%d.txt", core_id);
    printf("Processing file: %s\n", file_name);

    FILE *input_file = fopen(file_name, "r");
    if (input_file == NULL)
    {
      printf("Failed to open file: %s\n", file_name);
      exit(0); // Exiting the loop within the parallel region
    }
    char line[20];

    while (fgets(line, sizeof(line), input_file))
    {
      instruction instr = parse_instruction(line);
      process_instruction(core_cache, num_cores, core_id, instr);
    }
    fclose(input_file);
    free(core_cache[core_id]);
  }
  free(core_cache);
}

int main(int argc, char *argv[])
{
  global_memory = (byte *)calloc(MEMORY_SIZE, sizeof(byte));
  cpu_loop(2);
  free(global_memory);
}