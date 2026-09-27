#include "common.hpp"

#include <random>

namespace
{
	template<typename P>
	using raw_t = typename P::base_type;

	/// Random raw values spread over all magnitudes
	template<typename P>
	std::vector<raw_t<P>> random_raws(const int count, const int max_bits = std::numeric_limits<raw_t<P>>::digits)
	{
		using B = raw_t<P>;
		std::mt19937_64 rng(4242 + P::fraction_bits);
		std::vector<B> values;
		for(int i = 0; i < count; ++i)
		{
			const int bits = 1 + static_cast<int>(rng() % max_bits);
			auto raw = static_cast<B>(rng() >> (64 - bits));
			if(std::is_signed_v<B> && rng() % 2)
				raw = static_cast<B>(-raw);
			values.push_back(raw);
		}
		return values;
	}

#ifdef __SIZEOF_INT128__
	using wide_t = __int128;

	/// a / b rounded to nearest, ties away from zero (the library's rounding mode)
	wide_t divide_round(const wide_t a, const wide_t b)
	{
		const wide_t q = a / b;
		const wide_t r = a % b;
		const wide_t abs_r = r < 0 ? -r : r;
		const wide_t abs_b = b < 0 ? -b : b;
		if(abs_r * 2 >= abs_b)
			return ((a < 0) != (b < 0)) ? q - 1 : q + 1;
		return q;
	}
#endif
}

template<typename T>
class core : public ::testing::Test
{
};

using CoreTypes = ::testing::Types<
	fpm::fixed_4_4,
	fpm::fixed_8_8,
	fpm::fixed_16_16,
	fpm::fixed_24_8,
	fpm::fixed_8_24,
	fpm::fixed<int32_t, int64_t, 16, false>
#ifdef FPM_INT128
	,
	fpm::fixed_32_32,
	fpm::fixed_48_16,
	fpm::fixed_16_48,
	fpm::fixed_8_56
#endif
>;
TYPED_TEST_SUITE(core, CoreTypes);

#ifdef __SIZEOF_INT128__
TYPED_TEST(core, multiplication_is_exactly_rounded)
{
	using P = TypeParam;
	constexpr auto F = P::fraction_bits;
	constexpr int half_bits = std::numeric_limits<raw_t<P>>::digits / 2;

	const auto xs = random_raws<P>(3000, half_bits + static_cast<int>(F) / 2);
	for(std::size_t i = 0; i + 1 < xs.size(); ++i)
	{
		const wide_t product = wide_t{xs[i]} * xs[i + 1];
		const wide_t expected = P::enable_rounding ? divide_round(product, wide_t{1} << F) : product / (wide_t{1} << F);
		if(expected > std::numeric_limits<raw_t<P>>::max() || expected < std::numeric_limits<raw_t<P>>::min())
			continue;
		const auto x = P::from_raw_value(xs[i]);
		const auto y = P::from_raw_value(xs[i + 1]);
		ASSERT_EQ(static_cast<int64_t>(expected), static_cast<int64_t>((x * y).raw_value())) << xs[i] << " * " << xs[i + 1];
		auto z = x;
		z *= y;
		ASSERT_EQ(x * y, z);
	}
}

TYPED_TEST(core, division_is_exactly_rounded)
{
	using P = TypeParam;
	constexpr auto F = P::fraction_bits;

	const auto xs = random_raws<P>(3000);
	for(std::size_t i = 0; i + 1 < xs.size(); ++i)
	{
		if(xs[i + 1] == 0)
			continue;
		const wide_t numerator = wide_t{xs[i]} << F;
		const wide_t expected = P::enable_rounding ? divide_round(numerator, xs[i + 1]) : numerator / xs[i + 1];
		if(expected > std::numeric_limits<raw_t<P>>::max() || expected < std::numeric_limits<raw_t<P>>::min())
			continue;
		const auto x = P::from_raw_value(xs[i]);
		const auto y = P::from_raw_value(xs[i + 1]);
		ASSERT_EQ(static_cast<int64_t>(expected), static_cast<int64_t>((x / y).raw_value())) << xs[i] << " / " << xs[i + 1];
		auto z = x;
		z /= y;
		ASSERT_EQ(x / y, z);
	}
}
#endif

