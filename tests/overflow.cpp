#include "common.hpp"
#include <fpm/fixed/math.hpp>

#include <cmath>
#include <random>

// With FPM_DEFINED_OVERFLOW the results that a type cannot represent are defined: the ones of the operators wrap around
// (for signed types too), and the ones of the mathematical functions saturate. With FPM_CHECK_OVERFLOW the former are
// an error instead. Without both, only the results that a type can represent are calculated here.

template<typename T>
class overflow : public ::testing::Test
{
};

using OverflowTypes = ::testing::Types<
	fpm::fixed_4_4,
	fpm::fixed_8_8,
	fpm::fixed_16_16,
	fpm::fixed_24_8,
	fpm::fixed_8_24,
	fpm::fixed<int32_t, int64_t, 1>,
	fpm::fixed<int32_t, int64_t, 30>,
	fpm::fixed<int32_t, int64_t, 16, false>,
	fpm::fixed<uint8_t, uint16_t, 4>,
	fpm::fixed<uint16_t, uint32_t, 8>,
	fpm::fixed<uint32_t, uint64_t, 16>,
	fpm::fixed<uint32_t, uint64_t, 31, false>
#ifdef FPM_INT128
	,
	fpm::fixed_32_32,
	fpm::fixed_56_8,
	fpm::fixed_8_56,
	fpm::fixed<int64_t, FPM_INT128, 62>,
	fpm::fixed<int64_t, FPM_INT128, 1, false>
#endif
>;

TYPED_TEST_SUITE(overflow, OverflowTypes);

namespace
{
	constexpr bool defined = fpm::detail::defined_overflow;

	/// Whether the results of the operators wrap around
	constexpr bool wraps = fpm::detail::defined_overflow && !fpm::detail::checked_overflow;

#ifdef FPM_INT128
	// The exact results, which the tests below need (and are skipped without)
	using wide = fpm::int128_t;

	template<typename P>
	constexpr wide range = wide{1} << (sizeof(typename P::base_type) * 8);

	/// Whether the type can represent the raw value
	template<typename P>
	bool fits(const wide raw)
	{
		using B = typename P::base_type;
		return raw >= static_cast<wide>(std::numeric_limits<B>::lowest()) && raw <= static_cast<wide>(std::numeric_limits<B>::max());
	}

	/// The raw value that wraps around
	template<typename P>
	typename P::base_type wrapped(const wide raw)
	{
		return static_cast<typename P::base_type>(raw);
	}

	/// value / 2^bits like the operators: rounded to nearest (ties away from zero), or truncated towards zero
	template<typename P>
	wide scaled(const wide value, const int bits)
	{
		const wide divisor = wide{1} << bits;
		const wide quotient = value / divisor;
		const wide rest = value % divisor;
		if(!P::enable_rounding)
			return quotient;
		if(rest * 2 >= divisor)
			return quotient + 1;
		if(rest * 2 <= -divisor)
			return quotient - 1;
		return quotient;
	}
#endif

	/// The ends of the range and their neighbours, small values, and random ones of every magnitude
	template<typename P>
	std::vector<P> values(const int count = 300)
	{
		using B = typename P::base_type;
		constexpr B max = std::numeric_limits<B>::max();
		constexpr B lowest = std::numeric_limits<B>::lowest();
		std::vector<P> result;
		for(const B raw : {B{0}, B{1}, B{2}, B{3}, max, static_cast<B>(max - 1), static_cast<B>(max / 2), static_cast<B>(max / 2 + 1), static_cast<B>(max / 3),
			lowest, static_cast<B>(lowest + 1), static_cast<B>(lowest / 2), static_cast<B>(lowest / 2 - 1), static_cast<B>(B{1} << P::fraction_bits), static_cast<B>(B{0} - B{1}),
			static_cast<B>(B{0} - static_cast<B>(B{1} << P::fraction_bits))})
			result.push_back(P::from_raw_value(raw));
		std::mt19937_64 rng(P::fraction_bits * 31 + sizeof(B));
		for(int i = 0; i < count; ++i)
		{
			const auto shift = rng() % (sizeof(B) * 8);
			result.push_back(P::from_raw_value(static_cast<B>(rng() >> (64 - sizeof(B) * 8) >> shift)));
			result.push_back(P::from_raw_value(static_cast<B>(B{0} - static_cast<B>(rng() >> (64 - sizeof(B) * 8) >> shift))));
		}
		return result;
	}

