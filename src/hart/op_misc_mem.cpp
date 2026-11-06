#include <hart.h>

hart::handler_t hart::decode_misc_mem()
{
    switch (funct3) {
        case FUNCT3_FENCE:   return &thunk<&hart::inst_fence>;
        case FUNCT3_FENCE_I: return &thunk<&hart::inst_fence_i>;
    }

    return &thunk<&hart::inst_illegal>;
}

void hart::inst_fence() {
    std::atomic_thread_fence(std::memory_order_seq_cst); // other harts may run in other threads
    pc = pc + 4;
}

void hart::inst_fence_i() {
    pc = pc + 4;
}