TYPED_TEST(core, integer_arithmetic)
{
	using P = TypeParam;
	for(int i = -2; i <= 2; ++i) // within the range of all types (fixed_4_4: [-8, 8))
	{
		EXPECT_EQ(P(i), P(i) * 1);
		EXPECT_EQ(P(2 * i), P(i) * 2);
		EXPECT_EQ(P(2 * i), 2 * P(i));
		EXPECT_EQ(P(i + 3), P(i) + 3);
		EXPECT_EQ(P(i - 3), P(i) - 3);
		EXPECT_EQ(P(3 - i), 3 - P(i));

		auto x = P(i);
		x += 2;
		EXPECT_EQ(P(i + 2), x);
		x -= 4;
		EXPECT_EQ(P(i - 2), x);
		x *= 2;
		EXPECT_EQ(P(2 * i - 4), x);
		x /= 2;
		EXPECT_EQ(P(i - 2), x);
	}
}

TYPED_TEST(core, integer_conversion_truncates)
{
	using P = TypeParam;
	EXPECT_EQ(2, static_cast<int>(P(2.75)));
	EXPECT_EQ(-2, static_cast<int>(P(-2.75)));
	EXPECT_EQ(0, static_cast<int>(P(0.5)));

	// Like static_cast between integers, construction keeps the bits that fit (modular arithmetic, no overflow)
	using B = raw_t<P>;
	using U = std::make_unsigned_t<B>;
	constexpr auto F = P::fraction_bits;
	for(const int64_t v : {int64_t{1} << 40, -(int64_t{1} << 40) - 7, int64_t{1234567890123}, int64_t{-1}})
	{
		const auto expected = static_cast<B>(static_cast<U>(static_cast<uint64_t>(v) << F));
		EXPECT_EQ(expected, P(v).raw_value()) << v;
	}
}

TYPED_TEST(core, float_conversion)
{
	using P = TypeParam;
	constexpr double epsilon = static_cast<double>(std::numeric_limits<P>::epsilon());
	for(const double v : {0.0, 1.0, -1.0, 0.3, -0.3, 1.75, -1.75, 3.14159})
	{
		const double converted = static_cast<double>(P(v));
		EXPECT_LE(std::abs(converted - v), P::enable_rounding ? epsilon / 2 : epsilon) << v;
		EXPECT_EQ(static_cast<float>(P(v)), static_cast<float>(static_cast<double>(P(v))));
	}
}

TYPED_TEST(core, numeric_limits)
{
	using P = TypeParam;
	using L = std::numeric_limits<P>;
	EXPECT_EQ(L::round_style, P::enable_rounding ? std::round_to_nearest : std::round_toward_zero);
	EXPECT_EQ(0.5, static_cast<double>(L::round_error()));
	EXPECT_EQ(raw_t<P>{1}, L::epsilon().raw_value());
	EXPECT_EQ(std::numeric_limits<raw_t<P>>::max(), L::max().raw_value());
	EXPECT_EQ(std::numeric_limits<raw_t<P>>::lowest(), L::lowest().raw_value());
}

TYPED_TEST(core, constants)
{
	using P = TypeParam;
	constexpr double epsilon = static_cast<double>(std::numeric_limits<P>::epsilon());
	const double tolerance = P::enable_rounding ? epsilon / 2 : epsilon;
	EXPECT_LE(std::abs(static_cast<double>(P::pi()) - 3.14159265358979323846), tolerance);
	EXPECT_LE(std::abs(static_cast<double>(P::half_pi()) - 1.57079632679489661923), tolerance);
	EXPECT_LE(std::abs(static_cast<double>(P::two_pi()) - 6.28318530717958647692), tolerance);
	EXPECT_LE(std::abs(static_cast<double>(P::e()) - 2.71828182845904523536), tolerance);
}

#ifndef NDEBUG
TYPED_TEST(core, division_by_zero_asserts)
{
	using P = TypeParam;
	// Integer division by zero doesn't trap on every architecture (e.g. ARM64), so these must assert
	EXPECT_DEATH(auto v = P(1) / P(0), "");
	EXPECT_DEATH(auto v = P(1) / 0, "");
	EXPECT_DEATH({ auto v = P(1); v /= P(0); }, "");
	EXPECT_DEATH({ auto v = P(1); v /= 0; }, "");
}
#endif

