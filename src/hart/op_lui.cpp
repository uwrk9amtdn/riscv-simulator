#include <hart.h>

void hart::decode_lui()
{
    inst_lui();
}

void hart::inst_lui()
{
    imm = get_part_s(inst, 31, 12, 12);
    regs[rd] = imm;
    pc = pc + 4;
}