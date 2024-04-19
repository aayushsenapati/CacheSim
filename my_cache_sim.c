#include <stdlib.h>
#include <stdio.h>
#include <omp.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

typedef char byte;

enum mesi_state
{
    MODIFIED,
    EXCLUSIVE,
    SHARED,
    INVALID
};

enum bus_mode
{
    RD,
    WR,
    UP,
    NF,
    ACK
};

struct cache
{
    byte address;          // This is the address in memory.
    byte value;            // This is the value stored in cached memory.
    enum mesi_state state; // MESI state
};

struct decoded_inst
{
    int type; // 0 is RD, 1 is WR
    byte address;
    byte value; // Only used for WR
};

struct bus_data
{
    enum bus_mode mode;
    byte address;
    byte value;
};

typedef struct cache cache;
typedef struct decoded_inst decoded;

struct bus_data *bus; // This is the "bus" that caches use to communicate.

byte *memory;

decoded decode_inst_line(char *buffer)
{
    decoded inst;
    char inst_type[3]; // Increased size to accommodate 'WR\0' and 'RD\0'
    sscanf(buffer, "%s", inst_type);
    if (!strcmp(inst_type, "RD"))
    {
        inst.type = 0;
        int addr = 0;
        sscanf(buffer, "%s %d", inst_type, &addr);
        inst.value = -1;
        inst.address = addr;
    }
    else if (!strcmp(inst_type, "WR"))
    {
        inst.type = 1;
        int addr = 0;
        int val = 0;
        sscanf(buffer, "%s %d %d", inst_type, &addr, &val);
        inst.address = addr;
        inst.value = val;
    }
    return inst;
}

void print_cachelines(cache *c, int cache_size)
{
    for (int i = 0; i < cache_size; i++)
    {
        cache cacheline = *(c + i);
        printf("Address: %d, State: %d, Value: %d\n", cacheline.address, cacheline.state, cacheline.value);
    }
}

