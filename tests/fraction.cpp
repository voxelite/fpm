#include "common.hpp"
#include <fpm/fraction.hpp>

#include <cmath>
#include <random>

template<typename T>
class fraction : public ::testing::Test
{
};

using FractionTypes = ::testing::Types<
	fpm::fraction<uint8_t>,
	fpm::fraction<uint16_t>,
	fpm::fraction<uint32_t>,
	fpm::fraction<uint64_t>
>;

TYPED_TEST_SUITE(fraction, FractionTypes);

namespace
{
	/// Raw values over the whole range: the edges, and random values of every magnitude
	template<typename P>
	std::vector<P> values()
	{
		using B = typename P::base_type;
		constexpr B max = std::numeric_limits<B>::max();
		std::vector<P> result;
		for(const B raw : {B{0}, B{1}, B{2}, static_cast<B>(max / 2), static_cast<B>(max / 2 + 1), static_cast<B>(max - 1), max})
			result.push_back(P::from_raw_value(raw));
		std::mt19937_64 rng(P::fraction_bits);
		for(int i = 0; i < 2000; ++i)
		{
			const int bits = 1 + static_cast<int>(rng() % P::fraction_bits);
			result.push_back(P::from_raw_value(static_cast<B>(rng() >> (64 - bits))));
		}
		return result;
	}

	/// The exact value, which needs up to 64 bits of precision
	template<typename P>
	long double exact(const P x)
	{
		return std::ldexp(static_cast<long double>(x.raw_value()), -static_cast<int>(P::fraction_bits));
	}

	/// All operations in constant expressions, where undefined behavior does not compile
	template<typename B>
	constexpr bool constant_evaluation()
	{
		using P = fpm::fraction<B>;
		constexpr B max = std::numeric_limits<B>::max();
		constexpr auto top = static_cast<B>(B{1} << (P::fraction_bits - 1));
		constexpr P zero{};
		constexpr P half = P::from_raw_value(top);
		constexpr P quarter = P::from_raw_value(static_cast<B>(top / 2));
		constexpr P last = P::from_raw_value(max);
		constexpr P first = P::from_raw_value(1);

		return P::fraction_bits == sizeof(B) * 8
			&& half + half == zero
			&& last + first == zero
			&& last + last == P::from_raw_value(static_cast<B>(max - 1))
			&& zero - first == last
			&& quarter - half == half + quarter
			&& -quarter == half + quarter
			&& -zero == zero
			&& -last == first
			&& quarter * 2 == half
			&& quarter * 5 == quarter
			&& 3 * quarter == half + quarter
			&& quarter * -1 == half + quarter
			&& last * max == first
			&& last * int64_t{-1} == first
			&& last * std::numeric_limits<uint64_t>::max() == first
			&& half / 2 == quarter
			&& half / 2u == quarter
			&& last / max == first
			&& half / -2 == half + quarter
			&& first / std::numeric_limits<int64_t>::lowest() == zero
			&& last / std::numeric_limits<uint64_t>::max() == (P::fraction_bits == 64 ? first : zero)
			&& quarter < half
			&& last > half
			&& half != quarter
			&& P{0.5} == half
			&& P{0.25f} == quarter
			&& P{0.0} == zero
			&& static_cast<double>(half) == 0.5
			&& static_cast<float>(quarter) == 0.25f
			&& static_cast<long double>(zero) == 0.0L
			&& P{fpm::fixed_16_16{2.5}} == half
			&& P{fpm::fixed_16_16{-0.25}} == half + quarter
			&& P{fpm::fixed_16_16{-3}} == zero
			&& P{fpm::fixed_8_8{0.25}} == quarter
			&& P{(fpm::fixed<uint16_t, uint32_t, 8>{255.5})} == half
			&& fpm::fixed_16_16(half) == fpm::fixed_16_16{0.5}
			&& fpm::fixed_8_8(quarter) == fpm::fixed_8_8{0.25}
			&& fpm::fixed_4_4(half) == fpm::fixed_4_4{0.5}
			&& (fpm::fixed<uint16_t, uint32_t, 8>(half)) == (fpm::fixed<uint16_t, uint32_t, 8>{0.5})
			&& (fpm::fixed<int32_t, int64_t, 30>(quarter)) == (fpm::fixed<int32_t, int64_t, 30>{0.25})
			&& (fpm::fixed<int32_t, int64_t, 1>(last)) == (fpm::fixed<int32_t, int64_t, 1>{1})
			&& (fpm::fixed<int32_t, int64_t, 1, false>(last)) == (fpm::fixed<int32_t, int64_t, 1, false>{0.5})
#ifdef FPM_INT128
			&& P{fpm::fixed_32_32{-7.75}} == quarter
			&& P{fpm::fixed_8_56{0.5}} == half
			&& fpm::fixed_32_32(half) == fpm::fixed_32_32{0.5}
			&& fpm::fixed_8_56(quarter) == fpm::fixed_8_56{0.25}
			&& (fpm::fixed<int64_t, FPM_INT128, 62>(last)) <= (fpm::fixed<int64_t, FPM_INT128, 62>{1})
			&& (fpm::fixed<int64_t, FPM_INT128, 62, false>(last)) < (fpm::fixed<int64_t, FPM_INT128, 62, false>{1})
#endif
			;
	}

