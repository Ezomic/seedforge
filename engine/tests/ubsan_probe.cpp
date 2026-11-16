// Overflows a signed int on purpose. ubsan_halt_check.cmake runs it and fails unless UBSan reports the
// overflow and the process exits nonzero, so a sanitizer build that lets UBSan findings pass goes red.
#include <climits>
#include <cstdio>

int main()
{
    volatile int big = INT_MAX;
    const int wrapped = big + 1;
    std::printf("still running after the overflow: %d\n", wrapped);
    return 0;
}
