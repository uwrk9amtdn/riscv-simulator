#include <hart.h>

void hart::op_lui()
{
    imm = get_part_s(inst, 31, 12, 12);
    regs[rd] = imm;
    pc = pc + 4;
}