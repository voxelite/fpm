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

// The usual arithmetic conversions would convert a negative value to unsigned
TYPED_TEST(arithmethic_int, division_by_unsigned)
{
	using P = TypeParam;

	EXPECT_EQ(P(-3.5) / 7, P(-3.5) / 7u);
	EXPECT_EQ(P(-3.5) / 7, P(-3.5) / std::size_t{7});
	EXPECT_EQ(P(-3.5) / 7, P(-3.5) / uint8_t{7});
	EXPECT_EQ(P(-3.5) / 7, P(-3.5) / uint64_t{7});
	EXPECT_EQ(P(3.5) / 7, P(3.5) / 7u);
	EXPECT_EQ(P(-4) / 3, P(-4) / 3u);

	P x{-3.5};
	x /= 7u;
	EXPECT_EQ(P(-0.5), x);

	EXPECT_EQ(P(-24.5), P(-3.5) * 7u);
	EXPECT_EQ(P(-24.5), std::size_t{7} * P(-3.5));
	EXPECT_EQ(P(3.5), P(-3.5) + 7u);
	EXPECT_EQ(P(-10.5), P(-3.5) - 7u);
}

TEST(arithmethic_int, division_unsigned_type)
{
	using P = fpm::fixed<uint32_t, uint64_t, 16>;

	EXPECT_EQ(P(0.5), P(3.5) / 7);
	EXPECT_EQ(P(0.5), P(3.5) / 7u);
	EXPECT_EQ(P(20000), P(40000) / int8_t{2});

#ifndef FPM_CHECK_OVERFLOW
	// Negative results wrap, like the negation of an unsigned value
	EXPECT_EQ(-P(0.5), P(3.5) / -7);
#endif
}

TYPED_TEST(arithmethic_int, comparison)
{
	using P = TypeParam;

	EXPECT_TRUE(P(3) == 3);
	EXPECT_TRUE(3 == P(3));
	EXPECT_TRUE(P(3) != 4);
	EXPECT_TRUE(P(3.5) != 3);
	EXPECT_TRUE(P(-3.5) != -3);
	EXPECT_TRUE(P(-3.5) != -4);

	EXPECT_TRUE(P(3.5) > 3);
	EXPECT_TRUE(P(3.5) < 4);
	EXPECT_TRUE(P(3) <= 3);
	EXPECT_TRUE(P(3) >= 3);
	EXPECT_TRUE(P(-3.5) < -3);
	EXPECT_TRUE(P(-3.5) > -4);
	EXPECT_TRUE(3 < P(3.5));
	EXPECT_TRUE(4 > P(3.5));
	EXPECT_TRUE(-3 > P(-3.5));

	// Mixed signedness
	EXPECT_TRUE(P(-1) < 1u);
	EXPECT_TRUE(P(-1) != std::numeric_limits<unsigned>::max());
	EXPECT_TRUE(P(-1) < std::size_t{0});
	EXPECT_TRUE(P(1) == uint8_t{1});
	EXPECT_TRUE(uint64_t{2} > P(1.5));
}

// Integers that the type cannot represent do not wrap around
TEST(arithmethic_int, comparison_out_of_range)
{
	using P = fpm::fixed_16_16;

	EXPECT_FALSE(P(1) == 65537);
	EXPECT_TRUE(P(1) < 65537);
	EXPECT_TRUE(P(1) > -65535);
	EXPECT_TRUE(std::numeric_limits<P>::max() < 32768);
	EXPECT_TRUE(std::numeric_limits<P>::lowest() == -32768);
	EXPECT_TRUE(std::numeric_limits<P>::lowest() > std::numeric_limits<int64_t>::lowest());
	EXPECT_TRUE(std::numeric_limits<P>::max() < std::numeric_limits<uint64_t>::max());

	using U = fpm::fixed<uint16_t, uint32_t, 8>;
	EXPECT_TRUE(U(1) > -1);
	EXPECT_TRUE(U(255) > -1);
	EXPECT_FALSE(U(255) == -1);
	EXPECT_TRUE(U(1) < 257);

	static_assert(P(2.5) > 2 && P(2.5) < 3 && P(2) == 2 && 2 == P(2) && 3 > P(2.5));
}
