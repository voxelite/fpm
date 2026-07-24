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
	EXPECT_DEATH(auto v = P(1) / P(0), "");
#endif
}

TEST(arithmethic, division_range)
{
	using P = fpm::fixed<std::int32_t, std::int64_t, 12>;

	// These calculation will overflow and produce
	// wrong results without the intermediate type.
	EXPECT_EQ(P(32), P(256) / P(8));
}

TEST(arithmethic, multiplication_rounding)
{
	// Using 1 bit of fractional precision to test rounding
	using Q_round = fpm::fixed<std::int32_t, std::int64_t, 1, true>;
	using Q = fpm::fixed<std::int32_t, std::int64_t, 1, false>;

	EXPECT_EQ(Q_round(1.0), Q_round(1.5) * Q_round(0.5));
	EXPECT_EQ(Q_round(0.5), Q_round(0.5) * Q_round(0.5));
	EXPECT_EQ(Q(0.5), Q(1.5) * Q(0.5));
	EXPECT_EQ(Q(0.0), Q(0.5) * Q(0.5));
}

TEST(arithmethic, division_rounding)
{
	// Using 1 bit of fractional precision to test rounding
	using Q_round = fpm::fixed<std::int32_t, std::int64_t, 1, true>;
	using Q = fpm::fixed<std::int32_t, std::int64_t, 1, false>;

	EXPECT_EQ(Q_round(2.5), Q_round(3.5) / Q_round(1.5));
	EXPECT_EQ(Q_round(0.5), Q_round(1.0) / Q_round(1.5));
	EXPECT_EQ(Q(2.0), Q(3.5) / Q(1.5));
	EXPECT_EQ(Q(0.5), Q(1.0) / Q(1.5));
}
