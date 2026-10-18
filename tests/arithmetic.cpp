#include "common.hpp"

template<typename T>
class arithmethic : public ::testing::Test
{
};

using FixedTypes = ::testing::Types<
	// 16-bit
	fpm::fixed_8_8,
	// 32-bit
	fpm::fixed_16_16,
	fpm::fixed_24_8,
	fpm::fixed_8_24
#ifdef FPM_INT128
	,
	// 64-bit
	fpm::fixed_32_32,
	fpm::fixed_24_40,
	fpm::fixed_16_48,
	fpm::fixed_40_24,
	fpm::fixed_48_16
#endif
>;

TYPED_TEST_SUITE(arithmethic, FixedTypes);

TYPED_TEST(arithmethic, negation)
{
	using P = TypeParam;

	EXPECT_EQ(P(-13.125), -P( 13.125));
	EXPECT_EQ(P( 13.125), -P(-13.125));
}

TYPED_TEST(arithmethic, addition)
{
	using P = TypeParam;

	EXPECT_EQ(P(10.75), P(3.5) + P(7.25));
}

TYPED_TEST(arithmethic, subtraction)
{
	using P = TypeParam;

	EXPECT_EQ(P(-3.75), P(3.5) - P(7.25));
}

TYPED_TEST(arithmethic, multiplication)
{
	using P = TypeParam;

	EXPECT_EQ(P(-25.375), P(3.5) * P(-7.25));
}

TYPED_TEST(arithmethic, division)
{
	using P = TypeParam;

	EXPECT_EQ(P(3.5 / 7.25), P(3.5) / P(7.25));
	EXPECT_EQ(P(-3.5 / 7.25), P(-3.5) / P(7.25));
	EXPECT_EQ(P(3.5 / -7.25), P(3.5) / P(-7.25));
	EXPECT_EQ(P(-3.5 / -7.25), P(-3.5) / P(-7.25));

#ifndef NDEBUG
	EXPECT_DEATH_IF_SUPPORTED(auto v = P(1) / P(0), "");
#endif
}

TYPED_TEST(arithmethic, division_by_whole)
{
	using P = TypeParam;

	// A whole divisor takes a division in the base type
	EXPECT_EQ(P(3.5 / 2), P(3.5) / P(2));
	EXPECT_EQ(P(7.25 / 3), P(7.25) / P(3));
	EXPECT_EQ(P(-7.25 / 3), P(-7.25) / P(3));
	EXPECT_EQ(P(7.25 / -3), P(7.25) / P(-3));
	EXPECT_EQ(P(-7.25 / -3), P(-7.25) / P(-3));
	EXPECT_EQ(P(7.25), P(7.25) / P(1));
	EXPECT_EQ(P(-7.25), P(7.25) / P(-1));
	EXPECT_EQ(P(0), P(0) / P(5));
}

TEST(arithmethic, division_by_whole_rounding)
{
	// Using 1 bit of fractional precision to test rounding
	using Q_round = fpm::fixed<int32_t, int64_t, 1, true>;
	using Q = fpm::fixed<int32_t, int64_t, 1, false>;

	// 0.75 is a tie: away from zero
	EXPECT_EQ(Q_round(1.0), Q_round(1.5) / Q_round(2));
	EXPECT_EQ(Q_round(-1.0), Q_round(-1.5) / Q_round(2));
	EXPECT_EQ(Q_round(-1.0), Q_round(1.5) / Q_round(-2));
	EXPECT_EQ(Q_round(1.0), Q_round(-1.5) / Q_round(-2));
	EXPECT_EQ(Q_round(1.0), Q_round(2.5) / Q_round(3));
	EXPECT_EQ(Q_round(0.0), Q_round(0.5) / Q_round(3));
	EXPECT_EQ(Q(0.5), Q(1.5) / Q(2));
	EXPECT_EQ(Q(-0.5), Q(-1.5) / Q(2));
	EXPECT_EQ(Q(0.5), Q(2.5) / Q(3));
	EXPECT_EQ(Q(0.0), Q(0.5) / Q(3));
}

TEST(arithmethic, division_by_whole_like_any_divisor)
{
	// Every value by every whole divisor: the same raw result as the division in the intermediate type
	using P = fpm::fixed_8_8;
	for(int32_t x = std::numeric_limits<int16_t>::lowest(); x <= std::numeric_limits<int16_t>::max(); x++)
	{
		for(int32_t n = -128; n < 128; n++)
		{
			// (The lowest value by -1 does not fit, which only defined overflow allows)
			if(n == 0 || (n == -1 && x == std::numeric_limits<int16_t>::lowest() && !fpm::detail::defined_overflow))
				continue;
			const int32_t y = n * 256;
			const int32_t quotient = (x * 512) / y;
			const auto expected = static_cast<int16_t>(quotient >= 0 ? (quotient + 1) >> 1 : quotient >> 1);
			ASSERT_EQ(expected, (P::from_raw_value(static_cast<int16_t>(x)) / P::from_raw_value(static_cast<int16_t>(y))).raw_value()) << x << " / " << n;
		}
	}
}

TEST(arithmethic, division_range)
{
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// These calculation will overflow and produce
	// wrong results without the intermediate type.
	EXPECT_EQ(P(32), P(256) / P(8));
}

TEST(arithmethic, multiplication_rounding)
{
	// Using 1 bit of fractional precision to test rounding
	using Q_round = fpm::fixed<int32_t, int64_t, 1, true>;
	using Q = fpm::fixed<int32_t, int64_t, 1, false>;

	EXPECT_EQ(Q_round(1.0), Q_round(1.5) * Q_round(0.5));
	EXPECT_EQ(Q_round(0.5), Q_round(0.5) * Q_round(0.5));
	EXPECT_EQ(Q(0.5), Q(1.5) * Q(0.5));
	EXPECT_EQ(Q(0.0), Q(0.5) * Q(0.5));
}

TEST(arithmethic, division_rounding)
{
	// Using 1 bit of fractional precision to test rounding
	using Q_round = fpm::fixed<int32_t, int64_t, 1, true>;
	using Q = fpm::fixed<int32_t, int64_t, 1, false>;

	EXPECT_EQ(Q_round(2.5), Q_round(3.5) / Q_round(1.5));
	EXPECT_EQ(Q_round(0.5), Q_round(1.0) / Q_round(1.5));
	EXPECT_EQ(Q(2.0), Q(3.5) / Q(1.5));
	EXPECT_EQ(Q(0.5), Q(1.0) / Q(1.5));
}
