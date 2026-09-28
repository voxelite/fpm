#include "common.hpp"

template<typename T>
class customizations : public ::testing::Test
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

TYPED_TEST_SUITE(customizations, FixedTypes);

TYPED_TEST(customizations, swap)
{
	using std::swap;

	using P = TypeParam;
	P x{1}, y{2};
	swap(x, y);
	EXPECT_EQ(P{2}, x);
	EXPECT_EQ(P{1}, y);
}

TYPED_TEST(customizations, hash)
{
	using P = TypeParam;

	const std::hash<P> hash{};
	for(int y = -50; y < 50; ++y)
	{
		EXPECT_EQ(hash(P{y}/10), hash(P{y}/10));
		for(int x = -50; x < 50; ++x)
		{
			if(x != y)
			{
				EXPECT_NE(hash(P{x}/10), hash(P{y}/10));
			}
		}
	}
}

template<typename T>
struct Limits {};

template<>
struct Limits<fpm::fixed_8_8>
{
	static constexpr bool is_signed() noexcept { return true; }
	static constexpr int digits() noexcept { return 15; }
	static constexpr int max_digits10() noexcept { return 3+3; }
	static constexpr int min_exponent() noexcept { return -7; }
	static constexpr int max_exponent() noexcept { return  7; }
	static constexpr int min_exponent10() noexcept { return -2; }
	static constexpr int max_exponent10() noexcept { return  2; }
	static constexpr fpm::fixed_8_8 lowest() noexcept { return fpm::fixed_8_8::from_raw_value(-32767 - 1); }
	static constexpr fpm::fixed_8_8 max() noexcept { return fpm::fixed_8_8::from_raw_value( 32767); }
};

template<>
struct Limits<fpm::fixed_16_16>
{
	static constexpr bool is_signed() noexcept { return true; }
	static constexpr int digits() noexcept { return 31; }
	static constexpr int max_digits10() noexcept { return 5+5; }
	static constexpr int min_exponent() noexcept { return -15; }
	static constexpr int max_exponent() noexcept { return  15; }
	static constexpr int min_exponent10() noexcept { return -4; }
	static constexpr int max_exponent10() noexcept { return 4; }
	static constexpr fpm::fixed_16_16 lowest() noexcept { return fpm::fixed_16_16::from_raw_value(-2147483647 - 1); }
	static constexpr fpm::fixed_16_16 max() noexcept { return fpm::fixed_16_16::from_raw_value( 2147483647); }
};

template<>
struct Limits<fpm::fixed_24_8>
{
	static constexpr bool is_signed() noexcept { return true; }
	static constexpr int digits() noexcept { return 31; }
	static constexpr int max_digits10() noexcept { return 7+3; }
	static constexpr int min_exponent() noexcept { return -7; }
	static constexpr int max_exponent() noexcept { return 23; }
	static constexpr int min_exponent10() noexcept { return -2; }
	static constexpr int max_exponent10() noexcept { return 6; }
	static constexpr fpm::fixed_24_8 lowest() noexcept { return fpm::fixed_24_8::from_raw_value(-2147483647 - 1); }
	static constexpr fpm::fixed_24_8 max() noexcept { return fpm::fixed_24_8::from_raw_value( 2147483647); }
};

template<>
struct Limits<fpm::fixed_8_24>
{
	static constexpr bool is_signed() noexcept { return true; }
	static constexpr int digits() noexcept { return 31; }
	static constexpr int max_digits10() noexcept { return 3+8; }
	static constexpr int min_exponent() noexcept { return -23; }
	static constexpr int max_exponent() noexcept { return  7; }
	static constexpr int min_exponent10() noexcept { return -7; }
	static constexpr int max_exponent10() noexcept { return  2; }
	static constexpr fpm::fixed_8_24 lowest() noexcept { return fpm::fixed_8_24::from_raw_value(-2147483647 - 1); }
	static constexpr fpm::fixed_8_24 max() noexcept { return fpm::fixed_8_24::from_raw_value( 2147483647); }
};