	/// The result of an operator is the one of exact arithmetic, which wraps around if the type cannot represent it.
	/// Only the ones that it can represent are calculated if they do not.
	#define EXPECT_RESULT(expected_raw, expression) \
		do \
		{ \
			const wide exact = (expected_raw); \
			if(wraps || fits<P>(exact)) \
				ASSERT_EQ(wrapped<P>(exact), (expression).raw_value()) << #expression << " for " << static_cast<long double>(x.raw_value()) << " and " << static_cast<long double>(y.raw_value()); \
		} while(false)

	// Constant expressions, where undefined behavior does not compile
#if defined(FPM_DEFINED_OVERFLOW) && !defined(FPM_CHECK_OVERFLOW)
	template<typename P>
	constexpr bool constant_evaluation()
	{
		using L = std::numeric_limits<P>;
		constexpr auto unit = P::from_raw_value(1);
		P x = L::max();
		x += unit;
		P y = L::lowest();
		y -= unit;
		P z = L::max();
		z *= 3;
		P w = L::lowest();
		w /= -1;
		return L::max() + unit == L::lowest()
			&& L::lowest() - unit == L::max()
			&& -L::lowest() == L::lowest()
			&& L::max() + L::max() == P::from_raw_value(-2)
			&& L::lowest() + L::lowest() == P::from_raw_value(0)
			&& L::max() * 2 == P::from_raw_value(-2)
			&& L::lowest() * -1 == L::lowest()
			&& L::lowest() / -1 == L::lowest()
			&& L::lowest() / int64_t{-1} == L::lowest()
			&& abs(L::lowest()) == L::max()
			&& copysign(L::lowest(), unit) == L::max()
			&& copysign(L::max(), -unit) == -L::max()
			&& ceil(L::max()) == L::max()
			&& round(L::max()) == L::max()
			&& pow(L::lowest(), 3) == L::lowest()
			&& nextafter(L::max(), L::lowest()) == L::max() - unit
			&& x == L::lowest() && y == L::max() && z == L::max() - unit - unit && w == L::lowest()
			&& L::is_modulo;
	}

	static_assert(constant_evaluation<fpm::fixed_4_4>());
	static_assert(constant_evaluation<fpm::fixed_8_8>());
	static_assert(constant_evaluation<fpm::fixed_16_16>());
	static_assert(constant_evaluation<fpm::fixed<int32_t, int64_t, 16, false>>());
#ifdef FPM_INT128
	static_assert(constant_evaluation<fpm::fixed_32_32>());
	static_assert(constant_evaluation<fpm::fixed_8_56>());
#endif
	static_assert(fpm::fixed_16_16{200} * fpm::fixed_16_16{200} == fpm::fixed_16_16{40000 - 65536});
	static_assert(fpm::fixed_16_16{20000} / fpm::fixed_16_16{0.25} == fpm::fixed_16_16{80000 - 65536});
#endif
}

#ifdef FPM_INT128
TYPED_TEST(overflow, operators)
{
	using P = TypeParam;
	const int F = static_cast<int>(P::fraction_bits);
	const auto all = values<P>();

	for(const P x : all)
	{
		const wide a = x.raw_value();
		for(const P y : {all[0], all[1], all[4], all[5], all[9], all[10], all[13], all[14], all[20], all[21], all[40], all[41], all[100], all[101]})
		{
			const wide b = y.raw_value();
			EXPECT_RESULT(a + b, x + y);
			EXPECT_RESULT(a - b, x - y);
			EXPECT_RESULT(scaled<P>(a * b, F), x * y);
			if(b != 0)
			{
				const wide quotient = P::enable_rounding ? scaled<P>((a << (F + 1)) / b, 1) : (a << F) / b;
				EXPECT_RESULT(quotient, x / y);
			}

			// Compound assignments
			P z = x;
			if(wraps || (fits<P>(a + b) && fits<P>(a + b - b)))
			{
				z += y;
				z -= y;
				ASSERT_EQ(x, z);
			}
		}
		{
			const P y{};
			EXPECT_RESULT(-a, -x);
		}

		// With integers, of any type
		for(const int i : {0, 1, -1, 2, -2, 3, 7, -100, 127})
		{
			const P y{};
			const wide integer = wide{i} << F;
			if(std::is_unsigned_v<typename P::base_type> && i < 0)
				continue; // (their sum is another one for an unsigned type: see below)
			if(wraps || fits<P>(integer))
			{
				EXPECT_RESULT(a + integer, x + i);
				EXPECT_RESULT(a + integer, i + x);
				EXPECT_RESULT(a - integer, x - i);
				EXPECT_RESULT(integer - a, i - x);
			}
			EXPECT_RESULT(a * i, x * i);
			EXPECT_RESULT(a * i, i * x);
			EXPECT_RESULT(a * i, x * static_cast<int64_t>(i));
			EXPECT_RESULT(a * i, x * static_cast<int8_t>(i));
			if(i != 0)
			{
				EXPECT_RESULT(a / i, x / i);
				EXPECT_RESULT(a / i, x / static_cast<int64_t>(i));
				EXPECT_RESULT(a / i, x / static_cast<int8_t>(i));
			}
			if(i > 0)
			{
				EXPECT_RESULT(a * i, x * static_cast<unsigned>(i));
				EXPECT_RESULT(a * i, x * static_cast<uint64_t>(i));
				EXPECT_RESULT(a / i, x / static_cast<unsigned>(i));
				EXPECT_RESULT(a / i, x / static_cast<uint8_t>(i));
			}
		}
	}
}
#endif

