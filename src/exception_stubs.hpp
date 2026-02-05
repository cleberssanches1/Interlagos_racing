#pragma once

namespace std
{
    void __throw_bad_array_new_length() __attribute__((weak));
    void __throw_bad_alloc() __attribute__((weak));
    void __throw_length_error(const char*) __attribute__((weak));
}
