#pragma once

namespace std
{
    void __throw_bad_array_new_length() __attribute__((weak));
    void __throw_bad_alloc() __attribute__((weak));
    void __throw_out_of_range(const char*) __attribute__((weak));
    void __throw_out_of_range_fmt(const char*, ...) __attribute__((weak));
}