// What every optimization level gives: the compiler may not assume that there is no overflow
TYPED_TEST(overflow, comparisons_after_overflow)
{
	using P = TypeParam;
	if(!wraps)
		GTEST_SKIP() << "only with FPM_DEFINED_OVERFLOW, and without FPM_CHECK_OVERFLOW";

	volatile auto raw = std::numeric_limits<typename P::base_type>::max();
	const P x = P::from_raw_value(raw);
	const P unit = P::from_raw_value(1);
	EXPECT_FALSE(x + unit > x);
	EXPECT_TRUE(x + unit < x);
	EXPECT_EQ(std::numeric_limits<P>::lowest(), x + unit);

	int steps = 0;
	for(P i = x - unit - unit; i >= x - unit - unit && steps < 10; i += unit)
		++steps;
	EXPECT_EQ(3, steps);
}

#ifdef FPM_INT128
// With FPM_DEFINED_OVERFLOW the results of the mathematical functions that a type cannot represent saturate:
// they are the maximum, or the lowest value for negative ones. This is the same with FPM_CHECK_OVERFLOW.
TYPED_TEST(overflow, functions_saturate)
{
	using P = TypeParam;
	using B = typename P::base_type;
	using L = std::numeric_limits<P>;
	if(!defined)
		GTEST_SKIP() << "only with FPM_DEFINED_OVERFLOW";
	const auto number = [](const P value) { return std::ldexp(static_cast<long double>(value.raw_value()), -static_cast<int>(P::fraction_bits)); };
	const long double unit = number(P::from_raw_value(1));
	const long double largest = number(L::max());
	const long double least = number(L::lowest());
	const long double tolerance = 12 * unit;
	const bool is_signed = std::is_signed_v<B>;
	const long double minus_one = -1;

	int beyond = 0;
	const auto expect = [&](const char* name, const P result, const long double exact, const long double first, const long double second = 0)
	{
		// Only the results that are clearly beyond the range: the ones next to its ends are rounded
		if(exact > largest + tolerance)
		{
			++beyond;
			EXPECT_EQ(L::max(), result) << name << " of " << static_cast<double>(first) << " and " << static_cast<double>(second);
		}
		else if(exact < least - tolerance)
		{
			++beyond;
			EXPECT_EQ(L::lowest(), result) << name << " of " << static_cast<double>(first) << " and " << static_cast<double>(second);
		}
	};
	const auto all = values<P>(100);
	for(const P x : all)
	{
		const long double v = number(x);
		expect("exp", exp(x), std::exp(v), v);
		expect("exp2", exp2(x), std::exp2(v), v);
		expect("expm1", expm1(x), std::expm1(v), v);
		if(v > 0)
		{
			expect("log", log(x), std::log(v), v);
			expect("log2", log2(x), std::log2(v), v);
			expect("log10", log10(x), std::log10(v), v);
		}
		if(v > minus_one)
			expect("log1p", log1p(x), std::log1p(v), v);
		if(v >= minus_one && v <= 1)
			expect("acos", acos(x), std::acos(v), v);
		if(x.raw_value() != 0)
		{
			for(const int e : {-7, -3, -2, -1, 1, 2, 3, 8, 31})
			{
				expect("pow with an integer", pow(x, e), std::pow(v, static_cast<long double>(e)), v, e);
				if(is_signed || e > 0)
					expect("pow with a fixed-point integer", pow(x, P::from_raw_value(wrapped<P>(wide{e} << P::fraction_bits))),
						fits<P>(wide{e} << P::fraction_bits) ? std::pow(v, static_cast<long double>(e)) : 0, v, e);
			}
		}
		for(const P y : {all[0], all[1], all[4], all[5], all[9], all[10], all[13], all[14], all[20], all[31]})
		{
			const long double w = number(y);
			if(x.raw_value() != 0 || y.raw_value() != 0)
				expect("atan2", atan2(x, y), std::atan2(v, w), v, w);
			expect("hypot", hypot(x, y), std::hypot(v, w), v, w);
			if(v > 0 && (y.raw_value() & (P{1}.raw_value() - 1)) != 0)
				expect("pow", pow(x, y), std::pow(v, w), v, w);
		}
	}
	// Every type has results that it cannot represent
	EXPECT_GT(beyond, 0);

	const P two = P::from_raw_value(static_cast<B>(B{1} << (P::fraction_bits + 1 > static_cast<unsigned>(L::digits) - 1 ? P::fraction_bits : P::fraction_bits + 1)));
	if(largest < 4)
		EXPECT_EQ(L::max(), pow(L::max(), 2));
	else
		EXPECT_EQ(L::max(), pow(two, L::digits));
	if(is_signed)
	{
		EXPECT_EQ(L::lowest(), pow(L::lowest(), 3));
		EXPECT_EQ(L::max(), pow(L::lowest(), 2));
		EXPECT_EQ(L::lowest(), pow(L::lowest(), 1));
		if(largest < 3)
		{
			// The type cannot represent π
			EXPECT_EQ(L::max(), acos(P{-1}));
			EXPECT_EQ(L::max(), atan2(P{}, P{-1}));
			EXPECT_EQ(L::max(), atan2(P::from_raw_value(1), P{-1}));
			EXPECT_EQ(L::lowest(), atan2(P::from_raw_value(-1), P{-1}));
		}
	}
	else
	{
		// No negative numbers: the logarithms of numbers below 1 are 0
		EXPECT_EQ(P{}, log(P::from_raw_value(1)));
		EXPECT_EQ(P{}, log2(P::from_raw_value(1)));
		EXPECT_EQ(P{}, log10(P::from_raw_value(1)));
		EXPECT_EQ(P{}, log2(P{1}));
	}
}
#endif

