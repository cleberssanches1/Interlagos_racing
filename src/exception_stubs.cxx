#include "exception_stubs.hpp"

namespace std
{
    void __throw_bad_array_new_length() {}
    void __throw_bad_alloc() {}
    void __throw_length_error(const char*) {}
}
