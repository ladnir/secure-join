#pragma once
#include "secure-join/Defines.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include "macoro/channel.h"
#include "macoro/macros.h"

#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"

namespace secJoin
{
    struct SendBase
    {
        std::vector<std::array<oc::PRNG, 2>> mBase;

        std::vector<std::array<oc::block, 2>> get()
        {
            std::vector<std::array<oc::block, 2>> r(mBase.size());
            for (u64 i = 0;i < r.size();++i)
            {
                r[i][0] = mBase[i][0].get();
                r[i][1] = mBase[i][1].get();
            }
            return r;
        }


        void resize(u64 n)
        {
            mBase.resize(n);
        }

        SendBase fork()
        {
            SendBase s;
            s.resize(mBase.size());
            for (u64 i = 0;i < mBase.size();++i)
            {
                s.mBase[i][0].SetSeed(mBase[i][0].get<oc::block>());
                s.mBase[i][1].SetSeed(mBase[i][1].get<oc::block>());
            }

            return s;
        }
    };

    struct RecvBase
    {
        std::vector<oc::PRNG> mBase;
        oc::BitVector mChoice;

        std::vector<oc::block> get()
        {
            std::vector<oc::block> r(mBase.size());
            for (u64 i = 0;i < r.size();++i)
            {
                r[i] = mBase[i].get();
            }
            return r;
        }



        void resize(u64 n)
        {
            mBase.resize(n);
            mChoice.resize(n);
        }
        
        RecvBase fork()
        {
            RecvBase s;
            s.resize(mBase.size());
            for (u64 i = 0;i < mBase.size();++i)
            {
                s.mBase[i].SetSeed(mBase[i].get<oc::block>());
            }

            s.mChoice = mChoice;
            return s;
        }
    };

}