// The mathematical functions with the ends of the range and other values: their behavior is defined
// (the sanitizers of the debug build report what is not)
TYPED_TEST(overflow, functions)
{
	using P = TypeParam;
	using B = typename P::base_type;
	if(!wraps)
		GTEST_SKIP() << "only with FPM_DEFINED_OVERFLOW, and without FPM_CHECK_OVERFLOW";

	const auto max = std::numeric_limits<P>::max();
	const auto lowest = std::numeric_limits<P>::lowest();
	const bool is_signed = std::is_signed_v<B>;
	const auto all = values<P>(60);
	B sink = 0;
	const auto use = [&](const P result) { sink = static_cast<B>(sink ^ result.raw_value()); };

	for(const P x : all)
	{
		// Any number
		use(abs(x));
		use(ceil(x));
		use(floor(x));
		use(trunc(x));
		use(round(x));
		use(nearbyint(x));
		use(rint(x));
		use(cbrt(x));
		use(sin(x));
		use(cos(x));
		use(tan(x));
		use(atan(x));
		use(exp(x));
		use(exp2(x));
		use(expm1(x));
		use(nextafter(x, max));
		use(nextafter(x, lowest));
		use(nexttoward(x, max));
		use(pow(x, 2));
		use(pow(x, 3));
		use(pow(x, 7));
		P integral{};
		use(modf(x, &integral));
		use(integral);
		use(P{static_cast<B>(fpclassify(x) + isfinite(x) + isinf(x) + isnan(x) + isnormal(x) + signbit(x))});

		// Not negative
		if(x.raw_value() >= 0)
			use(sqrt(x));
		if(x.raw_value() > 0)
		{
			use(log(x));
			use(log2(x));
			use(log10(x));
			use(pow(x, -1));
			use(pow(x, -2));
		}

		// Above -1
		if(!is_signed || x.raw_value() > static_cast<B>(B{0} - static_cast<B>(B{1} << P::fraction_bits)))
			use(log1p(x));

		// At most 1
		if(x <= P{1} && (!is_signed || x >= -P{1}))
		{
			use(asin(x));
			use(acos(x));
		}

		for(const P y : {all[0], all[1], all[4], all[5], all[9], all[10], all[13], all[14], all[20], all[31]})
		{
			use(atan2(x, y));
			use(hypot(x, y));
			use(copysign(x, y));
			if(y.raw_value() != 0)
			{
				use(fmod(x, y));
				use(remainder(x, y));
				int quotient = 0;
				use(remquo(x, y, &quotient));
				sink = static_cast<B>(sink ^ static_cast<B>(quotient));
			}
			if(x.raw_value() > 0)
				use(pow(x, y));
		}
	}

	// As documented
	EXPECT_EQ(is_signed ? max : lowest, abs(lowest));
	EXPECT_EQ(is_signed ? max : lowest, copysign(lowest, max));
	EXPECT_EQ(max, ceil(max));
	EXPECT_EQ(max, round(max));
	EXPECT_EQ(max, nearbyint(max));
	EXPECT_EQ(max, rint(max));
	EXPECT_EQ(floor(max), trunc(max));
	EXPECT_EQ(lowest, floor(lowest));
	EXPECT_EQ(max, exp(max));
	EXPECT_EQ(max, exp2(max));
	EXPECT_EQ(max, hypot(max, max));
	EXPECT_EQ(max, nextafter(max, max));
	EXPECT_EQ(lowest, nextafter(max, lowest) == max - P::from_raw_value(1) ? nextafter(lowest, lowest) : max);
	if(is_signed)
	{
		EXPECT_EQ(max, hypot(lowest, lowest));
		EXPECT_EQ(max, hypot(lowest, P{}));
		EXPECT_EQ(-cbrt(max), cbrt(-max));
		EXPECT_LT(cbrt(lowest), P{});
		EXPECT_GE(exp(lowest), P{});
		EXPECT_LT(exp(lowest), P{1});
	}
	static_cast<void>(sink);
}

