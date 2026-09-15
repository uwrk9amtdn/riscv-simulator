#include <hart.h>

void hart::op_auipc()
{
    imm = get_part_s(inst, 31, 12, 12);
    regs[rd] = pc + imm;
    pc = pc + 4;
}