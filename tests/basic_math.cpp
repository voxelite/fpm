#include "common.hpp"
#include <fpm/fixed/math.hpp>

TEST(basic_math, abs)
{
	using P = fpm::fixed_24_8;

	EXPECT_EQ(P(13.125), abs(P(-13.125)));
	EXPECT_EQ(P(13.125), abs(P(13.125)));
	EXPECT_EQ(P(1), abs(P(-1)));
	EXPECT_EQ(P(1), abs(P(1)));
}

TEST(basic_math, fmod)
{
	using P = fpm::fixed_24_8;

	EXPECT_EQ(P( 1.5), fmod(P( 9.5), P( 2)));
	EXPECT_EQ(P(-1.5), fmod(P(-9.5), P( 2)));
	EXPECT_EQ(P( 1.5), fmod(P( 9.5), P(-2)));
	EXPECT_EQ(P(-1.5), fmod(P(-9.5), P(-2)));
}

TEST(basic_math, remainder)
{
	using P = fpm::fixed_24_8;

	EXPECT_EQ(P(-0.5), remainder(P( 9.5), P( 2)));
	EXPECT_EQ(P( 0.5), remainder(P(-9.5), P( 2)));
	EXPECT_EQ(P(-0.5), remainder(P( 9.5), P(-2)));
	EXPECT_EQ(P( 0.5), remainder(P(-9.5), P(-2)));

	EXPECT_EQ(P( 1), remainder(P( 9), P( 2)));
	EXPECT_EQ(P(-1), remainder(P(-9), P( 2)));
	EXPECT_EQ(P( 1), remainder(P( 9), P(-2)));
	EXPECT_EQ(P(-1), remainder(P(-9), P(-2)));

	EXPECT_EQ(P(-1), remainder(P( 11), P( 2)));
	EXPECT_EQ(P( 1), remainder(P(-11), P( 2)));
	EXPECT_EQ(P(-1), remainder(P( 11), P(-2)));
	EXPECT_EQ(P( 1), remainder(P(-11), P(-2)));

	EXPECT_EQ(P(-0.9), remainder(P( 5.1), P( 3)));
	EXPECT_EQ(P( 0.9), remainder(P(-5.1), P( 3)));
	EXPECT_EQ(P(-0.9), remainder(P( 5.1), P(-3)));
	EXPECT_EQ(P( 0.9), remainder(P(-5.1), P(-3)));

	EXPECT_EQ(P(0), remainder(P(0), P(1)));
}

TEST(basic_math, remquo)
{
	// Same remainder as `remainder`, and the quotient rounded to nearest (ties to even) like std::remquo:
	// its sign and at least the 3 lowest bits
	constexpr int QUO_MIN_SIZE = 1 << 3;

	using P = fpm::fixed_16_16;

	const double values[][2] = {
		{9.5, 2}, {-9.5, 2}, {9.5, -2}, {-9.5, -2},
		{9, 2}, {-9, 2}, {9, -2}, {-9, -2},
		{11, 2}, {-11, 2}, {11, -2}, {-11, -2},
		{5.125, 3}, {-5.125, 3}, {5.125, -3}, {-5.125, -3},
		{97.125, 3.75}, {-97.125, 3.75}, {97.125, -3.75}, {-97.125, -3.75},
		{7.5, 1}, {6.5, 1}, {0, 1}, {1, 1000},
	};
	for(const auto& [x, y] : values)
	{
		int quo = 999999;
		int quo_expected = 999999;
		const double expected = std::remquo(x, y, &quo_expected);
		EXPECT_EQ(P(expected), remquo(P(x), P(y), &quo)) << x << " / " << y;
		EXPECT_EQ(quo_expected % QUO_MIN_SIZE, quo % QUO_MIN_SIZE) << x << " / " << y;
		EXPECT_EQ(quo_expected < 0, quo < 0) << x << " / " << y;
		EXPECT_EQ(remainder(P(x), P(y)), remquo(P(x), P(y), &quo));
	}
}
