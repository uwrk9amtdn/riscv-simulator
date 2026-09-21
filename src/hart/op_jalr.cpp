#include <hart.h>

void hart::decode_jalr()
{
    inst_jalr();
}

void hart::inst_jalr()
{
    u32 target_pc;

    imm = get_part_s(inst, 31, 20);
    target_pc = (regs[rs1] + imm) & (~1);
    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        regs[rd] = pc + 4;
        pc = target_pc;
    }
}