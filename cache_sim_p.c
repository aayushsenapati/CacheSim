#include <stdlib.h>
#include <stdio.h>
#include <omp.h>
#include <string.h>

typedef char byte;

// MESI states
#define MESI_MODIFIED 'M'
#define MESI_EXCLUSIVE 'E'
#define MESI_SHARED 'S'
#define MESI_INVALID 'I'

struct cache {
    byte address; // Address in memory
    byte value; // Value stored in cached memory
    char state; // MESI state
    int lru_counter; // Counter for Least Recently Used cache line
};

struct decoded_inst {
    int type; // 0 for RD, 1 for WR
    byte address;
    byte value; // Value for WR
};

typedef struct cache cache;
typedef struct decoded_inst decoded;

byte *memory;

decoded decode_inst_line(char *buffer) {
    decoded inst;
    char inst_type[3];
    sscanf(buffer, "%s", inst_type);
    if (!strcmp(inst_type, "RD")) {
        inst.type = 0;
        int addr = 0;
        sscanf(buffer, "%s %d", inst_type, &addr);
        inst.value = -1;
        inst.address = addr;
    } else if (!strcmp(inst_type, "WR")) {
        inst.type = 1;
        int addr = 0;
        int val = 0;
        sscanf(buffer, "%s %d %d", inst_type, &addr, &val);
        inst.address = addr;
        inst.value = val;
    }
    return inst;
}

void cpu_loop(int num_cores, int cache_size) {
    // Array of cache for each core
    cache **c = (cache **) malloc(sizeof(cache *) * num_cores);
    for (int i = 0; i < num_cores; ++i) {
        c[i] = (cache *) malloc(sizeof(cache) * cache_size);
        // Initialize cache lines
        for (int j = 0; j < cache_size; j++) {
            c[i][j].lru_counter = 0;
            c[i][j].state = MESI_INVALID;
        }
    }

    // Read input files and decode instructions
    #pragma omp parallel num_threads(num_cores) shared(c, memory)
    {
        int core_num = omp_get_thread_num();
        char filename[20];
        sprintf(filename, "input_%d.txt", core_num);
        FILE *inst_file = fopen(filename, "r");
        char inst_line[20];
        while (fgets(inst_line, sizeof(inst_line), inst_file)) {
            decoded inst = decode_inst_line(inst_line);
            int hash = inst.address % cache_size;
            cache cacheline = c[core_num][hash];
            switch (inst.type) {
                case 0: // RD
                    if (cacheline.address == inst.address && cacheline.state != MESI_INVALID) { // Cache hit
                        printf("Core %d: RD %d: %d\n", core_num, inst.address, cacheline.value);
                        if (cacheline.state == MESI_MODIFIED || cacheline.state == MESI_EXCLUSIVE) {
                            cacheline.state = MESI_SHARED;
                        }
                    } else { // Cache miss
                        printf("Core %d: RD %d: -1\n", core_num, inst.address);
                        // Cache coherence
                        // Broadcast invalidation message to other cores
                        #pragma omp critical
                        {
                            for (int i = 0; i < num_cores; i++) {
                                for (int j = 0; j < cache_size; j++) {
                                    if (c[i][j].address == inst.address && c[i][j].state != MESI_INVALID) {
                                        c[i][j].state = MESI_SHARED;
                                    }
                                }
                            }
                        }
                        cacheline.address = inst.address;
                        cacheline.state = MESI_EXCLUSIVE;
                        cacheline.value = memory[inst.address];
                    }
                    break;

                case 1: // WR
                    if (cacheline.address == inst.address && cacheline.state != MESI_INVALID) { // Cache hit
                        printf("Core %d: WR %d: %d\n", core_num, inst.address, inst.value);
                        cacheline.value = inst.value;
                        cacheline.state = MESI_MODIFIED;
                    } else { // Cache miss
                        printf("Core %d: WR %d: -1\n", core_num, inst.address);
                        // Cache coherence
                        // Broadcast invalidation message to other cores
                        #pragma omp critical
                        {
                            for (int i = 0; i < num_cores; i++) {
                                for (int j = 0; j < cache_size; j++) {
                                    if (c[i][j].address == inst.address && c[i][j].state != MESI_INVALID) {
                                        c[i][j].state = MESI_INVALID;
                                    }
                                }
                            }
                        }
                        int index = cache_replacement_policy(c[core_num], cache_size);
                        cacheline = c[core_num][index];
                        cacheline.address = inst.address;
                        cacheline.state = MESI_MODIFIED;
                        cacheline.value = inst.value;
                    }
                    break;
            }

            // Update LRU counter
            for (int i = 0; i < cache_size; i++) {
                if (c[core_num][i].address == cacheline.address) {
                    c[core_num][i].lru_counter = 0;
                } else {
                    c[core_num][i].lru_counter++;
                }
            }

            // Update cache line
            c[core_num][hash] = cacheline;
        }
        fclose(inst_file);
    }

    // Free cache memory
    for (int i = 0; i < num_cores; ++i) {
        free(c[i]);
    }
    free(c);
}

int main(int argc, char *argv[]) {
    // Get the number of cores from command line argument
    int num_cores = argc > 1 ? atoi(argv[1]) : omp_get_max_threads();

    // Initialize global memory
    int memory_size = 24;
    memory = (byte *) malloc(sizeof(byte) * memory_size);

    // Run CPU loop with the number of cores equal to the number of input files
    cpu_loop(num_cores, 4);

    free(memory);
    return 0;
}
