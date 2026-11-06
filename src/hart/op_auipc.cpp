#include <hart.h>

hart::handler_t hart::decode_auipc()
{
    return &thunk<&hart::inst_auipc>;
}

void hart::inst_auipc()
{
    regs[rd] = pc + imm;
    pc = pc + 4;
}