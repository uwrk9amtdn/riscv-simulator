#include <hart.h>

void hart::op_misc_mem()
{

    switch (funct3) {
    case FUNCT3_FENCE:
        break;

    case FUNCT3_FENCE_I:
        break;

    default:
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
        break;
    }

    pc = pc + 4;
}