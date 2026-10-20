// The modules `fpm.fixed` and `fpm.fraction` together: the functions of fractions that take or give fixed-point numbers
#include <gtest/gtest.h>
#include <cstdint>

import fpm.fixed;
import fpm.fraction;

namespace
{
	using P = fpm::fixed_16_16;
	using Q = fpm::fraction<uint16_t>;
}

TEST(module_fixed_fraction, conversions)
{
	EXPECT_EQ(P{0.25}, static_cast<P>(Q{0.25}));
	EXPECT_EQ(Q{0.25}, Q{P{0.25}});
	EXPECT_EQ(P{-0.25}, static_cast<P>(fpm::difference(Q{0.25}, Q{0.5})));
}

TEST(module_fixed_fraction, trigonometry)
{
	EXPECT_EQ(fpm::fixed_8_24{1}, fpm::sin<fpm::fixed_8_24>(Q{0.25}));
	EXPECT_EQ(Q{0.125}, (fpm::atan2<Q>(P{1}, P{1})));
	EXPECT_EQ(Q{0.25}, fpm::asin<Q>(P{1}));
}
