#include "common.hpp"

#include <cmath>
#include <random>
#include <utility>

using P = fpm::fixed_16_16;
using Q = fpm::fixed_24_8;

TEST(conversion, construction)
{
	P x{};
}

TEST(conversion, copy)
{
	const P x(12);

	// Copy ctor
	P y(x);
	EXPECT_EQ(P(12), y);

	// Copy assignment
	P z = x;
	EXPECT_EQ(P(12), y);
}

TEST(conversion, move)
{
	const P x(12);

	// Move ctor
	P y(std::move(x));
	EXPECT_EQ(P(12), y);

	// Move assignment
	P z = std::move(x);
	EXPECT_EQ(P(12), y);
}

TEST(conversion, floats)
{
	EXPECT_EQ(1.125, static_cast<double>(P{1.125f}));
	EXPECT_EQ(1.125, static_cast<double>(P{1.125}));
}

TEST(conversion, float_rounding)
{
	// Small number of fraction bits to test rounding
	using Q = fpm::fixed<int32_t, int64_t, 2>;

	EXPECT_EQ(1.25, static_cast<double>(Q{1.125}));
	EXPECT_EQ(1.5, static_cast<double>(Q{1.375}));
	EXPECT_EQ(-1.25, static_cast<double>(Q{-1.125}));
	EXPECT_EQ(-1.5, static_cast<double>(Q{-1.375}));
}

TEST(conversion, float_no_rounding)
{
	// Small number of fraction bits to test no rounding
	using Q = fpm::fixed<int32_t, int64_t, 2, false>;

	EXPECT_EQ(1.0, static_cast<double>(Q{1.125}));
	EXPECT_EQ(1.25, static_cast<double>(Q{1.375}));
	EXPECT_EQ(1.25, static_cast<double>(Q{1.499}));
	EXPECT_EQ(-1.0, static_cast<double>(Q{-1.125}));
	EXPECT_EQ(-1.0, static_cast<double>(Q{-1.249}));
	EXPECT_EQ(-1.25, static_cast<double>(Q{-1.375}));
}

TEST(conversion, ints)
{
	EXPECT_EQ(-125, static_cast<int>(P{-125}));
	EXPECT_EQ(-125l, static_cast<long>(P{-125l}));
	EXPECT_EQ(-125ll, static_cast<long long>(P{-125ll}));

	EXPECT_EQ(125u, static_cast<unsigned int>(P{125u}));
	EXPECT_EQ(125lu, static_cast<unsigned long>(P{125lu}));
	EXPECT_EQ(125llu, static_cast<unsigned long long>(P{125llu}));
}

TEST(conversion, fixed_point)
{
	EXPECT_EQ(P(-1), P::from_fixed_point<0>(-1));
	EXPECT_EQ(P(1), P::from_fixed_point<0>(1));

	EXPECT_EQ(P(-1.125), P::from_fixed_point<4>(-18));
	EXPECT_EQ(P(1.125), P::from_fixed_point<4>(18));

	// This should round up to 1
	EXPECT_EQ(P(-1), P::from_fixed_point<20>(-1048575));
	EXPECT_EQ(P(1), P::from_fixed_point<20>(1048575));
}

TEST(conversion, fixed_point_no_rounding)
{
	using P = fpm::fixed<int32_t, int64_t, 16, false>;
	constexpr P epsilon = std::numeric_limits<P>::epsilon();

	EXPECT_EQ(P(-1), P::from_fixed_point<0>(-1));
	EXPECT_EQ(P(1), P::from_fixed_point<0>(1));

	EXPECT_EQ(P(-1.125), P::from_fixed_point<4>(-18));
	EXPECT_EQ(P(1.125), P::from_fixed_point<4>(18));

	// This should NOT round up to 1: there will be a truncation error equal to epsilon
	EXPECT_EQ(P(-1 + epsilon), P::from_fixed_point<20>(-1048575));
	EXPECT_EQ(P(1 - epsilon), P::from_fixed_point<20>(1048575));
}

