#pragma once
#include "secure-join/Defines.h"
#include <vector>
#include "cryptoTools/Common/Timer.h"
#include "coproto/Socket/Socket.h"
#include "macoro/task.h"
#include "macoro/macros.h"
#include "secure-join/Perm/Permutation.h"

namespace secJoin
{
    // and XOR sharing of a permutation
    class AdditivePerm 
    {
    public:
        // The XOR shares of the permutation pi.
        std::vector<u32> mShare;

        u64 size() const { return mShare.size(); }


        macoro::task<> validate(coproto::Socket& sock)
        {
            MC_BEGIN(macoro::task<>, this, &sock,
                perm = Perm{}
                );

            perm.mPi.resize(mShare.size());


            MC_AWAIT(sock.send(coproto::copy(mShare)));
            MC_AWAIT(sock.recv(perm.mPi));

            for(u64 i = 0; i < perm.mPi.size(); ++i)
            {
                perm.mPi[i] ^= mShare[i];
            }

            perm.validate();

            MC_END();
        }
    };
}