#include <stddef.h>
#include <limits.h>
#define STR2(x) #x
#define STR(x) STR2(x)
#pragma message("sizeof(short)=" STR(__SIZEOF_SHORT__))
#pragma message("sizeof(int)=" STR(__SIZEOF_INT__))
#pragma message("sizeof(long)=" STR(__SIZEOF_LONG__))
#pragma message("sizeof(long long)=" STR(__SIZEOF_LONG_LONG__))
#pragma message("sizeof(void*)=" STR(__SIZEOF_POINTER__))
#pragma message("sizeof(size_t)=" STR(__SIZEOF_SIZE_T__))
int main(){return 0;}
