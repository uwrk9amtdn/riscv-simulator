#include <hart.h>

hart::handler_t hart::decode_lui()
{
    return &thunk<&hart::inst_lui>;
}

void hart::inst_lui()
{
    regs[rd] = imm;
    pc = pc + 4;
}