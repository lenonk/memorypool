#include <mutex>
#include <limits>
#include <vector>
#include <thread>
#include <string>
#include <atomic>
#include <cstring>
#include <sstream>
#include <algorithm>

#include <stdint.h>
#include <unistd.h>

#define _MEM_POOL_DEBUG_
#include <memory_pool.h>

std::atomic<int_fast64_t> counter { 0 };

// Data struct for MemoryPool to house
struct data {
    int_fast64_t foo = 0;
    char data[65535];
    int_fast32_t bar = 0;
    int_fast16_t baz = 0;
    bool boo = false;
    std::stringstream far { "" }; 
};

// Create a new MemoryPool
MemoryPool<data, 1000> pool;

// Creates a random string of "length"
std::string random_string( size_t length )
{
    auto randchar = []() -> char {
        const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
        
        const size_t max_index = (sizeof(charset) - 1);
        return charset[ rand() % max_index ];
    };
    
    std::string str(length, 0);
    std::generate_n(str.begin(), length, randchar);
    return str;
}

// Torture test the MemoryPool
int32_t 
allocate(int8_t threads, int32_t limit) {
    // Vector of thread ids so we can join() them later
    std::vector<std::thread *> tids;
    
    for (int8_t i = 0; i < threads; i++) {
        std::thread *tid = new std::thread([&]() {
            // Vector to hold the allocated MemoryPool nodes
            std::vector<data *> nodes(limit);
            
            while (1) {
                // Allocate "limit" new nodes from the MemoryPool.  Set values on all the members
                // and go to sleep for a random amount of time.  After waking up, check that the
                // values are all what we expect them to be, then return the nodes to the MemoryPool.
                // If _MEM_POOL_DEBUG_ is defined, MemoryPool has code to check that no element is 
                // ever double allocated, and that elements are returned properly.
                for (int16_t i = 0; i < limit; i++) {
                    nodes[i] = pool.new_element();
                    
                    std::string pkt_data = random_string(65535);
                    std::memcpy(nodes[i]->data, pkt_data.c_str(), pkt_data.length());
                    
                    auto new_foo = ++nodes[i]->foo;
                    auto new_bar = ++nodes[i]->bar;
                    auto new_baz = ++nodes[i]->baz;
                    auto new_boo = !nodes[i]->boo;
                    nodes[i]->boo = new_boo;
                    
                    nodes[i]->far << "Iteration" << new_foo; 
                    
                    std::this_thread::sleep_for(std::chrono::microseconds(rand() % 1000));
                    
                    if (std::strncmp(nodes[i]->data, pkt_data.c_str(), pkt_data.length()) != 0) {
                        fprintf(stdout, "pkt_data does not match!\n");
                        fprintf(stdout, "1: %s\n", pkt_data.c_str());
                        fprintf(stdout, "2: %s\n", nodes[i]->data);
                        abort();
                    }
                    
                    if (nodes[i]->foo != new_foo) {
                        fprintf(stdout, "foo does not match! %ld != %ld\n", nodes[i]->foo, new_foo);
                        abort();
                    }
                    
                    if (nodes[i]->bar != new_bar) {
                        fprintf(stdout, "bar does not match! %ld != %ld\n", nodes[i]->bar, new_bar);
                        abort();
                    }
                    
                    if (nodes[i]->baz != new_baz) {
                        fprintf(stdout, "baz does not match! %ld != %ld\n", nodes[i]->baz, new_baz);
                        abort();
                    }
                    
                    if (nodes[i]->boo != new_boo) {
                        fprintf(stdout, "boo does not match! %d != %d\n", nodes[i]->boo, new_boo);
                        abort();
                    }
                    
                    if (nodes[i]->far.str() != "Iteration" + std::to_string(new_foo)) {
                        fprintf(stdout, "far does not match!\n");
                        abort();
                    }
                }
                
                for (int16_t i = 0; i < limit; i++)
                   pool.delete_element(nodes[i]);

                nodes.clear();
                if ((++counter % 10) == 0)
                    fprintf(stdout, "Counter: %ld\n", counter.load());
            }
        });
        
        tids.push_back(tid);
    }

    for (auto tid : tids) { tid->join(); }

    return 0;
}

int32_t
main(void) {
    // Using a limit of 400 with a thread count of 5 causes two blocks of 1000 
    // to be allcoated from the memory pool. This ensures there's no problem 
    // with dynamically allocated blocks
    allocate(5, 400);
}
