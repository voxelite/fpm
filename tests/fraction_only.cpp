#include "common.hpp"

TEST(fraction_only, signed)
{
	using F = fpm::fixed<int8_t, int16_t, 7>;
	EXPECT_EQ(std::numeric_limits<F>::min(), F{-1});
	EXPECT_EQ(std::numeric_limits<F>::max(), F{0.992188});

	EXPECT_EQ(F{-1}, F{1}); // Overflow
	EXPECT_EQ(F{-1} + F{1}, F{0});
}

TEST(fraction_only, unsigned)
{
	using F = fpm::fixed<uint8_t, uint16_t, 8>;
	EXPECT_EQ(std::numeric_limits<F>::min(), F{0});
	EXPECT_EQ(std::numeric_limits<F>::max(), F{0.996094});

	EXPECT_EQ(F{0}, F{1}); // Overflow
}