#ifdef FPM_INT128
template<>
struct Limits<fpm::fixed_32_32>
{
	static constexpr bool is_signed() noexcept { return true; }
	static constexpr int digits() noexcept { return 63; }
	static constexpr int max_digits10() noexcept { return 10+10; }
	static constexpr int min_exponent() noexcept { return -31; }
	static constexpr int max_exponent() noexcept { return  31; }
	static constexpr int min_exponent10() noexcept { return -9; }
	static constexpr int max_exponent10() noexcept { return  9; }
	static constexpr fpm::fixed_32_32 lowest() noexcept { return fpm::fixed_32_32::from_raw_value(-9223372036854775807LL - 1); }
	static constexpr fpm::fixed_32_32 max() noexcept { return fpm::fixed_32_32::from_raw_value( 9223372036854775807LL); }
};

template<>
struct Limits<fpm::fixed_24_40>
{
	static constexpr bool is_signed() noexcept { return true; }
	static constexpr int digits() noexcept { return 63; }
	static constexpr int max_digits10() noexcept { return 7+13; }
	static constexpr int min_exponent() noexcept { return -39; }
	static constexpr int max_exponent() noexcept { return  23; }
	static constexpr int min_exponent10() noexcept { return -12; }
	static constexpr int max_exponent10() noexcept { return  6; }
	static constexpr fpm::fixed_24_40 lowest() noexcept { return fpm::fixed_24_40::from_raw_value(-9223372036854775807LL - 1); }
	static constexpr fpm::fixed_24_40 max() noexcept { return fpm::fixed_24_40::from_raw_value( 9223372036854775807LL); }
};

template<>
struct Limits<fpm::fixed_16_48>
{
	static constexpr bool is_signed() noexcept { return true; }
	static constexpr int digits() noexcept { return 63; }
	static constexpr int max_digits10() noexcept { return 5+15; }
	static constexpr int min_exponent() noexcept { return -47; }
	static constexpr int max_exponent() noexcept { return  15; }
	static constexpr int min_exponent10() noexcept { return -14; }
	static constexpr int max_exponent10() noexcept { return  4; }
	static constexpr fpm::fixed_16_48 lowest() noexcept { return fpm::fixed_16_48::from_raw_value(-9223372036854775807LL - 1); }
	static constexpr fpm::fixed_16_48 max() noexcept { return fpm::fixed_16_48::from_raw_value( 9223372036854775807LL); }
};

template<>
struct Limits<fpm::fixed_40_24>
{
	static constexpr bool is_signed() noexcept { return true; }
	static constexpr int digits() noexcept { return 63; }
	static constexpr int max_digits10() noexcept { return 12+8; }
	static constexpr int min_exponent() noexcept { return -23; }
	static constexpr int max_exponent() noexcept { return  39; }
	static constexpr int min_exponent10() noexcept { return -7; }
	static constexpr int max_exponent10() noexcept { return  11; }
	static constexpr fpm::fixed_40_24 lowest() noexcept { return fpm::fixed_40_24::from_raw_value(-9223372036854775807LL - 1); }
	static constexpr fpm::fixed_40_24 max() noexcept { return fpm::fixed_40_24::from_raw_value( 9223372036854775807LL); }
};

template<>
struct Limits<fpm::fixed_48_16>
{
	static constexpr bool is_signed() noexcept { return true; }
	static constexpr int digits() noexcept { return 63; }
	static constexpr int max_digits10() noexcept { return 15+5; }
	static constexpr int min_exponent() noexcept { return -15; }
	static constexpr int max_exponent() noexcept { return  47; }
	static constexpr int min_exponent10() noexcept { return -4; }
	static constexpr int max_exponent10() noexcept { return  14; }
	static constexpr fpm::fixed_48_16 lowest() noexcept { return fpm::fixed_48_16::from_raw_value(-9223372036854775807LL - 1); }
	static constexpr fpm::fixed_48_16 max() noexcept { return fpm::fixed_48_16::from_raw_value( 9223372036854775807LL); }
};
#endif

