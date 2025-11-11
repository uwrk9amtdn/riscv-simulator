#include "ram.h"
#include <cstring>

ram::ram(u32 size)
{
    this->size_ = size;
    this->data = std::make_unique<u8[]>(size);
}

bool ram::load(u32 addr, u32 len, u8* data)
{
    if (addr >= size_ || len > size_ - addr) {
        return false;
    }

    if (data == 0) {
        return false;
    }

    memcpy(data, this->data.get() + addr, len);

    return true;
}

bool ram::store(u32 addr, u32 len, const u8* data)
{
    if (addr >= size_ || len > size_ - addr) {
        return false;
    }

    if (data == 0) {
        return false;
    }

    memcpy(this->data.get() + addr, data, len);

    return true;
}

u32 ram::size() const
{
    return size_;
}

void ram::tick() { }

ram::~ram() { }