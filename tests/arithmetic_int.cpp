#include "common.hpp"

template<typename T>
class arithmethic_int : public ::testing::Test
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

TYPED_TEST_SUITE(arithmethic_int, FixedTypes);

TYPED_TEST(arithmethic_int, addition)
{
	using P = TypeParam;

	EXPECT_EQ(P(10.5), P(3.5) + 7);
}

TYPED_TEST(arithmethic_int, subtraction)
{
	using P = TypeParam;

	EXPECT_EQ(P(-3.5), P(3.5) - 7);
}

TYPED_TEST(arithmethic_int, multiplication)
{
	using P = TypeParam;

	EXPECT_EQ(P(-24.5), P(3.5) * -7);
}

TYPED_TEST(arithmethic_int, division)
{
	using P = TypeParam;

	EXPECT_EQ(P(3.5 / 7), P(3.5) / 7);
	EXPECT_EQ(P(-3.5 / 7), P(-3.5) / 7);
	EXPECT_EQ(P(3.5 / -7), P(3.5) / -7);
	EXPECT_EQ(P(-3.5 / -7), P(-3.5) / -7);

#ifndef NDEBUG
	EXPECT_DEATH(auto v = P(1) / 0, "");
#endif
}

TEST(arithmethic_int, division_range)
{
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// These calculation will overflow and produce
	// wrong results without the intermediate type.
	EXPECT_EQ(P(32), P(256) / 8);
}