	static_assert(constant_evaluation<uint8_t>());
	static_assert(constant_evaluation<uint16_t>());
	static_assert(constant_evaluation<uint32_t>());
	static_assert(constant_evaluation<uint64_t>());

	static_assert(sizeof(fpm::fraction<uint16_t>) == sizeof(uint16_t));
	static_assert(std::is_trivially_copyable_v<fpm::fraction<uint32_t>>);
}

TYPED_TEST(fraction, wraps_around)
{
	using P = TypeParam;
	using B = typename P::base_type;
	const auto all = values<P>();

	for(const P x : all)
	{
		EXPECT_EQ(P{}, x - x);
		EXPECT_EQ(P{}, x + -x);
		EXPECT_EQ(x, -(-x));
		EXPECT_EQ(x, x * 1);
		EXPECT_EQ(P{}, x * 0);
		EXPECT_EQ(-x, x * -1);
		EXPECT_EQ(x + x + x, x * 3);
		EXPECT_EQ(x + x + x, x * uint8_t{3});
		EXPECT_EQ(x + x + x, int64_t{3} * x);
		EXPECT_EQ(x, x / 1);

		for(const P y : {all[3], all[5], all[6], all[100]})
		{
			EXPECT_EQ(static_cast<B>(uint64_t{x.raw_value()} + uint64_t{y.raw_value()}), (x + y).raw_value());
			EXPECT_EQ(static_cast<B>(uint64_t{x.raw_value()} - uint64_t{y.raw_value()}), (x - y).raw_value());

			// Modulo 1, against exact arithmetic (where the floating-point type is precise enough for it)
			if(P::fraction_bits < static_cast<uint32_t>(std::numeric_limits<long double>::digits))
			{
				const long double sum = exact(x) + exact(y);
				EXPECT_EQ(sum >= 1 ? sum - 1 : sum, exact(x + y));
				const long double difference = exact(x) - exact(y);
				EXPECT_EQ(difference < 0 ? difference + 1 : difference, exact(x - y));
			}

			EXPECT_EQ(x, (x + y) - y);
			EXPECT_EQ(x, (x - y) + y);
			EXPECT_EQ(x + y, y + x);
			EXPECT_EQ(x.raw_value() < y.raw_value(), x < y);
			EXPECT_EQ(x.raw_value() == y.raw_value(), x == y);
			EXPECT_EQ(x.raw_value() >= y.raw_value(), x >= y);
		}

		// Compound assignments
		P z = x;
		z += all[5];
		z -= all[5];
		z *= 1;
		z /= 1;
		EXPECT_EQ(x, z);
	}

	EXPECT_EQ(P::from_raw_value(static_cast<B>(std::numeric_limits<B>::max() / 2)), P::from_raw_value(std::numeric_limits<B>::max()) / 2);
}

TYPED_TEST(fraction, division)
{
	using P = TypeParam;
	using B = typename P::base_type;

	for(const P x : values<P>())
	{
		for(const int y : {1, 2, 3, 7, 100, 255})
		{
			const auto expected = P::from_raw_value(static_cast<B>(x.raw_value() / static_cast<B>(y)));
			EXPECT_EQ(expected, x / y);
			EXPECT_EQ(expected, x / static_cast<unsigned>(y));
			EXPECT_EQ(expected, x / static_cast<uint8_t>(y));
			EXPECT_EQ(expected, x / static_cast<int64_t>(y));
			EXPECT_EQ(expected, x / static_cast<uint64_t>(y));
			// The negative quotient wraps around
			EXPECT_EQ(-expected, x / -y);
			EXPECT_EQ(-expected, x / static_cast<int64_t>(-y));
		}
	}

#ifndef NDEBUG
	EXPECT_DEATH(auto v = P{0.5} / 0, "");
#endif
}

