#pragma once
#include "secure-join/config.h"
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/CorGenerator.h"

#include "cryptoTools/Common/BitIterator.h"
#include <bitset>
#include "libOTe/Tools/Tools.h"
#include "macoro/optional.h"
#include "F2LinearCode.h"
#include "F3LinearCode.h"

#include "AltModPrf.h"
#include "AltModKeyMult.h"
#include "ConvertToF3.h"

// TODO:
// * implement batching for large number of inputs. Will get 
//   better data locality.
// 

namespace secJoin
{

    // The "sender" half of the AltMod PRF protocol.
    // The sender will hold the key either in plaintext
    // for in secret shared format.
    // The sender will either know nothing about the input or 
    // have it in secret shared format.
    // The output will always be a secret sharing of 
    // 
    //    y = AltModPrf(k, x)
    //
    class AltModWPrfSender : public oc::TimerAdapter
    {
    public:

        AltModWPrfSender() = default;
        AltModWPrfSender(const AltModWPrfSender&) = delete;
        AltModWPrfSender(AltModWPrfSender&&) noexcept = default;
        AltModWPrfSender& operator=(const AltModWPrfSender&) = delete;
        AltModWPrfSender& operator=(AltModWPrfSender&&) noexcept = default;


        // enables additional debugging, insecure if true.
        bool mDebug = false;

        // The key OTs, one for each bit of the sender's key [share].
        AltModKeyMultReceiver mKeyMultRecver;

        //  The key OTs, if in fully secret shared mode, we need to multiply our input with their key.
        AltModKeyMultSender mKeyMultSender;

        AltModPrf::Offset mOffset{};

        // The number of input we will have.
        u64 mInputSize = 0;

        // The Ole request that will be used for the mod2 operation
        BinOleRequest mMod2OleReq;

        // the 1-oo-4 OT request that will be used for the mod2 operations
        Request<F4BitOtSend> mMod2F4Req;

        // a flag used to control which mod2 protocol to use, F4 is faster.
        bool mUseMod2F4Ot = true;

        // when the input & key is shared, we need to convert the expanded input
        // from a F2 sharing to an F3 sharing. This is acheived using this subproto.
        ConvertToF3Sender mConvToF3;

        // A flag to determine if the key will be secret shared.
        AltModPrfKeyMode mKeyMode = AltModPrfKeyMode::SenderOnly;

        // A flag to determine if the input will be secret shared.
        AltModPrfInputMode mInputMode = AltModPrfInputMode::ReceiverOnly;

        // variables that are used for debugging.
        std::vector<block> mDebugInput;
        oc::Matrix<oc::block> 
            mDebugEk0, mDebugEk1,
            mDebugU0, mDebugU1, 
            mDebugV;

        // initialize the protocol to perform inputSize prf evals.
        // correlations will be obtained from ole.
        // The caller can specify the key [share] and optionally
        // OTs associated with the key [share]. If not specify, they
        // will be generated internally.
        void init(
            u64 inputSize,
            CorGenerator& ole,
            AltModPrfKeyMode keyMode = AltModPrfKeyMode::SenderOnly,
            AltModPrfInputMode inputMode = AltModPrfInputMode::ReceiverOnly,
            macoro::optional<AltModPrf::KeyType> key = {},
            span<const block> keyRecvOts = {},
            span<const std::array<block, 2>> keySendOts = {});

        // clear the state. Removes any key that is set can cancels the prepro (if any).
        void clear();

        // perform the correlated randomness generation. 
        void preprocess();

        // explicitly set the key and key OTs. sendOTs are required only for shared key mode.
        void setKeyOts(
            AltModPrf::KeyType k, 
            span<const oc::block> ots,
            span<const std::array<block, 2>> sendOts = {});

        // return the key that is currently set.
        AltModPrf::KeyType getKey() const
        {
            if (mKeyMultRecver.mRecvKeyReq.size() || mKeyMultRecver.mKeyRecvOTs.empty())
                throw std::logic_error("PRF key is unavailable before key generation");
            return { mKeyMultRecver.mKey, mOffset };
        }

        // Run the prf protocol and write the result to y. Requires that correlated 
        // randomness has already been requested using the request() function.
        // if in shared input mode, x should be a share of the input. Otherwise empty.
        coproto::task<> evaluate(
            span<const oc::block> x,
            span<oc::block> y,
            coproto::Socket& sock,
            PRNG& _);

        // Evaluate already bit-sliced 512-bit inputs. Input and output storage
        // must remain alive until the task completes.
        coproto::task<> evaluateExpanded(oc::MatrixView<const block> x, span<block> y, coproto::Socket& sock);

    private:
        coproto::task<> evaluateImpl(span<const block> x, oc::MatrixView<const block> expanded,
            span<block> y, coproto::Socket& sock);

    public:


        // the mod 2 subprotocol based on OLE. 
        macoro::task<> mod2Ole(
            oc::MatrixView<const oc::block> u0,
            oc::MatrixView<const oc::block> u1,
            oc::MatrixView<oc::block> out,
            coproto::Socket& sock);


        // the mod 2 subprotocol based on F4 OT.
        macoro::task<> mod2OtF4(
            oc::MatrixView<const oc::block> u0,
            oc::MatrixView<const oc::block> u1,
            oc::MatrixView<oc::block> out,
            coproto::Socket& sock);


    };



    // The "receiver" half of the AltMod PRF protocol.
    // The receiver will hold the input either in plaintext
    // for in secret shared format.
    // The receiver will either know nothing about the key or 
    // have it in secret shared format.
    // The output will always be a secret sharing of 
    // 
    //    y = AltModPrf(k, x)
    //
    class AltModWPrfReceiver : public oc::TimerAdapter
    {
    public:

