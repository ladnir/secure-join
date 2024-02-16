#include "ComposedPerm.h"

namespace secJoin
{
    template<>
    macoro::task<> ComposedPerm::apply<u8>(
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> out,
        coproto::Socket& chl
        )
    {

        if (out.rows() != size())
            throw RTE_LOC;
        MC_BEGIN(macoro::task<>, in, out, &chl, op, 
            this,
            soutperm = oc::Matrix<u8>{}
        );

        if (out.rows() != in.rows() ||
            out.cols() != in.cols())
            throw RTE_LOC;

        if (out.rows() != size())
            throw RTE_LOC;

        if (mPartyIdx > 1)
            throw RTE_LOC;


        if (mPermSender.hasSetup(in.cols()) == false)
            throw std::runtime_error("preprocessing has not been requested. Call request() before. " LOCATION);

        soutperm.resize(in.rows(), in.cols());
        if (((op == PermOp::Inverse) ^ bool(mPartyIdx)) == true)
        {
            MC_AWAIT(mPermReceiver.apply<u8>(op, in, soutperm, chl));
            MC_AWAIT(mPermSender.apply<u8>(op, soutperm, out, chl));
        }
        else
        {
            MC_AWAIT(mPermSender.apply<u8>(op, in, soutperm, chl));
            MC_AWAIT(mPermReceiver.apply<u8>(op, soutperm, out, chl));
        }

        MC_END();
    }


    macoro::task<> ComposedPerm::derandomize(
        AdditivePerm& newPerm, 
        coproto::Socket& chl)
    {
        MC_BEGIN(macoro::task<>, this, &newPerm, &chl,
            d = AdditivePerm{},
            p0 = Perm{}
            );
        // the parties have random 
        // 
        //  p = p1 o p0
        // 
        // but we want composed perm of newPerm
        //
        //  newPerm = p1' o p0'
        // 
        // we will set p1' = p1 and update p0. First
        // we reveal 
        //  
        //  p0' = p1^1 o newPerm
        // 
        // to party 0 who will derandomize p0 to p0'.
        if (mPartyIdx)
        {
            d.mShare.resize(newPerm.size());

            // d = p1^-1 o newPerm
            MC_AWAIT(mPermSender.apply(
                PermOp::Inverse, 
                matrixCast<const u8>(newPerm.mShare), 
                matrixCast<u8>(d.mShare),
                chl));

            MC_AWAIT(chl.send(std::move(d.mShare)));


            MC_AWAIT(mPermReceiver.derandomize(chl));
        }
        else
        {
            d.mShare.resize(newPerm.size());

            // d = p1^-1 o newPerm
            MC_AWAIT(mPermReceiver.apply(
                PermOp::Inverse,
                matrixCast<const u8>(newPerm.mShare),
                matrixCast<u8>(d.mShare),
                chl));
            
            static_assert(sizeof(*p0.mPi.data()) == sizeof(*d.mShare.data()), "AdditivePerm and Perm should have the same size");
            p0.mPi.resize(newPerm.size());
            MC_AWAIT(chl.recv(p0.mPi));

            for (u64 i = 0; i < p0.size(); ++i)
                p0.mPi[i] ^= d.mShare[i];

            // derandomize old p0 to be p0'
            MC_AWAIT(mPermSender.derandomize(p0, chl));
        }
        MC_END();
    }
}
