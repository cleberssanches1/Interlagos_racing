#include "exception_stubs.hpp"
#include <srl.hpp>

namespace std
{
    [[noreturn]] void __throw_bad_array_new_length() { while (1) {} }
    [[noreturn]] void __throw_bad_alloc() { while (1) {} }
    [[noreturn]] void __throw_out_of_range(const char*) { while (1) {} }
    [[noreturn]] void __throw_out_of_range_fmt(const char*, ...) { while (1) {} }
}

extern "C"
{
    void* malloc(size_t size)
    {
        if (size == 0u) return nullptr;
        return SRL::Memory::HighWorkRam::Malloc(size);
    }

    void free(void* ptr)
    {
        SRL::Memory::Free(ptr);
    }

    void* calloc(size_t count, size_t size)
    {
        if (count == 0u || size == 0u) return nullptr;
        const size_t total = count * size;
        void* ptr = SRL::Memory::HighWorkRam::Malloc(total);
        if (ptr != nullptr)
        {
            SRL::Memory::MemSet(ptr, 0, total);
        }
        return ptr;
    }

    void* realloc(void* ptr, size_t size)
    {
        if (ptr == nullptr)
        {
            return malloc(size);
        }
        if (size == 0u)
        {
            free(ptr);
            return nullptr;
        }
        if (SRL::Memory::HighWorkRam::InRange(ptr))
        {
            return SRL::Memory::HighWorkRam::Realloc(ptr, size);
        }
        if (SRL::Memory::LowWorkRam::InRange(ptr))
        {
            return SRL::Memory::LowWorkRam::Realloc(ptr, size);
        }
        if (SRL::Memory::CartRam::InRange(ptr))
        {
            return SRL::Memory::CartRam::Realloc(ptr, size);
        }
        return nullptr;
    }

    void cfree(void* ptr)
    {
        free(ptr);
    }

    void* _malloc_r(struct _reent*, size_t size)
    {
        return malloc(size);
    }

    void _free_r(struct _reent*, void* ptr)
    {
        free(ptr);
    }

    void* _calloc_r(struct _reent*, size_t count, size_t size)
    {
        return calloc(count, size);
    }

    void* _realloc_r(struct _reent*, void* ptr, size_t size)
    {
        return realloc(ptr, size);
    }
}
