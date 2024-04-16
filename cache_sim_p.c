#include<stdlib.h>
#include<stdio.h>
#include<omp.h>
#include<string.h>
#include<ctype.h>

typedef char byte;

enum mesi_state {
    MODIFIED,
    EXCLUSIVE,
    SHARED,
    INVALID
};

struct cache {
    byte address; // This is the address in memory.
    byte value; // This is the value stored in cached memory.
    enum mesi_state state; // MESI state
};

struct decoded_inst {
    int type; // 0 is RD, 1 is WR
    byte address;
    byte value; // Only used for WR 
};

typedef struct cache cache;
typedef struct decoded_inst decoded;

byte * memory;

decoded decode_inst_line(char * buffer){
    decoded inst;
    char inst_type[3]; // Increased size to accommodate 'WR\0' and 'RD\0'
    sscanf(buffer, "%s", inst_type);
    if(!strcmp(inst_type, "RD")){
        inst.type = 0;
        int addr = 0;
        sscanf(buffer, "%s %d", inst_type, &addr);
        inst.value = -1;
        inst.address = addr;
    } else if(!strcmp(inst_type, "WR")){
        inst.type = 1;
        int addr = 0;
        int val = 0;
        sscanf(buffer, "%s %d %d", inst_type, &addr, &val);
        inst.address = addr;
        inst.value = val;
    }
    return inst;
}

void print_cachelines(cache * c, int cache_size){
    for(int i = 0; i < cache_size; i++){
        cache cacheline = *(c+i);
        printf("Address: %d, State: %d, Value: %d\n", cacheline.address, cacheline.state, cacheline.value);
    }
}

void cpu_loop(int core_id, int num_threads, int cache_size, cache *c, int memory_size){
    // Read Input file
    char filename[20];
    sprintf(filename, "input_%d.txt", core_id);
    FILE * inst_file = fopen(filename, "r");
    if(inst_file == NULL){
        // Handle file open error
        perror("Error opening file");
        printf("Filename: %s\n", filename);
        return;
    }
    char inst_line[20];
    // Decode instructions and execute them.
    while (fgets(inst_line, sizeof(inst_line), inst_file)){
        decoded inst = decode_inst_line(inst_line);
        /*
         * Cache Replacement Algorithm
         */
        int hash = inst.address % cache_size;
        cache *cacheline = &c[hash];
        /*
         * This is where you will implement the coherency check.
         * For now, we will simply grab the latest data from memory.
         */
        if(cacheline->address != inst.address){
            // Flush current cacheline to memory
            if(cacheline->address >= 0 && cacheline->address < memory_size){
                memory[cacheline->address] = cacheline->value;
            }
            // Assign new cacheline
            cacheline->address = inst.address;
            cacheline->state = INVALID; // Set state to INVALID
            // This is where it reads value of the address from memory
            if(inst.address >= 0 && inst.address < memory_size){
                cacheline->value = memory[inst.address];
            }
            if(inst.type == 1){
                cacheline->value = inst.value;
                cacheline->state = MODIFIED; // Set state to MODIFIED after a write
            } else {
                cacheline->state = EXCLUSIVE; // Set state to EXCLUSIVE after a read
            }
        } else {
            if(inst.type == 1){
                cacheline->value = inst.value;
                cacheline->state = MODIFIED; // Set state to MODIFIED after a write
            } else {
                switch(cacheline->state){
                    case MODIFIED:
                        cacheline->state = SHARED; // Set state to SHARED if it was MODIFIED
                        break;
                    case SHARED:
                        // No state change needed if already SHARED
                        break;
                    case EXCLUSIVE:
                        cacheline->state = SHARED; // Set state to SHARED if it was EXCLUSIVE
                        break;
                    case INVALID:
                        // This should not happen according to MESI protocol
                        break;
                }
            }
        }
        switch(inst.type){
            case 0:
                printf("Core %d reading from address %d: %d\n", core_id, cacheline->address, cacheline->value);
                break;
            
            case 1:
                printf("Core %d writing to address %d: %d\n", core_id, cacheline->address, cacheline->value);
                break;
        }
    }
    fclose(inst_file);
}

int main(int c, char * argv[]){
    // Initialize Global memory
    // Let's assume the memory module holds about 24 bytes of data.
    int memory_size = 24;
    memory = (byte *) malloc(sizeof(byte) * memory_size);
    if(memory == NULL){
        // Handle error
        perror("Memory allocation failed");
        return 1;
    }
    int num_threads = 2;
    int cache_size = 2;
    cache * caches = (cache *) malloc(sizeof(cache) * cache_size);
    if(caches == NULL){
        // Handle error
        perror("Cache allocation failed");
        free(memory);
        return 1;
    }
    #pragma omp parallel num_threads(num_threads)
    {
        cpu_loop(omp_get_thread_num(), num_threads, cache_size, caches, memory_size);
    }
    free(caches);
    free(memory);
    return 0;
}
