#include "TestRunner.h"
#include <iostream>

void run_spsc_tests();
void run_slab_tests();
void run_matching_tests();
void run_integration_tests();

int main() {
    std::cout << "\n╔══════════════════════════════════╗\n";
    std::cout <<   "║   hft engine test suite          ║\n";
    std::cout <<   "╚══════════════════════════════════╝\n";

    run_spsc_tests();
    run_slab_tests();
    run_matching_tests();
    run_integration_tests();

    return test::summary();
}
