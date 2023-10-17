#include "DarkMatter22Prf.h"
namespace secJoin
{

    const std::array<block256, 128> DarkMatter22Prf::mB = PRNG(oc::block(2134, 5437)).get<std::array<block256, 128>>();

}