TYPED_TEST(customizations, numeric_limits)
{
	using L = std::numeric_limits<TypeParam>;
	using TL = Limits<TypeParam>;

	EXPECT_EQ(L::is_specialized, true);
	EXPECT_EQ(L::is_signed, TL::is_signed());
	EXPECT_EQ(L::is_integer, false);
	EXPECT_EQ(L::is_exact, true);
	EXPECT_EQ(L::has_infinity, false);
	EXPECT_EQ(L::has_quiet_NaN, false);
	EXPECT_EQ(L::has_signaling_NaN, false);
	EXPECT_EQ(L::has_denorm, std::denorm_absent);
	EXPECT_EQ(L::has_denorm_loss, false);
	EXPECT_EQ(L::round_style, std::round_to_nearest);
	EXPECT_EQ(L::is_iec559, false);
	EXPECT_EQ(L::is_bounded, true);
	EXPECT_EQ(L::is_modulo, false);
	EXPECT_EQ(L::digits, TL::digits());
	EXPECT_EQ(L::digits10, 0);
	EXPECT_EQ(L::max_digits10, TL::max_digits10());
	EXPECT_EQ(L::radix, 2);
	EXPECT_EQ(L::min_exponent, TL::min_exponent());
	EXPECT_EQ(L::min_exponent10, TL::min_exponent10());
	EXPECT_EQ(L::max_exponent, TL::max_exponent());
	EXPECT_EQ(L::max_exponent10, TL::max_exponent10());
	EXPECT_EQ(L::traps, true);
	EXPECT_EQ(L::tinyness_before, false);

	EXPECT_EQ(L::min(), TypeParam::from_raw_value(1));
	EXPECT_EQ(L::lowest(), TL::lowest());
	EXPECT_EQ(L::max(), TL::max());
	EXPECT_EQ(L::epsilon(), TypeParam::from_raw_value(1));
	EXPECT_EQ(L::round_error(), TypeParam(0.5));
	EXPECT_EQ(L::denorm_min(), TypeParam::from_raw_value(1));
}

// Verify that types with a single integral bit (the fewest possible) work correctly
TEST(customizations, numeric_limits_edge)
{
	using Q14 = fpm::fixed<int16_t, int32_t, 14>;
	EXPECT_TRUE(HasMaximumError(static_cast<double>(std::numeric_limits<Q14>::max()), 1.999, 0.01));
	EXPECT_EQ(-2.0, static_cast<double>(std::numeric_limits<Q14>::lowest()));
	EXPECT_EQ(1.0, static_cast<double>(Q14{1}));
	EXPECT_EQ(0.5, static_cast<double>(std::numeric_limits<Q14>::round_error()));

	using Q30 = fpm::fixed<int32_t, int64_t, 30>;
	EXPECT_TRUE(HasMaximumError(static_cast<double>(std::numeric_limits<Q30>::max()), 1.999, 0.01));
	EXPECT_EQ(-2.0, static_cast<double>(std::numeric_limits<Q30>::lowest()));
	EXPECT_EQ(1.0, static_cast<double>(Q30{1}));

	using S = fpm::fixed<int8_t, int16_t, 6>;
	EXPECT_EQ(std::numeric_limits<S>::lowest(), S{-2});
	EXPECT_EQ(std::numeric_limits<S>::max(), S{1.984375});
	EXPECT_EQ(S{-1} + S{1}, S{0});
	EXPECT_EQ(S{-2}, S{2}); // Overflow
	EXPECT_EQ(S{0.75}, S{1.5} * S{0.5});
	EXPECT_EQ(S{1.5}, S{0.75} / S{0.5});
	EXPECT_EQ(S{-1.5}, S{0.75} / S{-0.5});

	using U = fpm::fixed<uint8_t, uint16_t, 7>;
	EXPECT_EQ(std::numeric_limits<U>::lowest(), U{0});
	EXPECT_EQ(std::numeric_limits<U>::min(), U::from_raw_value(1));
	EXPECT_EQ(std::numeric_limits<U>::max(), U{1.9921875});
	EXPECT_EQ(U{0}, U{2}); // Overflow
	EXPECT_EQ(U{1}, U{1.5} / U{1.5});
	EXPECT_EQ(U{1.5}, U{1.25} * U{1.25} - U{0.0625});
}
