#pragma once

#include "secure-join/config.h"
#ifdef SECUREJOIN_ENABLE_PAILLIER

#include "Paillier/Ciphertext.h"
#include "Paillier/Plaintext.h"
#include "Paillier/RNG.h"
#include "Paillier/Key.h"
#include "cryptoTools/Common/Matrix.h"
#include "coproto/coproto.h"

namespace secJoin
{
    namespace Pl = Paillier;
    // Paillier protocol
    // vector                  permutation
    // x1,...,xn               p1,..,pn
    //
    // Output
    // y1,...,yn               z1,...,zn
    //
    //  such that
    //
    //  yi + zi = x_pi
    //
    // P1 encrypts all xi.
    // P2 samples arndom zi
    // P2 permutes the xi's and adds zi after.
    // P2 sends back the result.
    // P1 decrypts and outputs the result, yi.
    //
    class PaillierPerm
    {
        public:


        // Returns the permutation protocol for P1. Must await the result to run  
        // the protocol. 
        //
        // x : the vector to be permuted. 
        // prng : Source of randomness
        // out : the output vector, x permuted by pi.
        // chl : the communication socket.
        macoro::task<> applyVec(std::vector<u64>& x, PRNG& prng, std::vector<u64>& out, coproto::Socket& chl)
        {
            MC_BEGIN(macoro::task<>, &x, &out, &chl,
                secparam = 2048,
                rand = Pl::RNG(prng.get()),
                mSk =Pl::PrivateKey{},
                pkBytes = std::vector<u8>{},
                msg = Pl::Plaintext{},
                ct = Pl::Ciphertext{},
                encFeaturesBytes = oc::Matrix<u8>{}
            );

            //generate & send Paillier key
            mSk.keyGen(secparam, rand);
            pkBytes.resize(mSk.mPublicKey.sizeBytes());
            mSk.mPublicKey.toBytes(pkBytes);
            MC_AWAIT(chl.send(std::move(pkBytes)));

            /*Encrypt the X vector */
            msg.setModulus(mSk.mPublicKey.mN);
            encFeaturesBytes.resize(x.size(), mSk.mPublicKey.ciphertextByteSize());
            for (u64 i = 0; i < x.size(); ++i)
            {
                msg.setValue(Pl::Integer(x[i]));

                //generate the ciphertext
                mSk.mPublicKey.enc(msg, rand, ct);

                //Convert the ciphertext to bytes
                ct.toBytes(encFeaturesBytes[i]);
            }

            MC_AWAIT(chl.send(std::move(encFeaturesBytes)));

            //Receive the encrypted mapped features from the server and decrypt
            encFeaturesBytes.resize(x.size(), mSk.mPublicKey.ciphertextByteSize());
            MC_AWAIT(chl.recv(encFeaturesBytes));

            // decrypt and write to output.
            out.resize(x.size());
            for (u64 i = 0; i < x.size(); ++i)
            {
                ct.fromBytes(encFeaturesBytes[i], mSk.mPublicKey);
                msg = mSk.dec(ct);
                out[i] = (i64) msg; 
            }

            // optionaly convert to binary secret shares.
            if(false)
                MC_AWAIT(convertToXor(out));

            MC_END();
        }

        macoro::task<> applyPerm(
            std::vector<u64>& pi,
            PRNG& prng,           
            std::vector<u64>& out, coproto::Socket& chl)
        {
            MC_BEGIN(macoro::task<>, &pi, &out,&chl,
                buff = std::vector<u8>{},
                mPk = Pl::PublicKey{},
                msg = Pl::Plaintext{},
                ct= Pl::Ciphertext{},
                encFeatures = std::vector<Pl::Ciphertext>{},
                encFeaturesBytes = oc::Matrix<u8>{},
                encMappedFeaturesBytes = oc::Matrix<u8>{},
                rand = Pl::RNG(prng.get())
            );

            // Receive the public key.
            MC_AWAIT(chl.recvResize(buff));
            mPk.fromBytes(buff);
            
            // Receive the ciphertexts
            encFeaturesBytes.resize(pi.size(), mPk.ciphertextByteSize());
            MC_AWAIT(chl.recv(encFeaturesBytes));
            encFeatures.resize(pi.size());
            for (u64 i = 0; i < encFeatures.size(); ++i)
            {   
                encFeatures[i].fromBytes(encFeaturesBytes[i], mPk);
            }

            // Perform the homomorphic computation
            msg.setModulus(mPk.mN);
            encMappedFeaturesBytes.resize(pi.size(), mPk.ciphertextByteSize());

            for (u64 j = 0; j < pi.size(); ++j)
            {
                //generate random value for out[j]
                msg.randomize(rand); 

                // encrypt the value
                mPk.enc(msg, rand, ct);

                // get the first 64 bits of msg. 
                out[j] = -(i64)msg;

                // Homomorphically add to EncFeatures[mMapping(i,j)];
                ct.add(ct, encFeatures[pi[j]]);

                //Convert the ciphertext to bytes
                ct.toBytes(encMappedFeaturesBytes[j]);
            }

            // send the mapped ciphertexts back.
            MC_AWAIT(chl.send(std::move(encMappedFeaturesBytes)));

            // optionaly convert to binary secret shares.
            if(false)
                MC_AWAIT(convertToXor(out));

            MC_END();
        }

        static macoro::task<> convertToXor(std::vector<u64>& shares)
        {
            // not implemented.
            throw RTE_LOC;
            // //Convert from additive to xor sharing using GMW
            // auto subCir = mLib.int_int_add(64, 64, 64);
            // u64 pIdx = isClient() ? 1 : 0;
            // auto features = oc::MatrixView<i64>(mMappedFeatures);
            // features.reshape(features.size(), 1);
            // mGmw.init(features.size(), *subCir, 1, pIdx, mPrng.get());

            // if(isClient())
            // {
            //     mGmw.setInput(0, features);
            //     mGmw.setZeroInput(1);
            // }
            // else
            // {
            //     mGmw.setZeroInput(0);
            //     mGmw.setInput(1, features);  
            // }   
            // mGmw.run(chl);
                
            // Matrix<u8> out;
            // out.resize(features.size(), sizeof(i64));
            // mGmw.getOutput(0, out); 
            // oc::MatrixView<u8> v((u8*)mMappedFeatures.data(), mMappedFeatures.size(), sizeof(i64));
            // for (int i = 0; i < v.size(); ++i)
            //     {   
            //         v(i) = out(i);
            //     }
        }
    };
}

#endif // SECUREJOIN_ENABLE_PAILLIER
