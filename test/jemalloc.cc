#include "jemalloc/jemalloc.h"
#include "gtest/gtest.h"
#include <stdlib.h>

TEST(Jemalloc, Jemalloc_Statistics_Test) {

    auto do_something=[](size_t i) {
        // Leak some memory.
        malloc(i * 100);
};

  for (size_t i = 0; i < 10; i++) {
                do_something(i);
        }

        // Dump allocator statistics to stderr.
        //The malloc_stats_print() function writes summary statistics
        malloc_stats_print(NULL, NULL, NULL);
}