#ifdef FPM_INT128
// Functions with an intermediate value that can be beyond the range while the result is not: 1 + x for log1p and
// e^x for expm1. And the results of exp, exp2 and expm1 next to the maximum: they saturate, also where they round up to it.
// None of them uses the operators, so this is the same with FPM_CHECK_OVERFLOW.
TYPED_TEST(overflow, functions_next_to_the_maximum)
{
	using P = TypeParam;
	using B = typename P::base_type;
	if(long_double_functions_digits() < std::numeric_limits<B>::digits)
		GTEST_SKIP() << "the functions of long double are less precise than the type";
	const auto number = [](const P value) { return std::ldexp(static_cast<long double>(value.raw_value()), -static_cast<int>(P::fraction_bits)); };
	const long double unit = number(P::from_raw_value(1));
	const long double largest = number(std::numeric_limits<P>::max());
	const long double least = number(std::numeric_limits<P>::lowest());
	const long double tolerance = (sizeof(B) == 1 ? 3 : sizeof(B) == 2 ? 4 : 10) * unit;
	const auto from_number = [&](const long double value) { return P::from_raw_value(static_cast<B>(std::llroundl(value / unit))); };

	auto inputs = values<P>();
	for(int i = 0; i <= 64; ++i)
	{
		// e^x from the maximum to 1 more than it, 2^x up to the maximum, and 1 + x around the top of the range
		inputs.push_back(from_number(std::log(largest + static_cast<long double>(i) / 64)));
		inputs.push_back(from_number(std::log2(largest) - static_cast<long double>(i) * unit));
		inputs.push_back(P::from_raw_value(static_cast<B>(std::numeric_limits<B>::max() - static_cast<B>(i))));
		const wide below_one_less = static_cast<wide>(std::numeric_limits<B>::max()) - (wide{1} << P::fraction_bits) + (i - 32);
		if(fits<P>(below_one_less))
			inputs.push_back(P::from_raw_value(static_cast<B>(below_one_less)));
	}

	const auto expect = [&](const char* name, const P x, const P result, const long double exact)
	{
		if(exact < least)
			return; // not representable, and nothing saturates there
		if(exact <= largest)
			EXPECT_LE(static_cast<double>(std::fabs(number(result) - exact) / unit), static_cast<double>(tolerance / unit)) << name << " of the raw value " << +x.raw_value();
		else if(exact > largest + tolerance)
			EXPECT_EQ(std::numeric_limits<P>::max(), result) << name << " of the raw value " << +x.raw_value();
		else
			EXPECT_GE(number(result), largest - tolerance) << name << " of the raw value " << +x.raw_value();
	};
	for(const P x : inputs)
	{
		const long double v = number(x);
		expect("exp", x, exp(x), std::exp(v));
		expect("exp2", x, exp2(x), std::exp2(v));
		expect("expm1", x, expm1(x), std::expm1(v));
		if(v > -1)
			expect("log1p", x, log1p(x), std::log1p(v));
	}
}
#endif

