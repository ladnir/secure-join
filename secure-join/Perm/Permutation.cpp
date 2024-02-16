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


    // A.compose(B) computes the permutation A o B
    Perm Perm::compose(const Perm& rhs) const
    {
        return apply(rhs.mPi);
    }

    // A.composeSwap(B) computes the permutation B o A
    Perm Perm::composeSwap(const Perm& rhs) const
    {
        return rhs.apply(mPi);
    }

}