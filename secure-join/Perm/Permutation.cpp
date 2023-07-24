#include "Permutation.h"


namespace secJoin
{

    // returns the inverse permutation
    Perm Perm::inverse() const
    {
        std::vector<u32> retd(size());
        for (auto i = 0ull; i < size(); i++)
            retd[mPi[i]] = i;
            
        return retd;
    }


    // A.composeSwap(B) computes the permutation AoB
    Perm Perm::compose(const Perm& rhs) const
    {
        return apply(rhs.mPi);
    }

    // A.composeSwap(B) computes the permutation BoA
    Perm Perm::composeSwap(const Perm& rhs) const
    {
        //std::vector<i64> res = applyInv(rhs.mPi);
        //return Perm(res);

        return rhs.apply(mPi);
    }

}