TYPED_TEST(fraction, floating_point)
{
	using P = TypeParam;

	for(const P x : values<P>())
	{
		// long double has at least as many bits as any fraction where it has 64 bits of precision;
		// otherwise the fractions with fewer bits than the floating-point type are exact
		if(P::fraction_bits <= static_cast<uint32_t>(std::numeric_limits<long double>::digits))
		{
			EXPECT_EQ(exact(x), static_cast<long double>(x));
			EXPECT_EQ(x, P{static_cast<long double>(x)});
		}
		if(P::fraction_bits <= static_cast<uint32_t>(std::numeric_limits<double>::digits))
		{
			EXPECT_EQ(exact(x), static_cast<long double>(static_cast<double>(x)));
			EXPECT_EQ(x, P{static_cast<double>(x)});
		}
		if(P::fraction_bits <= static_cast<uint32_t>(std::numeric_limits<float>::digits))
		{
			EXPECT_EQ(exact(x), static_cast<long double>(static_cast<float>(x)));
			EXPECT_EQ(x, P{static_cast<float>(x)});
		}

		// Never beyond the range, also where the floating-point type is less precise
		EXPECT_GE(static_cast<float>(x), 0.0f);
		EXPECT_LE(static_cast<float>(x), 1.0f);
		EXPECT_GE(static_cast<double>(x), 0.0);
		EXPECT_LE(static_cast<double>(x), 1.0);
	}

	// Rounded to nearest: within half a unit of the exact value, or a unit for the largest fraction
	std::mt19937_64 rng(7);
	std::uniform_real_distribution<double> distribution(0.0, 1.0);
	for(int i = 0; i < 10000; ++i)
	{
		// (Also close to 1)
		const double value = std::min(i % 4 == 0 ? 1.0 - distribution(rng) / 300 : distribution(rng), std::nextafter(1.0, 0.0));
		const P x{value};
		const long double error = std::abs(exact(x) - static_cast<long double>(value));
		const bool is_largest = x.raw_value() == std::numeric_limits<typename P::base_type>::max();
		EXPECT_LE(error, std::ldexp(is_largest ? 1.0L : 0.5L, -static_cast<int>(P::fraction_bits))) << value;
	}

	// The largest values below 1 give the largest fraction (not 1): unless the fraction can represent them
	const auto largest = P::from_raw_value(std::numeric_limits<typename P::base_type>::max());
	const double last_double = std::nextafter(1.0, 0.0);
	const float last_float = std::nextafter(1.0f, 0.0f);
	if(P::fraction_bits < static_cast<uint32_t>(std::numeric_limits<double>::digits))
		EXPECT_EQ(largest, P{last_double});
	else
		EXPECT_EQ(static_cast<long double>(last_double), exact(P{last_double}));
	if(P::fraction_bits < static_cast<uint32_t>(std::numeric_limits<float>::digits))
		EXPECT_EQ(largest, P{last_float});
	else
		EXPECT_EQ(static_cast<long double>(last_float), exact(P{last_float}));

	// Every number in [0, 1) gives the nearest fraction, in order
	if(P::fraction_bits < static_cast<uint32_t>(std::numeric_limits<double>::digits))
	{
		const double unit = std::ldexp(1.0, -static_cast<int>(P::fraction_bits));
		EXPECT_EQ(largest, P{1.0 - unit});
		EXPECT_EQ(largest, P{1.0 - unit / 2});
		EXPECT_EQ(largest, P{1.0 - unit / 4});
		EXPECT_EQ(largest, P{1.0 - unit * 1.25});
		EXPECT_EQ(P::from_raw_value(static_cast<typename P::base_type>(largest.raw_value() - 1)), P{1.0 - unit * 1.75});
	}

#ifndef NDEBUG
	EXPECT_DEATH(auto v = P{1.0}, "");
	EXPECT_DEATH(auto v = P{-0.25}, "");
#endif
}

namespace
{
	/// Conversions to a fixed-point type and back, against exact arithmetic
	template<typename P, typename Q>
	void test_fixed()
	{
		using QB = typename Q::base_type;
		const int fraction_bits = static_cast<int>(P::fraction_bits);
		const int fixed_bits = static_cast<int>(Q::fraction_bits);
		const long double unit = std::ldexp(1.0L, -fixed_bits);

		for(const P x : values<P>())
		{
			const Q q(x);
			const auto value = std::ldexp(static_cast<long double>(q.raw_value()), -fixed_bits);
			if(fixed_bits >= fraction_bits)
			{
				// Exact, both ways
				ASSERT_EQ(exact(x), value);
				ASSERT_EQ(x, P{q});
			}
			else if(Q::enable_rounding)
			{
				// Rounded to nearest, ties upwards: up to 1
				const long double expected = std::floor(exact(x) / unit + 0.5L) * unit;
				ASSERT_EQ(expected, value) << exact(x);
				ASSERT_LE(value, 1.0L);
			}
			else
			{
				ASSERT_EQ(std::floor(exact(x) / unit) * unit, value) << exact(x);
			}
		}

		// The fraction of any fixed-point number, also of negative ones
		std::mt19937_64 rng(static_cast<unsigned>(fraction_bits * 100 + fixed_bits));
		std::vector<QB> raws{0, 1, std::numeric_limits<QB>::max(), std::numeric_limits<QB>::lowest(), static_cast<QB>(QB{1} << fixed_bits)};
		if(std::is_signed_v<QB>)
			raws.push_back(static_cast<QB>(QB{0} - QB{1}));
		for(int i = 0; i < 2000; ++i)
		{
			const auto shift = rng() % 64;
			raws.push_back(static_cast<QB>(rng() >> shift));
		}
		for(const QB raw : raws)
		{
			const long double value = std::ldexp(static_cast<long double>(raw), -fixed_bits);
			const long double expected_fraction = value - std::floor(value);
			long double expected = expected_fraction;
			if(fixed_bits > fraction_bits)
			{
				const long double step = std::ldexp(1.0L, -fraction_bits);
				expected = Q::enable_rounding
					? std::floor(expected_fraction / step + 0.5L) * step
					: std::floor(expected_fraction / step) * step;
				if(expected >= 1)
					expected = 1 - step; // the largest fraction
			}
			ASSERT_EQ(expected, exact(P{Q::from_raw_value(raw)})) << "raw " << static_cast<long double>(raw);
		}
	}
}