TEST(conversion, fixed_to_fixed)
{
	EXPECT_EQ(Q(1), Q(P(1)));
	EXPECT_EQ(Q(1), Q(P(1)));

	// Conversion to fewer fraction bits should round
	EXPECT_EQ(Q::from_raw_value(0x13), Q(P::from_raw_value(0x12ff)));
	EXPECT_EQ(Q::from_raw_value(0x12), Q(P::from_raw_value(0x127f)));
	EXPECT_EQ(Q::from_raw_value(-0x13), Q(P::from_raw_value(-0x12ff)));
	EXPECT_EQ(Q::from_raw_value(-0x12), Q(P::from_raw_value(-0x127f)));

	// Conversion to more fraction bits should zero-extend
	EXPECT_EQ(P::from_raw_value(0x1200), P(Q::from_raw_value(0x12)));
	EXPECT_EQ(P::from_raw_value(-0x1200), P(Q::from_raw_value(-0x12)));

	{
		// Assignment requires explicit conversion via construction
		const P p(1);
		const Q q = Q(p);
		EXPECT_EQ(Q(1), q);
	}

	// Conversion to a smaller base type should truncate the upper bits
	using S1 = fpm::fixed<int8_t, int16_t, 1>;
	EXPECT_EQ(0x56, S1(P::from_raw_value(0x79'AB'10'00)).raw_value());
	EXPECT_EQ(-0x56, S1(P::from_raw_value(-0x79'AB'10'00)).raw_value());
}

// Unsigned values with the top bit set keep their value in a signed type that can hold them
TEST(conversion, unsigned_to_signed)
{
	const fpm::fixed<uint16_t, uint32_t, 8> small{200};
	EXPECT_EQ((fpm::fixed<int32_t, int64_t, 4>{200}), (fpm::fixed<int32_t, int64_t, 4>(small)));
	EXPECT_EQ((fpm::fixed<int32_t, int64_t, 12>{200}), (fpm::fixed<int32_t, int64_t, 12>(small)));

#ifdef FPM_INT128
	const fpm::fixed<uint32_t, uint64_t, 16> large{40000};
	EXPECT_EQ((fpm::fixed<int64_t, FPM_INT128, 8>{40000}), (fpm::fixed<int64_t, FPM_INT128, 8>(large)));
	EXPECT_EQ((fpm::fixed<int64_t, FPM_INT128, 24>{40000}), (fpm::fixed<int64_t, FPM_INT128, 24>(large)));
#endif
}

namespace
{
	/// The raw value of a number, with exact arithmetic: rounded (ties away from zero) or truncated, modulo the range
	template<typename P>
	typename P::base_type expected_raw(const long double value)
	{
		// Without the multiples of the range first (which is exact), so the scaled number is not too large
		using B = typename P::base_type;
		const int bits = static_cast<int>(sizeof(B) * 8);
		const int fraction_bits = static_cast<int>(P::fraction_bits);
		const long double scaled = std::ldexp(std::fmod(std::abs(value), std::ldexp(1.0L, bits - fraction_bits)), fraction_bits); // in [0, 2^bits)
		long double whole = std::floor(scaled);
		if(P::enable_rounding && scaled - whole >= 0.5L)
			whole += 1;
		const auto magnitude = static_cast<uint64_t>(std::fmod(whole, std::ldexp(1.0L, bits))); // 2^bits (rounded up to) is 0
		return static_cast<B>(value < 0 ? 0 - magnitude : magnitude);
	}

	template<typename P, typename T>
	void test_beyond_range()
	{
		using B = typename P::base_type;
		std::mt19937_64 rng(P::fraction_bits * 100 + sizeof(B) + sizeof(T));
		const int integral = static_cast<int>(sizeof(B) * 8 - P::fraction_bits);

		std::vector<T> numbers{0, 1, -1, 0.5f, -0.5f, 0.25f, -0.75f, 1e9f, -1e9f, 1e30f, -1e30f,
			std::numeric_limits<T>::max(), std::numeric_limits<T>::lowest(), std::numeric_limits<T>::min(), -std::numeric_limits<T>::min()};
		// The ends of the range, and their multiples
		for(const int k : {-3, -2, -1, 1, 2, 3, 1000, -1000})
		{
			const auto end = static_cast<T>(std::ldexp(static_cast<long double>(k), integral - 1));
			for(const T offset : {T{0}, T{0.25}, T{-0.25}, T{0.5}, T{-0.5}, T{1}, T{-1}})
			{
				numbers.push_back(end + offset);
				numbers.push_back(std::nextafter(end + offset, T{0}));
				numbers.push_back(std::nextafter(end + offset, end * T{2}));
			}
		}
		// Of every magnitude
		for(int i = 0; i < 4000; ++i)
		{
			const int exponent = static_cast<int>(rng() % 140) - 50;
			const auto mantissa = static_cast<long double>(rng() >> 11) / 9007199254740992.0L + 1;
			const auto number = static_cast<T>(std::ldexp((rng() % 2 == 0) ? mantissa : -mantissa, exponent));
			if(number - number == T{0})
				numbers.push_back(number);
		}

		for(const T number : numbers)
		{
			// (Exact arithmetic with 64 bits of precision: not for the numbers that need more)
			if(std::numeric_limits<T>::digits > 64 || std::numeric_limits<long double>::digits < 64)
				continue;
			ASSERT_EQ(expected_raw<P>(static_cast<long double>(number)), P{number}.raw_value()) << std::setprecision(25) << static_cast<long double>(number);
		}
	}

	// Constant expressions, where undefined behavior does not compile
	using fpm::fixed_16_16;
	static_assert(fixed_16_16{65536.0 + 1.5} == fixed_16_16{1.5});
	static_assert(fixed_16_16{32768.0} == fixed_16_16{-32768});
	static_assert(fixed_16_16{-32769.25} == fixed_16_16{32766.75});
	static_assert(fixed_16_16{1e30} == fixed_16_16{0});
	static_assert(fixed_16_16{-1e30f} == fixed_16_16{0});
	static_assert(fixed_16_16{std::numeric_limits<double>::max()} == fixed_16_16{0});
	static_assert(fixed_16_16{32767.99999999} == fixed_16_16{-32768}); // rounds up to the end of the range
	static_assert((fpm::fixed<uint16_t, uint32_t, 8>{-0.5}) == (fpm::fixed<uint16_t, uint32_t, 8>{255.5}));
	static_assert((fpm::fixed<uint16_t, uint32_t, 8>{256.25}) == (fpm::fixed<uint16_t, uint32_t, 8>{0.25}));
	static_assert((fpm::fixed<int32_t, int64_t, 16, false>{-65536.75}) == (fpm::fixed<int32_t, int64_t, 16, false>{-0.75}));
	static_assert(fpm::fixed_4_4{8.5f} == fpm::fixed_4_4{-7.5f});

	// Numbers without bits to spare are not rounded: their sum with a half is
	static_assert(fixed_16_16{130.4461822509765625f}.raw_value() == 8548921);
	static_assert(fpm::fixed_8_24{-0.950916349887847900390625f}.raw_value() == -15953729);
}

// Numbers beyond the range wrap around, like integers and other fixed-point numbers that do not fit
TEST(conversion, float_beyond_range)
{
	const auto with = []<typename P>()
	{
		test_beyond_range<P, float>();
		test_beyond_range<P, double>();
		test_beyond_range<P, long double>();
	};
	with.template operator()<fpm::fixed_4_4>();
	with.template operator()<fpm::fixed_8_8>();
	with.template operator()<fpm::fixed_16_16>();
	with.template operator()<fpm::fixed_8_24>();
	with.template operator()<fpm::fixed<int32_t, int64_t, 30>>();
	with.template operator()<fpm::fixed<int32_t, int64_t, 1>>();
	with.template operator()<fpm::fixed<int32_t, int64_t, 16, false>>();
	with.template operator()<fpm::fixed<uint16_t, uint32_t, 8>>();
	with.template operator()<fpm::fixed<uint32_t, uint64_t, 16>>();
	with.template operator()<fpm::fixed<uint32_t, uint64_t, 31, false>>();
#ifdef FPM_INT128
	with.template operator()<fpm::fixed_32_32>();
	with.template operator()<fpm::fixed_8_56>();
	with.template operator()<fpm::fixed<int64_t, FPM_INT128, 62>>();
	with.template operator()<fpm::fixed<int64_t, FPM_INT128, 1, false>>();
#endif

	using P = fpm::fixed_16_16;
	EXPECT_EQ(P{1.5}, P{65537.5});
	EXPECT_EQ(P{-1.5}, P{-65537.5f});
	EXPECT_EQ(P{0}, P{1e300});

#ifndef NDEBUG
	EXPECT_DEATH_IF_SUPPORTED(auto v = P{std::numeric_limits<double>::infinity()}, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = P{std::numeric_limits<float>::quiet_NaN()}, "");
#endif
}