TEST(core, fixed_to_fixed_conversions)
{
	// Every combination of base type and fraction bits, compared with the exactly rounded value
	const auto check = []<typename From, typename To>(From, To)
	{
		for(const double v : {0.0, 1.0, -1.0, 2.25, -2.25, 3.1415926, -3.1415926, 100.5, -100.5, 0.0078125})
		{
			const From from(v);
			const double exact = static_cast<double>(from);
			if(std::abs(exact) >= static_cast<double>(std::numeric_limits<To>::max()))
				continue;
			const To to(from);
			EXPECT_EQ(To(exact), to) << v << ": " << static_cast<double>(to) << " vs " << exact;
		}
	};
	const auto check_all_to = [&]<typename From>(From from)
	{
		check(from, fpm::fixed_8_8{});
		check(from, fpm::fixed_16_16{});
		check(from, fpm::fixed_24_8{});
		check(from, fpm::fixed_8_24{});
#ifdef FPM_INT128
		check(from, fpm::fixed_32_32{});
		check(from, fpm::fixed_48_16{});
		check(from, fpm::fixed_8_56{});
#endif
	};
	check_all_to(fpm::fixed_8_8{});
	check_all_to(fpm::fixed_16_16{});
	check_all_to(fpm::fixed_24_8{});
	check_all_to(fpm::fixed_8_24{});
#ifdef FPM_INT128
	check_all_to(fpm::fixed_32_32{});
	check_all_to(fpm::fixed_48_16{});
	check_all_to(fpm::fixed_8_56{});

	// Used to produce -0.25 (1 << 32 on int)
	EXPECT_EQ(fpm::fixed_32_32(-2.25), static_cast<fpm::fixed_32_32>(fpm::fixed_16_16(-2.25)));
	EXPECT_EQ(fpm::fixed_32_32(-2.25), fpm::fixed_32_32(fpm::fixed_16_16(-2.25)));
#endif
}

TEST(core, from_fixed_point)
{
#ifdef FPM_INT128
	// Shifts of 32 bits and more
	EXPECT_EQ(fpm::fixed_32_32(5), fpm::fixed_32_32::from_fixed_point<0>(5));
	EXPECT_EQ(fpm::fixed_32_32(-5), fpm::fixed_32_32::from_fixed_point<0>(-5));
	EXPECT_EQ(fpm::fixed_8_56(1.5), fpm::fixed_8_56::from_fixed_point<1>(3));
	EXPECT_EQ(fpm::fixed_16_48(0.75), fpm::fixed_16_48::from_fixed_point<2>(int64_t{3}));
#endif
	EXPECT_EQ(fpm::fixed_8_24(1.5), fpm::fixed_8_24::from_fixed_point<1>(3));
	static_assert(fpm::fixed_16_16::from_fixed_point<0>(7) == fpm::fixed_16_16(7));
}

TEST(core, from_custom_fraction)
{
	using P = fpm::fixed_16_16;
	// 0.1 = 6553.6 epsilon: rounds to nearest
	EXPECT_EQ(6554, P::from_custom_fraction<10>(0, 1).raw_value());
	EXPECT_EQ(-6554, P::from_custom_fraction<10>(0, -1).raw_value());
	EXPECT_EQ(3 * 65536 + 6554, P::from_custom_fraction<10>(3, 1).raw_value());
	EXPECT_EQ(P(3.141592), P::from_custom_fraction<1000000>(3, 141592));
	EXPECT_EQ(P(-3.141592), P::from_custom_fraction<1000000>(-3, -141592));

	using T = fpm::fixed<int32_t, int64_t, 16, false>;
	EXPECT_EQ(6553, T::from_custom_fraction<10>(0, 1).raw_value());

	// Denominators beyond the intermediate type's range for the fraction
	EXPECT_EQ(fpm::fixed_8_24(0.5), fpm::fixed_8_24::from_custom_fraction<1'000'000'000'000>(int64_t{0}, int64_t{500'000'000'000}));

#ifdef FPM_INT128
	// Integer parts that need more than 32 bits once shifted
	EXPECT_EQ(fpm::fixed_32_32(5.25), fpm::fixed_32_32::from_custom_fraction<1000>(5, 250));
	EXPECT_EQ(fpm::fixed_16_48(-1.5), fpm::fixed_16_48::from_custom_fraction<10>(-1, -5));
#endif
	static_assert(P::from_custom_fraction<4>(1, 1) == P(1.25));
}