#if defined(FPM_CHECK_OVERFLOW) && !defined(NDEBUG)
// With FPM_CHECK_OVERFLOW a result that the type cannot represent is an error
TYPED_TEST(overflow, checked)
{
	using P = TypeParam;
	using B = typename P::base_type;
	const auto max = std::numeric_limits<P>::max();
	const auto lowest = std::numeric_limits<P>::lowest();
	const auto unit = P::from_raw_value(1);
	const bool is_signed = std::is_signed_v<B>;

	EXPECT_DEATH_IF_SUPPORTED(auto v = max + unit, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = lowest - unit, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = max + max, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = max * 2, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = max * 2u, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = max * int64_t{1000000}, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = max * max, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = max / P::from_raw_value(static_cast<B>(B{1} << (P::fraction_bits - 1))), ""); // by 0.5
	EXPECT_DEATH_IF_SUPPORTED(auto v = max + 1, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = 1 + max, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = lowest - 1, "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = nextafter(max, lowest) + unit + unit, "");
	EXPECT_DEATH_IF_SUPPORTED(P v = max; v += unit, "");
	EXPECT_DEATH_IF_SUPPORTED(P v = lowest; v -= unit, "");
	EXPECT_DEATH_IF_SUPPORTED(P v = max; v *= 3, "");
	if(is_signed)
	{
		EXPECT_DEATH_IF_SUPPORTED(auto v = -lowest, "");
		EXPECT_DEATH_IF_SUPPORTED(auto v = lowest / -1, "");
		EXPECT_DEATH_IF_SUPPORTED(auto v = lowest * -1, "");
		if(!defined)
			EXPECT_DEATH_IF_SUPPORTED(auto v = abs(lowest), "");
		EXPECT_DEATH_IF_SUPPORTED(auto v = lowest + lowest, "");
		EXPECT_DEATH_IF_SUPPORTED(auto v = 1 - lowest, "");
	}
	else
	{
		// Negative
		EXPECT_DEATH_IF_SUPPORTED(auto v = -unit, "");
		EXPECT_DEATH_IF_SUPPORTED(auto v = P{} - unit, "");
		EXPECT_DEATH_IF_SUPPORTED(auto v = unit * -1, "");
		EXPECT_DEATH_IF_SUPPORTED(auto v = max / -1, "");
	}
	// An integer that the type cannot represent
	EXPECT_DEATH_IF_SUPPORTED(auto v = P{} + std::numeric_limits<int64_t>::max(), "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = std::numeric_limits<int64_t>::max() - P{}, "");

	// The ends of the range are results like any other
	EXPECT_EQ(max, (max - unit) + unit);
	EXPECT_EQ(lowest, (lowest + unit) - unit);
	EXPECT_EQ(P{}, max - max);
	EXPECT_EQ(P{}, lowest - lowest);
	EXPECT_EQ(max, max * 1);
	EXPECT_EQ(lowest, lowest * 1);
	EXPECT_EQ(max, max / 1);
	EXPECT_EQ(lowest, lowest / 1);
	EXPECT_EQ(lowest, lowest / P{1});
	EXPECT_EQ(max, max * P{1});
	EXPECT_EQ(P{}, max * 0);
	EXPECT_EQ(P{}, P{} * std::numeric_limits<int64_t>::lowest());
	EXPECT_EQ(P{}, P{} / -7);
	EXPECT_EQ(unit, unit / P{1});
	if(is_signed)
	{
		EXPECT_EQ(-unit, max + lowest);
		EXPECT_EQ(-max, lowest + unit);
		EXPECT_EQ(-max, max * -1);
		EXPECT_EQ(-max, max / -1);
		EXPECT_EQ(max, abs(-max));
	}
}
#endif
