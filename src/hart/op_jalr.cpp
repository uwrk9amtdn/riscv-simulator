#include <hart.h>

hart::handler_t hart::decode_jalr()
{
    if (funct3 != 0b000) {
        return &thunk<&hart::inst_illegal>;
    }

    return &thunk<&hart::inst_jalr>;
}

void hart::inst_jalr()
{
    u32 target_pc;

    target_pc = (regs[rs1] + imm) & (~1);
    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        regs[rd] = pc + 4;
        pc = target_pc;
    }
}