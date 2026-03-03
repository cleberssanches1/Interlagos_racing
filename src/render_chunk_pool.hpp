#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include <srl.hpp>

template <typename Handle>
class RenderChunkPool
{
public:
    bool Initialize(size_t capacity)
    {
        Release();
        if (capacity == 0) return false;
        storage_ = static_cast<Handle*>(SRL::Memory::HighWorkRam::Malloc(sizeof(Handle) * capacity));
        if (!storage_) return false;
        capacity_ = capacity;
        count_ = 0;
        constructed_ = 0;
        return true;
    }

    void Release()
    {
        if (storage_)
        {
            for (size_t i = 0; i < constructed_; ++i)
            {
                storage_[i].~Handle();
            }
            SRL::Memory::HighWorkRam::Free(storage_);
            storage_ = nullptr;
        }
        capacity_ = 0;
        count_ = 0;
        constructed_ = 0;
    }

    ~RenderChunkPool()
    {
        Release();
    }

    void SetActive(const Handle* items, size_t count)
    {
        if (!storage_ || capacity_ == 0 || !items)
        {
            count_ = 0;
            return;
        }
        const size_t copyCount = std::min(capacity_, count);
        const size_t keepCount = std::min(constructed_, copyCount);
        for (size_t i = 0; i < copyCount; ++i)
        {
            if (i < keepCount)
            {
                storage_[i] = items[i];
            }
            else
            {
                ::new (static_cast<void*>(storage_ + i)) Handle(items[i]);
            }
        }
        for (size_t i = copyCount; i < constructed_; ++i)
        {
            storage_[i].~Handle();
        }
        count_ = copyCount;
        constructed_ = copyCount;
    }

    const Handle* Active() const { return storage_; }
    size_t ActiveCount() const { return count_; }
    size_t Capacity() const { return capacity_; }
    bool Ready() const { return storage_ != nullptr; }

private:
    Handle* storage_ = nullptr;
    size_t capacity_ = 0;
    size_t count_ = 0;
    size_t constructed_ = 0;
};