void* cpu_loop(void *args)
{
    struct cpu_args *cpu_args = (struct cpu_args *)args;
    int core_id=cpu_args->core_id;
    int num_threads=cpu_args->num_threads;
    int cache_size=cpu_args->cache_size;
    cache *c=cpu_args->caches[core_id];
    struct bus_data *bus=cpu_args->bus;
    // Read Input file
    char filename[20];
    sprintf(filename, "input_%d.txt", core_id);
    FILE *inst_file = fopen(filename, "r");
    if (inst_file == NULL)
    {
        // Handle file open error
        perror("Error opening file");
        printf("Filename: %s\n", filename);
        return;
    }
    char inst_line[20];
    // Decode instructions and execute them.
    while (fgets(inst_line, sizeof(inst_line), inst_file))
    {
        decoded inst = decode_inst_line(inst_line);
        /*
         * Cache Replacement Algorithm
         */
        int hash = inst.address % cache_size;
        cache *cacheline = &c[hash];

        /*
         * MESI Protocol Implementation
         */

        if (inst.type == 0)
        { // Read
            if (cacheline->state == INVALID)
            {
#pragma omp critical
                {
                    for (int i = 0; i < num_threads; i++)
                    {
                        if (i != core_id)
                        {
                            bus[i].mode = RD;
                            bus[i].address = inst.address;
                            bus[i].value = -1;
                        }
                    }
                }

                // Wait for the bus
                int i = 0;
                int notfound = 0;
                while (bus[i].mode != UP)
                {
                    if (bus[i].mode == NF)
                    {
                        notfound++;
                    }
                    if (notfound == num_threads)
                    {
                        break;
                    }
                    (i++) % num_threads;
                    if (i == core_id)
                        (i++) % num_threads;
                }

                if (bus[i].mode == UP)
                {
                    cacheline->value = bus[i].value;
                    cacheline->state = SHARED;
                }
                else
                {
                    cacheline->value = memory[inst.address];
                    cacheline->state = EXCLUSIVE;
                }

                cacheline->address = inst.address;

                for (int i = 0; i < num_threads; i++)
                {
                    if (i != core_id)
                    {
                        bus[i].mode = ACK;
                    }
                }
            }
            else if (cacheline->state == SHARED)
            {

                if (inst.address != cacheline->address)
                {
                    cacheline->address = inst.address;
#pragma omp critical
                    {
                        for (int i = 0; i < num_threads; i++)
                        {
                            if (i != core_id)
                            {
                                bus[i].mode = RD;
                                bus[i].address = inst.address;
                                bus[i].value = -1;
                            }
                        }
                    }
                    // Wait for the bus
                    int i = 0;
                    int notfound = 0;
                    while (bus[i].mode != UP)
                    {
                        if (bus[i].mode == NF)
                        {
                            notfound++;
                        }
                        if (notfound == num_threads)
                        {
                            break;
                        }
                        (i++) % num_threads;
                        if (i == core_id)
                            (i++) % num_threads;
                    }

                    if (bus[i].mode == UP)
                    {
                        cacheline->value = bus[i].value;
                        cacheline->state = SHARED;
                    }
                    else
                    {
                        cacheline->value = memory[inst.address];
                        cacheline->state = EXCLUSIVE;
                    }
                    // not writing to memory as it is already in shared state
                    cacheline->address = inst.address;
                }
            }
            else if (cacheline->state == EXCLUSIVE)
            {
                if (inst.address != cacheline->address)
                {
#pragma omp critical
                    {
                        for (int i = 0; i < num_threads; i++)
                        {
                            if (i != core_id)
                            {
                                bus[i].mode = RD;
                                bus[i].address = inst.address;
                                bus[i].value = -1;
                            }
                        }
                    }
                    // Wait for the bus
                    int i = 0;
                    int notfound = 0;
                    while (bus[i].mode != UP)
                    {
                        if (bus[i].mode == NF)
                        {
                            notfound++;
                        }
                        if (notfound == num_threads)
                        {
                            break;
                        }
                        (i++) % num_threads;
                        if (i == core_id)
                            (i++) % num_threads;
                    }

                    if (bus[i].mode == UP)
                    {
                        cacheline->value = bus[i].value;
                        cacheline->state = SHARED;
                    }
                    else
                    {
                        cacheline->value = memory[inst.address];
                        cacheline->state = EXCLUSIVE;
                    }
                    // not writing to memory as it is already in exclusive state
                    cacheline->address = inst.address;
                }
            }
            else if (cacheline->state == MODIFIED)
            {

                if (inst.address != cacheline->address)
                {

#pragma omp critical
                    {
                        for (int i = 0; i < num_threads; i++)
                        {
                            if (i != core_id)
                            {
                                bus[i].mode = RD;
                                bus[i].address = inst.address;
                                bus[i].value = -1;
                            }
                        }
                    }
                    // Wait for the bus
                    int i = 0;
                    int notfound = 0;
                    while (bus[i].mode != UP)
                    {
                        if (bus[i].mode == NF)
                        {
                            notfound++;
                        }
                        if (notfound == num_threads)
                        {
                            break;
                        }
                        (i++) % num_threads;
                        if (i == core_id)
                            (i++) % num_threads;
                    }

                    if (bus[i].mode == UP)
                    {
                        cacheline->value = bus[i].value;
                        cacheline->state = SHARED;
                        memory[cacheline->address] = cacheline->value;
                    }
                    else
                    {
                        cacheline->value = memory[inst.address];
                        cacheline->state = EXCLUSIVE;
                    }
                    cacheline->address = inst.address;
                }
            }
        }
        else
        { // writes
            if (cacheline->state == INVALID)
            {
            }
            else if (cacheline->state == SHARED)
            {
            }
            else if (cacheline->state == MODIFIED)
            {
                if (inst.address != cacheline->address)
                {
                    memory[cacheline->address] = cacheline->value;
                }
            }
            else if (cacheline->state == EXCLUSIVE)
            {
            }

            // invalidate all other copies
#pragma omp critical
            {
                for (int i = 0; i < num_threads; i++)
                {
                    if (i != core_id)
                    {
                        bus[i].mode = WR;
                        bus[i].address = inst.address;
                        bus[i].value = inst.value;
                    }
                }
            }
            cacheline->state = MODIFIED;
            cacheline->value = inst.value;
            cacheline->address = inst.address;
        }

        switch (inst.type)
        {
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


void * bus_listener(void * args){
    struct cpu_args *cpu_args = (struct cpu_args *)args;
    int core_id=cpu_args->core_id;
    cache *c=cpu_args->caches[core_id];
    struct bus_data *bus=cpu_args->bus;
}


struct cpu_args
{
    int core_id;
    int num_threads;
    int cache_size;
    cache **caches;
    struct bus_data *bus;
};

int main(int c, char *argv[])
{
    // Initialize Global memory
    // Let's assume the memory module holds about 24 bytes of data.
    int memory_size = 24;
    memory = (byte *)malloc(sizeof(byte) * memory_size);
    if (memory == NULL)
    {
        // Handle error
        perror("Memory allocation failed");
        return 1;
    }
    int num_threads = 2;
    int cache_size = 2;
    cache **caches = (cache *)malloc(sizeof(cache) * num_threads);
    for(int i = 0; i < num_threads; i++){
        caches[i] = (cache *)malloc(sizeof(cache) * cache_size);
    }
    
    if (caches == NULL)
    {
        // Handle error
        perror("Cache allocation failed");
        free(memory);
        return 1;
    }
    bus = (struct bus_data *)malloc(sizeof(struct bus_data) * num_threads); // make bus

    struct cpu_args *args = (struct cpu_args *)malloc(sizeof(struct cpu_args) * num_threads);
    for (int i = 0; i < num_threads; i++)
    {
        args[i].core_id = i;
        args[i].num_threads = num_threads;
        args[i].cache_size = cache_size;
        args[i].caches = caches;
        args[i].bus = bus;
    }



#pragma omp parallel num_threads(num_threads)
    { 
        pthread_t threads[2];
        pthread_create(&threads[0], NULL, cpu_loop, &args[omp_get_thread_num()]);
        pthread_create(&threads[1], NULL, bus_listener, &args[omp_get_thread_num()]);


    }
    free(caches);
    free(memory);
    return 0;
}