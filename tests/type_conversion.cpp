#include "common.hpp"

TEST(type_conversion, same_base)
{
	EXPECT_EQ(fpm::fixed_16_16{0}, static_cast<fpm::fixed_16_16>(fpm::fixed_24_8{0}));
	EXPECT_EQ(fpm::fixed_16_16{1}, static_cast<fpm::fixed_16_16>(fpm::fixed_24_8{1}));
	EXPECT_EQ(fpm::fixed_16_16{2}, static_cast<fpm::fixed_16_16>(fpm::fixed_24_8{2}));
	EXPECT_EQ(fpm::fixed_16_16{3}, static_cast<fpm::fixed_16_16>(fpm::fixed_24_8{3}));

	EXPECT_EQ(fpm::fixed_16_16{0.5  }, static_cast<fpm::fixed_16_16>(fpm::fixed_24_8{0.5  }));
	EXPECT_EQ(fpm::fixed_16_16{0.25 }, static_cast<fpm::fixed_16_16>(fpm::fixed_24_8{0.25 }));
	EXPECT_EQ(fpm::fixed_16_16{0.125}, static_cast<fpm::fixed_16_16>(fpm::fixed_24_8{0.125}));
}

TEST(type_conversion, same_fraction)
{
	EXPECT_EQ(fpm::fixed_24_8{0}, static_cast<fpm::fixed_24_8>(fpm::fixed_8_8{0}));
	EXPECT_EQ(fpm::fixed_24_8{1}, static_cast<fpm::fixed_24_8>(fpm::fixed_8_8{1}));
	EXPECT_EQ(fpm::fixed_24_8{2}, static_cast<fpm::fixed_24_8>(fpm::fixed_8_8{2}));
	EXPECT_EQ(fpm::fixed_24_8{3}, static_cast<fpm::fixed_24_8>(fpm::fixed_8_8{3}));

	EXPECT_EQ(fpm::fixed_24_8{0.5  }, static_cast<fpm::fixed_24_8>(fpm::fixed_8_8{0.5  }));
	EXPECT_EQ(fpm::fixed_24_8{0.25 }, static_cast<fpm::fixed_24_8>(fpm::fixed_8_8{0.25 }));
	EXPECT_EQ(fpm::fixed_24_8{0.125}, static_cast<fpm::fixed_24_8>(fpm::fixed_8_8{0.125}));
}

TEST(type_conversion, to_bigger)
{
	EXPECT_EQ(fpm::fixed_16_16{0}, static_cast<fpm::fixed_16_16>(fpm::fixed_8_8{0}));
	EXPECT_EQ(fpm::fixed_16_16{1}, static_cast<fpm::fixed_16_16>(fpm::fixed_8_8{1}));
	EXPECT_EQ(fpm::fixed_16_16{2}, static_cast<fpm::fixed_16_16>(fpm::fixed_8_8{2}));
	EXPECT_EQ(fpm::fixed_16_16{3}, static_cast<fpm::fixed_16_16>(fpm::fixed_8_8{3}));

	EXPECT_EQ(fpm::fixed_16_16{0.5  }, static_cast<fpm::fixed_16_16>(fpm::fixed_8_8{0.5  }));
	EXPECT_EQ(fpm::fixed_16_16{0.25 }, static_cast<fpm::fixed_16_16>(fpm::fixed_8_8{0.25 }));
	EXPECT_EQ(fpm::fixed_16_16{0.125}, static_cast<fpm::fixed_16_16>(fpm::fixed_8_8{0.125}));
}

TEST(type_conversion, to_smaller)
{
	EXPECT_EQ(fpm::fixed_8_8{0}, static_cast<fpm::fixed_8_8>(fpm::fixed_16_16{0}));
	EXPECT_EQ(fpm::fixed_8_8{1}, static_cast<fpm::fixed_8_8>(fpm::fixed_16_16{1}));
	EXPECT_EQ(fpm::fixed_8_8{2}, static_cast<fpm::fixed_8_8>(fpm::fixed_16_16{2}));
	EXPECT_EQ(fpm::fixed_8_8{3}, static_cast<fpm::fixed_8_8>(fpm::fixed_16_16{3}));

	EXPECT_EQ(fpm::fixed_8_8{0.5  }, static_cast<fpm::fixed_8_8>(fpm::fixed_16_16{0.5}));
	EXPECT_EQ(fpm::fixed_8_8{0.25 }, static_cast<fpm::fixed_8_8>(fpm::fixed_16_16{0.25}));
	EXPECT_EQ(fpm::fixed_8_8{0.125}, static_cast<fpm::fixed_8_8>(fpm::fixed_16_16{0.125}));
}
