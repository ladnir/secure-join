#include "secure-join/Defines.h"
#include "mod3.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Common/Aligned.h"
#include "secure-join/Util/Simd.h"
#include <bit>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace secJoin
{

	// generated using buildMod3Tabel. Each of the first 243=3^5 positions
	// contains 5 mod-3 values. the first byte of each entry contains the 
	//5 lsb. the second byte contains the 5 msb. The third byte contains a 
	// zero-one flag indicating if this sample less that 243 ad therefore
	// valid. The idea of the sample is that it takes as input a random byte 
	// and returns 5 random mod3 values or bot.
	const std::array<u32, 256> mod3Table =
	{ {
		0x10000, 0x10001, 0x10100, 0x10002, 0x10003, 0x10102, 0x10200, 0x10201,
		0x10300, 0x10004, 0x10005, 0x10104, 0x10006, 0x10007, 0x10106, 0x10204,
		0x10205, 0x10304, 0x10400, 0x10401, 0x10500, 0x10402, 0x10403, 0x10502,
		0x10600, 0x10601, 0x10700, 0x10008, 0x10009, 0x10108, 0x1000a, 0x1000b,
		0x1010a, 0x10208, 0x10209, 0x10308, 0x1000c, 0x1000d, 0x1010c, 0x1000e,
		0x1000f, 0x1010e, 0x1020c, 0x1020d, 0x1030c, 0x10408, 0x10409, 0x10508,
		0x1040a, 0x1040b, 0x1050a, 0x10608, 0x10609, 0x10708, 0x10800, 0x10801,
		0x10900, 0x10802, 0x10803, 0x10902, 0x10a00, 0x10a01, 0x10b00, 0x10804,
		0x10805, 0x10904, 0x10806, 0x10807, 0x10906, 0x10a04, 0x10a05, 0x10b04,
		0x10c00, 0x10c01, 0x10d00, 0x10c02, 0x10c03, 0x10d02, 0x10e00, 0x10e01,
		0x10f00, 0x10010, 0x10011, 0x10110, 0x10012, 0x10013, 0x10112, 0x10210,
		0x10211, 0x10310, 0x10014, 0x10015, 0x10114, 0x10016, 0x10017, 0x10116,
		0x10214, 0x10215, 0x10314, 0x10410, 0x10411, 0x10510, 0x10412, 0x10413,
		0x10512, 0x10610, 0x10611, 0x10710, 0x10018, 0x10019, 0x10118, 0x1001a,
		0x1001b, 0x1011a, 0x10218, 0x10219, 0x10318, 0x1001c, 0x1001d, 0x1011c,
		0x1001e, 0x1001f, 0x1011e, 0x1021c, 0x1021d, 0x1031c, 0x10418, 0x10419,
		0x10518, 0x1041a, 0x1041b, 0x1051a, 0x10618, 0x10619, 0x10718, 0x10810,
		0x10811, 0x10910, 0x10812, 0x10813, 0x10912, 0x10a10, 0x10a11, 0x10b10,
		0x10814, 0x10815, 0x10914, 0x10816, 0x10817, 0x10916, 0x10a14, 0x10a15,
		0x10b14, 0x10c10, 0x10c11, 0x10d10, 0x10c12, 0x10c13, 0x10d12, 0x10e10,
		0x10e11, 0x10f10, 0x11000, 0x11001, 0x11100, 0x11002, 0x11003, 0x11102,
		0x11200, 0x11201, 0x11300, 0x11004, 0x11005, 0x11104, 0x11006, 0x11007,
		0x11106, 0x11204, 0x11205, 0x11304, 0x11400, 0x11401, 0x11500, 0x11402,
		0x11403, 0x11502, 0x11600, 0x11601, 0x11700, 0x11008, 0x11009, 0x11108,
		0x1100a, 0x1100b, 0x1110a, 0x11208, 0x11209, 0x11308, 0x1100c, 0x1100d,
		0x1110c, 0x1100e, 0x1100f, 0x1110e, 0x1120c, 0x1120d, 0x1130c, 0x11408,
		0x11409, 0x11508, 0x1140a, 0x1140b, 0x1150a, 0x11608, 0x11609, 0x11708,
		0x11800, 0x11801, 0x11900, 0x11802, 0x11803, 0x11902, 0x11a00, 0x11a01,
		0x11b00, 0x11804, 0x11805, 0x11904, 0x11806, 0x11807, 0x11906, 0x11a04,
		0x11a05, 0x11b04, 0x11c00, 0x11c01, 0x11d00, 0x11c02, 0x11c03, 0x11d02,
		0x11e00, 0x11e01, 0x11f00, 0x0, 0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
} };

	const std::array<u32, 256> mod3TableV{
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,5,5,5,5,5,
		5,5,5,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
	};

	const std::array<u32, 256> mod3TableLsb{ {
	 0b00000, 0b00001, 0b00000, 0b00010, 0b00011, 0b00010, 0b00000, 0b00001,
	 0b00000, 0b00100, 0b00101, 0b00100, 0b00110, 0b00111, 0b00110, 0b00100,
	 0b00101, 0b00100, 0b00000, 0b00001, 0b00000, 0b00010, 0b00011, 0b00010,
	 0b00000, 0b00001, 0b00000, 0b01000, 0b01001, 0b01000, 0b01010, 0b01011,
	 0b01010, 0b01000, 0b01001, 0b01000, 0b01100, 0b01101, 0b01100, 0b01110,
	 0b01111, 0b01110, 0b01100, 0b01101, 0b01100, 0b01000, 0b01001, 0b01000,
	 0b01010, 0b01011, 0b01010, 0b01000, 0b01001, 0b01000, 0b00000, 0b00001,
	 0b00000, 0b00010, 0b00011, 0b00010, 0b00000, 0b00001, 0b00000, 0b00100,
	 0b00101, 0b00100, 0b00110, 0b00111, 0b00110, 0b00100, 0b00101, 0b00100,
	 0b00000, 0b00001, 0b00000, 0b00010, 0b00011, 0b00010, 0b00000, 0b00001,
	 0b00000, 0b10000, 0b10001, 0b10000, 0b10010, 0b10011, 0b10010, 0b10000,
	 0b10001, 0b10000, 0b10100, 0b10101, 0b10100, 0b10110, 0b10111, 0b10110,
	 0b10100, 0b10101, 0b10100, 0b10000, 0b10001, 0b10000, 0b10010, 0b10011,
	 0b10010, 0b10000, 0b10001, 0b10000, 0b11000, 0b11001, 0b11000, 0b11010,
	 0b11011, 0b11010, 0b11000, 0b11001, 0b11000, 0b11100, 0b11101, 0b11100,
	 0b11110, 0b11111, 0b11110, 0b11100, 0b11101, 0b11100, 0b11000, 0b11001,
	 0b11000, 0b11010, 0b11011, 0b11010, 0b11000, 0b11001, 0b11000, 0b10000,
	 0b10001, 0b10000, 0b10010, 0b10011, 0b10010, 0b10000, 0b10001, 0b10000,
	 0b10100, 0b10101, 0b10100, 0b10110, 0b10111, 0b10110, 0b10100, 0b10101,
	 0b10100, 0b10000, 0b10001, 0b10000, 0b10010, 0b10011, 0b10010, 0b10000,
	 0b10001, 0b10000, 0b00000, 0b00001, 0b00000, 0b00010, 0b00011, 0b00010,
	 0b00000, 0b00001, 0b00000, 0b00100, 0b00101, 0b00100, 0b00110, 0b00111,
	 0b00110, 0b00100, 0b00101, 0b00100, 0b00000, 0b00001, 0b00000, 0b00010,
	 0b00011, 0b00010, 0b00000, 0b00001, 0b00000, 0b01000, 0b01001, 0b01000,
	 0b01010, 0b01011, 0b01010, 0b01000, 0b01001, 0b01000, 0b01100, 0b01101,
	 0b01100, 0b01110, 0b01111, 0b01110, 0b01100, 0b01101, 0b01100, 0b01000,
	 0b01001, 0b01000, 0b01010, 0b01011, 0b01010, 0b01000, 0b01001, 0b01000,
	 0b00000, 0b00001, 0b00000, 0b00010, 0b00011, 0b00010, 0b00000, 0b00001,
	 0b00000, 0b00100, 0b00101, 0b00100, 0b00110, 0b00111, 0b00110, 0b00100,
	 0b00101, 0b00100, 0b00000, 0b00001, 0b00000, 0b00010, 0b00011, 0b00010,
	 0b00000, 0b00001, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
	 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
	} };

	const std::array<u32, 256> mod3TableMsb{ {
	 0b00000, 0b00000, 0b00001, 0b00000, 0b00000, 0b00001, 0b00010, 0b00010,
	 0b00011, 0b00000, 0b00000, 0b00001, 0b00000, 0b00000, 0b00001, 0b00010,
	 0b00010, 0b00011, 0b00100, 0b00100, 0b00101, 0b00100, 0b00100, 0b00101,
	 0b00110, 0b00110, 0b00111, 0b00000, 0b00000, 0b00001, 0b00000, 0b00000,
	 0b00001, 0b00010, 0b00010, 0b00011, 0b00000, 0b00000, 0b00001, 0b00000,
	 0b00000, 0b00001, 0b00010, 0b00010, 0b00011, 0b00100, 0b00100, 0b00101,
	 0b00100, 0b00100, 0b00101, 0b00110, 0b00110, 0b00111, 0b01000, 0b01000,
	 0b01001, 0b01000, 0b01000, 0b01001, 0b01010, 0b01010, 0b01011, 0b01000,
	 0b01000, 0b01001, 0b01000, 0b01000, 0b01001, 0b01010, 0b01010, 0b01011,
	 0b01100, 0b01100, 0b01101, 0b01100, 0b01100, 0b01101, 0b01110, 0b01110,
	 0b01111, 0b00000, 0b00000, 0b00001, 0b00000, 0b00000, 0b00001, 0b00010,
	 0b00010, 0b00011, 0b00000, 0b00000, 0b00001, 0b00000, 0b00000, 0b00001,
	 0b00010, 0b00010, 0b00011, 0b00100, 0b00100, 0b00101, 0b00100, 0b00100,
	 0b00101, 0b00110, 0b00110, 0b00111, 0b00000, 0b00000, 0b00001, 0b00000,
	 0b00000, 0b00001, 0b00010, 0b00010, 0b00011, 0b00000, 0b00000, 0b00001,
	 0b00000, 0b00000, 0b00001, 0b00010, 0b00010, 0b00011, 0b00100, 0b00100,
	 0b00101, 0b00100, 0b00100, 0b00101, 0b00110, 0b00110, 0b00111, 0b01000,
	 0b01000, 0b01001, 0b01000, 0b01000, 0b01001, 0b01010, 0b01010, 0b01011,
	 0b01000, 0b01000, 0b01001, 0b01000, 0b01000, 0b01001, 0b01010, 0b01010,
	 0b01011, 0b01100, 0b01100, 0b01101, 0b01100, 0b01100, 0b01101, 0b01110,
	 0b01110, 0b01111, 0b10000, 0b10000, 0b10001, 0b10000, 0b10000, 0b10001,
	 0b10010, 0b10010, 0b10011, 0b10000, 0b10000, 0b10001, 0b10000, 0b10000,
	 0b10001, 0b10010, 0b10010, 0b10011, 0b10100, 0b10100, 0b10101, 0b10100,
	 0b10100, 0b10101, 0b10110, 0b10110, 0b10111, 0b10000, 0b10000, 0b10001,
	 0b10000, 0b10000, 0b10001, 0b10010, 0b10010, 0b10011, 0b10000, 0b10000,
	 0b10001, 0b10000, 0b10000, 0b10001, 0b10010, 0b10010, 0b10011, 0b10100,
	 0b10100, 0b10101, 0b10100, 0b10100, 0b10101, 0b10110, 0b10110, 0b10111,
	 0b11000, 0b11000, 0b11001, 0b11000, 0b11000, 0b11001, 0b11010, 0b11010,
	 0b11011, 0b11000, 0b11000, 0b11001, 0b11000, 0b11000, 0b11001, 0b11010,
	 0b11010, 0b11011, 0b11100, 0b11100, 0b11101, 0b11100, 0b11100, 0b11101,
	 0b11110, 0b11110, 0b11111, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
	 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
	} };


	void buildMod3Table()
	{
		std::array<u32, 256> ret;
		u64 m = 243;
		u64 s = 5;
		u32 vals[5];
		for (u64 j = 0; j < s; ++j)
			vals[j] = 0;
		for (u64 i = 0; i < 256; ++i)
		{
			if (i < m)
			{
				u32 lsb = 0;
				u32 msb = 0;

				for (u64 j = 0; j < s; ++j)
				{
					auto lsbj = vals[j] & 1;
					auto msbj = (vals[j] >> 1) & 1;
					lsb |= lsbj << j;
					msb |= msbj << j;
					//std::cout << vals[j] << "(" <<msbj<<"" << lsbj << "), ";
				}
				ret[i] = ((1 << 16) + (msb << 8) + lsb);

				++vals[0];
				for (u64 j = 0; j < s; ++j)
				{
					if (vals[j] == 3 && j != s - 1)
						vals[j + 1]++;
					vals[j] = vals[j] % 3;
				}

			}
			else
			{
				ret[i] = 0;
				//std::cout << "0," << std::endl;
			}

			std::cout << "0x" << std::hex << ret[i] << ",";
			if (i % 8 == 0)
				std::cout << std::endl;
		}
		//return ret;
	};


	void buildMod3Table2()
	{
		std::array<u32, 256> ret;
		u64 m = 243;
		u64 s = 5;
		u32 vals[5];
		for (u64 j = 0; j < s; ++j)
			vals[j] = 0;

		std::cout << "const std::array<u32, 256> mod3TableV { {\n";
		for (u64 i = 0; i < 256; ++i)
		{
			if (i < m)
				std::cout << "5,";
			else
				std::cout << "0,";

			if (i % 8 == 7)
				std::cout << std::endl;
		}
		std::cout << "}};\n\n";


		for (u64 l = 0; l < 3; ++l)
		{
			if (l == 0)
				std::cout << "const std::array<u32, 256> mod3TableLsb { {\n";
			else if (l == 1)
				std::cout << "const std::array<u32, 256> mod3TableMsb {{ \n";
			else
				std::cout << "const std::array<std::array<u8, 5>, 256> mod3TableFull {{ \n";

			//char delim = ' ';
			for (u64 i = 0; i < 256; ++i)
			{
				if (i < m)
				{
					u32 lsb = 0;
					u32 msb = 0;

					for (u64 j = 0; j < s; ++j)
					{
						auto lsbj = vals[j] & 1;
						auto msbj = (vals[j] >> 1) & 1;
						lsb |= lsbj << j;
						msb |= msbj << j;
						//std::cout << vals[j] << "(" <<msbj<<"" << lsbj << "), ";
					}

					if (l == 0)
						ret[i] = lsb;
					else if (l == 1)
						ret[i] = msb;


					if (l == 2)
					{
						std::cout << "{";
						char d = ' ';
						for (u64 j = 0; j < s; ++j)
						{
							std::cout << std::exchange(d, ',') << ' ' << vals[j];
						}
						std::cout << "},";
					}
					else
					{
						oc::BitVector vv((u8*)&ret[i], 5);
						std::cout << " 0b" << vv[4] << vv[3] << vv[2] << vv[1] << vv[0] << ",";
					}
					//ret[i] = ((1 << 16) + (msb << 8) + lsb);

					++vals[0];
					for (u64 j = 0; j < s; ++j)
					{
						if (vals[j] == 3 && j != s - 1)
							vals[j + 1]++;
						vals[j] = vals[j] % 3;
					}

				}
				else
				{
					ret[i] = 0;
					//std::cout << "0," << std::endl;
					if (l < 2)
						std::cout << " 0b00000,";
					else
					{
						std::cout << "{  0, 0, 0, 0, 0},";
					}
				}


				if (i % 8 == 7)
					std::cout << std::endl;

			}

			std::cout << "}};\n\n" << std::dec;
		}
	};

#ifdef ENABLE_SSE
	struct SampleBlock256
	{
		__m256i mVal;

		void zero() {
			mVal = _mm256_setzero_si256();
		}

		SampleBlock256() = default;
		SampleBlock256(const __m256i& v)
			: mVal(v)
		{}

		SampleBlock256(SampleBlock256* v)
		{
			load(v);
		}
		void load(SampleBlock256* v)
		{
			mVal = _mm256_load_si256(&v->mVal);
		}

		static auto set32(u32 v) 
		{
			return _mm256_set1_epi32(v);
		}

		SampleBlock256 operator&(const SampleBlock256& o) const
		{
			return _mm256_and_si256(mVal, o.mVal);
		}


		SampleBlock256 operator^(const SampleBlock256& o) const
		{
			return _mm256_xor_epi32(mVal, o.mVal);
		}

		SampleBlock256 operator|(const SampleBlock256& o) const
		{
			return _mm256_or_si256(mVal, o.mVal);
		}
		

		template<int s>
		SampleBlock256 srli_epi32() const
		{
			return _mm256_srli_epi32(mVal, s);
		}


		SampleBlock256 sllv_epi32(const SampleBlock256&s) const
		{
			return _mm256_sllv_epi32(mVal, s.mVal);
		}

		template<int v> 
		static SampleBlock256 i32gather_epi32(const int* ptr, const SampleBlock256& index)
		{
			return _mm256_i32gather_epi32(ptr, index.mVal, v);
		}

		int testz_si256(const SampleBlock256& o) const
		{
			return _mm256_testz_si256(mVal, o.mVal);
		}


		SampleBlock256 add_epi32(const SampleBlock256& s) const
		{
			return _mm256_add_epi32(mVal, s.mVal);
		}
		
	};
#else

	struct SampleBlock256
	{

		block mVal[2];

		void zero() {
			mVal[0] = block::allSame(0);
			mVal[1] = block::allSame(0);
		}

		SampleBlock256() = default;

		SampleBlock256(SampleBlock256* v)
		{
			load(v);
		}
		void load(SampleBlock256* v)
		{
			auto lanes = std::bit_cast<std::array<block, 2>>(*v);
			mVal[0] = lanes[0];
			mVal[1] = lanes[1];
		}

		static auto set32(u32 v)
		{
			SampleBlock256 r;
			r.mVal[0] = block::allSame<i32>(v);
			r.mVal[1] = block::allSame<i32>(v);
			return r;
		}

		SampleBlock256 operator&(const SampleBlock256& o) const
		{
			SampleBlock256 r;
			r.mVal[0] = mVal[0] & o.mVal[0];
			r.mVal[1] = mVal[1] & o.mVal[1];
			return r;
		}


		SampleBlock256 operator^(const SampleBlock256& o) const
		{
			SampleBlock256 r;
			r.mVal[0] = mVal[0] ^ o.mVal[0];
			r.mVal[1] = mVal[1] ^ o.mVal[1];
			return r;
		}

		SampleBlock256 operator|(const SampleBlock256& o) const
		{
			SampleBlock256 r;
			r.mVal[0] = mVal[0] | o.mVal[0];
			r.mVal[1] = mVal[1] | o.mVal[1];
			return r;
		}


		template<int s>
		SampleBlock256 srli_epi32() const
		{
			auto src = std::bit_cast<std::array<i32, 8>>(*this);
			std::array<i32, 8> dst{};
			for (u64 i = 0; i < 8; ++i)
				dst[i] = src[i] >> s;
			return std::bit_cast<SampleBlock256>(dst);
		}


		SampleBlock256 sllv_epi32(const SampleBlock256& s) const
		{
			auto src = std::bit_cast<std::array<i32, 8>>(*this);
			auto sh = std::bit_cast<std::array<i32, 8>>(s);
			std::array<i32, 8> dst{};
			for (u64 i = 0; i < 8; ++i)
				dst[i] = src[i] << sh[i];
			return std::bit_cast<SampleBlock256>(dst);
		}

		template<int v>
		static SampleBlock256 i32gather_epi32(const int* ptr, const SampleBlock256& index)
		{
			auto idx = std::bit_cast<std::array<i32, 8>>(index);
			std::array<i32, 8> dst{};
			for (u64 i = 0; i < 8; ++i)
			{
				i32 lane;
				std::memcpy(&lane, reinterpret_cast<const char*>(ptr) + static_cast<std::ptrdiff_t>(idx[i]) * v, sizeof(lane));
				dst[i] = lane;
			}
			return std::bit_cast<SampleBlock256>(dst);
		}

		int testz_si256(const SampleBlock256& o) const
		{
			return
				((mVal[0] & o.mVal[0]) |
				(mVal[1] & o.mVal[1])) == oc::ZeroBlock;

			//return _mm256_testz_si256(mVal, o.mVal);
		}


		SampleBlock256 add_epi32(const SampleBlock256& s) const
		{
			auto src = std::bit_cast<std::array<u32, 8>>(*this);
			auto rhs = std::bit_cast<std::array<u32, 8>>(s);
			std::array<u32, 8> dst{};
			for (u64 i = 0; i < 8; ++i)
				dst[i] = src[i] + rhs[i];
			return std::bit_cast<SampleBlock256>(dst);
		}

	};
#endif

	static_assert(std::is_trivially_copyable_v<SampleBlock256>);
	static_assert(sizeof(SampleBlock256) == sizeof(std::array<u32, 8>));

	void sampleMod3Lookup(PRNG& prng, span<block> msbVec, span<block> lsbVec)
	{
		//if ((u64)msbVec.data() % 32)
		//    throw RTE_LOC;// must be aligned.
		//if ((u64)lsbVec.data() % 32)
		//    throw RTE_LOC;// must be aligned.
		if (msbVec.size() != lsbVec.size())
			throw RTE_LOC;// must have same size.

		u64 n = msbVec.size();
		auto msbIter = msbVec.data();
		auto lsbIter = lsbVec.data();

		oc::AlignedArray<block, 128> rands;
		oc::AlignedArray<block, 8> rands2;

		auto e = (SampleBlock256*)(rands.data() + rands.size());
		auto e2 = (u8*)(rands2.data() + rands2.size());
		auto randsPtr = e;
		u8* rands2Ptr = e2;

		oc::AlignedArray<SampleBlock256, 4> lsbSum;
		oc::AlignedArray<SampleBlock256, 4> msbSum;
		oc::AlignedArray<SampleBlock256, 4> size, v_, lsb_, msb_;

		for (u64 i = 0; i < n; i += lsbSum.size() * 2)
		{
			for (u64 k = 0; k < lsbSum.size(); ++k)
			{
				lsbSum[k].zero();
				msbSum[k].zero();
				size[k].zero();
			}

			for (u64 k = 0; k < 8; ++k)
			{
				if (randsPtr >= e)
				{
					prng.mAes.ecbEncCounterMode(prng.mBlockIdx, rands.size(), rands.data());
					prng.mBlockIdx += rands.size();
					randsPtr = (SampleBlock256*)rands.data();
				}

				SampleBlock256 indexBase(randsPtr);
				for (u64 j = 0; j < lsbSum.size(); ++j)
				{
					auto indexes = indexBase & SampleBlock256::set32(255);
					indexBase = indexBase.srli_epi32<8>();

					//__m256i indexes = _mm256_set_epi32(
					//    randsPtr->m256i_u8[j * 8 + 7],
					//    randsPtr->m256i_u8[j * 8 + 6],
					//    randsPtr->m256i_u8[j * 8 + 5],
					//    randsPtr->m256i_u8[j * 8 + 4],
					//    randsPtr->m256i_u8[j * 8 + 3],
					//    randsPtr->m256i_u8[j * 8 + 2],
					//    randsPtr->m256i_u8[j * 8 + 1],
					//    randsPtr->m256i_u8[j * 8 + 0]
					//);

					v_[j] =   SampleBlock256::i32gather_epi32<4>((const i32*)mod3TableV.data(), indexes);
					lsb_[j] = SampleBlock256::i32gather_epi32<4>((const i32*)mod3TableLsb.data(), indexes);
					msb_[j] = SampleBlock256::i32gather_epi32<4>((const i32*)mod3TableMsb.data(), indexes);
				}
				randsPtr++;

				// shift the sum if we have a valid sample
				lsbSum[0] = lsbSum[0].sllv_epi32(v_[0]);
				lsbSum[1] = lsbSum[1].sllv_epi32(v_[1]);
				lsbSum[2] = lsbSum[2].sllv_epi32(v_[2]);
				lsbSum[3] = lsbSum[3].sllv_epi32(v_[3]);

				// 0 if there is overlap
				assert(lsbSum[0].testz_si256(lsb_[0]));
				assert(lsbSum[1].testz_si256(lsb_[1]));
				assert(lsbSum[2].testz_si256(lsb_[2]));
				assert(lsbSum[3].testz_si256(lsb_[3]));


				// add in the new sample
				lsbSum[0] = lsbSum[0] | lsb_[0];
				lsbSum[1] = lsbSum[1] | lsb_[1];
				lsbSum[2] = lsbSum[2] | lsb_[2];
				lsbSum[3] = lsbSum[3] | lsb_[3];

				// shift the sum if we have a valid sample
				msbSum[0] = msbSum[0].sllv_epi32(v_[0]);
				msbSum[1] = msbSum[1].sllv_epi32(v_[1]);
				msbSum[2] = msbSum[2].sllv_epi32(v_[2]);
				msbSum[3] = msbSum[3].sllv_epi32(v_[3]);

				// add in the new sample
				msbSum[0] = msbSum[0] | msb_[0];
				msbSum[1] = msbSum[1] | msb_[1];
				msbSum[2] = msbSum[2] | msb_[2];
				msbSum[3] = msbSum[3] | msb_[3];

				// 0 if there is overlap
				assert(lsbSum[0].testz_si256(msb_[0]));
				assert(lsbSum[1].testz_si256(msb_[1]));
				assert(lsbSum[2].testz_si256(msb_[2]));
				assert(lsbSum[3].testz_si256(msb_[3]));

				// add the size
				size[0] = size[0].add_epi32(v_[0]);
				size[1] = size[1].add_epi32(v_[1]);
				size[2] = size[2].add_epi32(v_[2]);
				size[3] = size[3].add_epi32(v_[3]);
			}

			for (u64 j = 0; j < lsbSum.size(); ++j)
			{
				// SampleBlock256 holds __m256i
				auto lsbLanes = std::bit_cast<std::array<u32, 8>>(lsbSum[j]);
				auto msbLanes = std::bit_cast<std::array<u32, 8>>(msbSum[j]);
				auto sizeLanes = std::bit_cast<std::array<u32, 8>>(size[j]);

				for (u64 k = 0; k < 8; ++k)
				{
					auto& lsbk = lsbLanes[k];
					auto& msbk = msbLanes[k];
					auto& sizek = sizeLanes[k];

					while (sizek < 32)
					{
						if (rands2Ptr == e2)
						{
							prng.mAes.ecbEncCounterMode(prng.mBlockIdx, rands2.size(), rands2.data());
							prng.mBlockIdx += rands2.size();
							rands2Ptr = (u8*)rands2.data();
						}

						auto b = *rands2Ptr++;
						auto v = mod3Table[b];

						auto lsbj = v & 255ull;
						auto msbj = (v >> 8) & 255ull;
						auto flag = 5 * (v >> 16);
						lsbk = (lsbk << flag) | lsbj;
						msbk = (msbk << flag) | msbj;

						sizek += flag;
					}
				}

				lsbSum[j] = std::bit_cast<SampleBlock256>(lsbLanes);
				msbSum[j] = std::bit_cast<SampleBlock256>(msbLanes);
				size[j] = std::bit_cast<SampleBlock256>(sizeLanes);
			}

			auto s = std::min<u64>(msbVec.size() - i, lsbSum.size() * 2);
			assert(lsbIter + s <= lsbVec.data() + lsbVec.size());
			assert(msbIter + s <= msbVec.data() + msbVec.size());
			std::copy((u8*)lsbSum.data(), (u8*)lsbSum.data() + s * sizeof(block), (u8*)lsbIter);
			std::copy((u8*)msbSum.data(), (u8*)msbSum.data() + s * sizeof(block), (u8*)msbIter);
			lsbIter += s;
			msbIter += s;

		}
	}


	void sample8Mod3(block* seed, u8& msb, u8& lsb)
	{
		// our strategy is to looks at each pair of bits.
		// the first one thats not 3 we take as our sample.
		// if no such pair exists, we output 0. There are 64 pairs
		// so we have (1/4)^64 = 2^{-128} failure Pr.
		lsb = 0;
		msb = 0;
		for (u64 j = 0; j < 8; ++j)
		{
			auto x = seed[j];
			auto lsbs = x.get<u64>(0);
			auto msbs = x.get<u64>(1);

			// a bit mask where 1 means the sample is valid, i.e. not 3.
			auto valid = ~(lsbs & msbs);

			// compute the index of the "first" valid mod3 sample. 
			// "first" here is defined as begin closest to the msb. 
			// we do this by counting the number of leading zeros.
			auto idx = 63 - oc::countl_zero(valid);
			auto l = (lsbs >> idx) & 1;
			auto m = (msbs >> idx) & 1;

			lsb |= (l << j);
			msb |= (m << j);
		}

	}



	void sampleMod3(span<block> seed, span<block> msbVec, span<block> lsbVec)
	{
		u64 n = seed.size();
		auto msbIter = (u8*)msbVec.data();
		auto lsbIter = (u8*)lsbVec.data();

		if (msbVec.size() * 128 != n)
			throw RTE_LOC;// must have same size.
		if (msbVec.size() != lsbVec.size())
			throw RTE_LOC;// must have same size.

		for (u64 i = 0; i < n; i += 8)
		{
			sample8Mod3(seed.data() + i, *msbIter++, *lsbIter++);
		}
	}

}
