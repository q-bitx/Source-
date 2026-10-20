#include <util/ecc_context.h>

void ECC_Start();
void ECC_Stop();

ECC_Context::ECC_Context()
{
    ECC_Start();
}

ECC_Context::~ECC_Context()
{
    ECC_Stop();
}
