#include <hart.h>

hart::handler_t hart::decode_jal()
{
    return &thunk<&hart::inst_jal>;
}

void hart::inst_jal() {
    u32 target_pc;
    target_pc = pc + imm;
    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        regs[rd] = pc + 4;
        pc = target_pc;
    }
}