TYPED_TEST(fraction, fixed_point)
{
	using P = TypeParam;

	// `long double` must represent the raw values exactly
	if(std::numeric_limits<long double>::digits < 64)
		GTEST_SKIP() << "long double has less than 64 bits of precision";

	test_fixed<P, fpm::fixed_4_4>();
	test_fixed<P, fpm::fixed_8_8>();
	test_fixed<P, fpm::fixed_16_16>();
	test_fixed<P, fpm::fixed_24_8>();
	test_fixed<P, fpm::fixed_8_24>();
	test_fixed<P, fpm::fixed<int32_t, int64_t, 16, false>>();
	test_fixed<P, fpm::fixed<int32_t, int64_t, 1>>();
	test_fixed<P, fpm::fixed<int32_t, int64_t, 30>>();
	test_fixed<P, fpm::fixed<int8_t, int16_t, 6>>();
	test_fixed<P, fpm::fixed<uint8_t, uint16_t, 7>>();
	test_fixed<P, fpm::fixed<uint16_t, uint32_t, 8>>();
	test_fixed<P, fpm::fixed<uint32_t, uint64_t, 31, false>>();
#ifdef FPM_INT128
	test_fixed<P, fpm::fixed_32_32>();
	test_fixed<P, fpm::fixed_48_16>();
	test_fixed<P, fpm::fixed_8_56>();
	test_fixed<P, fpm::fixed<int64_t, FPM_INT128, 62>>();
	test_fixed<P, fpm::fixed<int64_t, FPM_INT128, 1, false>>();
#endif
}

// Every value of the small types
TEST(fraction, exhaustive)
{
	for(uint32_t raw = 0; raw <= 0xFFFF; ++raw)
	{
		const auto x = fpm::fraction<uint16_t>::from_raw_value(static_cast<uint16_t>(raw));
		ASSERT_EQ(x, fpm::fraction<uint16_t>{fpm::fixed_16_16(x)});
		ASSERT_EQ(x, fpm::fraction<uint16_t>{static_cast<float>(x)});
		ASSERT_EQ(static_cast<int32_t>(raw), fpm::fixed_16_16(x).raw_value());
		ASSERT_EQ(static_cast<uint16_t>(raw * raw), (x * raw).raw_value());
		ASSERT_EQ(static_cast<uint16_t>(0u - raw), (-x).raw_value());

		if(raw <= 0xFF)
		{
			const auto y = fpm::fraction<uint8_t>::from_raw_value(static_cast<uint8_t>(raw));
			for(uint32_t other = 0; other <= 0xFF; ++other)
			{
				const auto z = fpm::fraction<uint8_t>::from_raw_value(static_cast<uint8_t>(other));
				ASSERT_EQ(static_cast<uint8_t>(raw + other), (y + z).raw_value());
				ASSERT_EQ(static_cast<uint8_t>(raw - other), (y - z).raw_value());
				ASSERT_EQ(static_cast<uint8_t>(raw * other), (y * other).raw_value());
				if(other != 0)
					ASSERT_EQ(static_cast<uint8_t>(raw / other), (y / other).raw_value());
			}
		}
	}
}

// An angle as a part of a turn
TEST(fraction, angle)
{
	using Angle = fpm::fraction<uint16_t>;

	Angle angle{0.75};
	angle += Angle{0.5}; // a turn and a quarter
	EXPECT_EQ(Angle{0.25}, angle);

	const auto radians = fpm::fixed_16_16(angle) * fpm::fixed_16_16::two_pi();
	EXPECT_EQ(fpm::fixed_16_16::half_pi(), radians);
}
