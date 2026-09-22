#include <stdint.h>
#include <stdio.h>

_Static_assert(sizeof(uint32_t) == 4, "uint32_t must be 32 bits");

int main(void)
{
    const uint32_t token = UINT32_C(0x55434d32);
    if (token != UINT32_C(0x55434d32))
        return 1;
    puts("compiler-suite-c-ok");
    return 0;
}