        AltModWPrfReceiver() = default;
        AltModWPrfReceiver(const AltModWPrfReceiver&) = delete;
        AltModWPrfReceiver(AltModWPrfReceiver&&) = default;
        AltModWPrfReceiver& operator=(const AltModWPrfReceiver&) = delete;
        AltModWPrfReceiver& operator=(AltModWPrfReceiver&&) noexcept = default;

        // enables additional debugging, insecure if true.
        bool mDebug = false;

        // The key OTs, we need to multiply our input with their key.
        AltModKeyMultSender mKeyMultSender;

        // When the key is shared, this will hold the OTs with one for each bit of our key.
        AltModKeyMultReceiver mKeyMultRecver;

        AltModPrf::Offset mOffset{};

        // The number of input we will have.
        u64 mInputSize = 0;

        // The Ole request that will be used for the mod2 operation
        BinOleRequest mMod2OleReq;

        // The F4Bit OTs request that will be used for the mod2 operation
        Request<F4BitOtRecv> mMod2F4Req;

        // a flag to control which mod 2 protocol to use. F4 is faster.
        bool mUseMod2F4Ot = true;

        // When the inputs/key are shared, we need to convert the F2 
        // expanded into into F3 shares. This is acheived using this proto.
        ConvertToF3Recver mConvToF3;

        // a flag determining if the key is shared.
        AltModPrfKeyMode mKeyMode = AltModPrfKeyMode::SenderOnly;

        // a flag determining if the input is shared.
        AltModPrfInputMode mInputMode = AltModPrfInputMode::ReceiverOnly;

        // variables that are used for debugging.
        oc::Matrix<oc::block> 
            mDebugEk0, mDebugEk1,
            mDebugU0, mDebugU1, 
            mDebugV;

        std::vector<block> mDebugInput;


        // clears any internal state.
        void clear();

        // initialize the protocol to perform inputSize prf evals.
        // set keyGen if you explicitly want to perform (or not) the 
        // key generation. default = perform if not already set.
        // keyShare is a share of the key. If not set, then the sender
        // will hold a plaintext key. keyOts is will base OTs for the
        // sender's key (share).
        void init(u64 size,
            CorGenerator& ole,
            AltModPrfKeyMode keyMode = AltModPrfKeyMode::SenderOnly,
            AltModPrfInputMode inputMode = AltModPrfInputMode::ReceiverOnly,
            std::optional<AltModPrf::KeyType> keyShare = {},
            span<const std::array<block, 2>> keyOts = {},
            span<const block> keyRecvOts = {});

        // Perform the preprocessing for the correlated randomness and key gen (if requested).
        void preprocess();

        // Run the prf protocol and write the result to y. Requires that correlated 
        // randomness has already been requested using the request() function.
        coproto::task<> evaluate(
            span<const oc::block> x,
            span<oc::block> y,
            coproto::Socket& sock,
            PRNG&);

        // Evaluate already bit-sliced 512-bit inputs. Input and output storage
        // must remain alive until the task completes.
        coproto::task<> evaluateExpanded(oc::MatrixView<const block> x, span<block> y, coproto::Socket& sock);

    private:
        coproto::task<> evaluateImpl(span<const block> x, oc::MatrixView<const block> expanded,
            span<block> y, coproto::Socket& sock);

    public:

        // the mod 2 subprotocol based on ole.
        macoro::task<> mod2Ole(
            oc::MatrixView<const oc::block> u0,
            oc::MatrixView<const oc::block> u1,
            oc::MatrixView<oc::block> out,
            coproto::Socket& sock);

        // the mod 2 subprotocol based on ole.
        macoro::task<> mod2OtF4(
            oc::MatrixView<const oc::block> u0,
            oc::MatrixView<const oc::block> u1,
            oc::MatrixView<oc::block> out,
            coproto::Socket& sock);

        void setKeyOts(
            span<const std::array<block, 2>> ots,
            std::optional<AltModPrf::KeyType> keyShare = {},
            span<const block> recvOts = {});


        // return the key that is currently set.
        std::optional<AltModPrf::KeyType> getKey() const
        {
            if (mKeyMode == AltModPrfKeyMode::SenderOnly || !mKeyMultSender.mOptionalKeyShare)
                return {};
            return AltModPrf::KeyType{ *mKeyMultSender.mOptionalKeyShare, mOffset };
        }

    };



    // on binary input x0 from the sender
    // on binary input x1 from the receiver
    // generate an F3 sharing of x0*x1. 
    // y0 will hold the lsb, and y1 will hold the msb.
    // The trit request size should be the bit length of x0,x1
    // This is the OT sender half of the protocol.
    macoro::task<> keyMultCorrectionSend(
        Request<TritOtSend>& request,
        oc::MatrixView<const oc::block> x,
        oc::Matrix<oc::block>& y0,
        oc::Matrix<oc::block>& y1,
        coproto::Socket& sock,
        bool debug);

    // on binary input x0 from the sender
    // on binary input x1 from the receiver
    // generate an F3 sharing of x0*x1. 
    // y0 will hold the lsb, and y1 will hold the msb.
    // The trit request size should be the bit length of x0,x1
    // This is the OT receiver half of the protocol.
    macoro::task<> keyMultCorrectionRecv(
        Request<TritOtRecv>& request,
        oc::MatrixView<const oc::block> x,
        oc::Matrix<oc::block>& y0,
        oc::Matrix<oc::block>& y1,
        coproto::Socket& sock,
        bool debug);
}
