#include <hart.h>

void hart::decode_misc_mem()
{
    switch (funct3) {
        case FUNCT3_FENCE:   return inst_fence();
        case FUNCT3_FENCE_I: return inst_fence_i();
    }

    throw trap{trap_cause_t::illegal_instruction_exception, inst};
}

void hart::inst_fence() {
    pc = pc + 4;
}

void hart::inst_fence_i() {
    pc = pc + 4;
}