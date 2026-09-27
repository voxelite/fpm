#include "common.hpp"
#include <fpm/math.hpp>

#include <random>

// Accuracy of the math functions for many fixed-point types, against `long double` references.
//
// Correctly rounded functions must be within half an epsilon. For approximations, the allowed error is
// a model of the approximation's own error plus rounding errors, based on the measured accuracy.
// These guard against losing precision.

namespace
{
	template<typename P>
	constexpr long double eps = static_cast<long double>(std::numeric_limits<P>::epsilon());

	template<typename P>
	constexpr long double max_value = static_cast<long double>(std::numeric_limits<P>::max());

	/// Fraction bits of the polynomial evaluations for `IntegralBits` integral bits (see fpm::detail::poly_bits)
	template<typename P, int IntegralBits>
	inline const long double poly_eps = std::ldexp(1.0L, -std::max<int>(
		std::numeric_limits<std::make_signed_t<typename P::base_type>>::digits - IntegralBits, P::fraction_bits));

	template<typename P>
	long double ld(const P x)
	{
		return static_cast<long double>(x);
	}

	/// Random values in [lo, hi] (quantized to P), evenly spread plus the end points
	template<typename P>
	std::vector<P> values(const long double lo, const long double hi, const int count = 2000)
	{
		std::mt19937_64 rng(1234 + P::fraction_bits * 7 + sizeof(typename P::base_type));
		std::uniform_real_distribution<double> d(0.0, 1.0);
		std::vector<P> result{P(static_cast<double>(lo)), P(static_cast<double>(hi))};
		for(int i = 0; i < count; ++i)
			result.push_back(P(static_cast<double>(lo + (hi - lo) * static_cast<long double>(d(rng)))));
		return result;
	}

	/// Random raw values over all magnitudes (positive, or both signs)
	template<typename P>
	std::vector<P> raw_values(const bool allow_negative, const int count = 2000)
	{
		using B = typename P::base_type;
		std::mt19937_64 rng(99 + P::fraction_bits);
		std::vector<P> result{P::from_raw_value(1), std::numeric_limits<P>::max()};
		for(int i = 0; i < count; ++i)
		{
			const int bits = 1 + static_cast<int>(rng() % std::numeric_limits<B>::digits);
			auto raw = static_cast<B>((rng() >> (64 - bits)) | 1);
			if(allow_negative && rng() % 2)
				raw = static_cast<B>(-raw);
			result.push_back(P::from_raw_value(raw));
		}
		return result;
	}

	::testing::AssertionResult within(const long double got, const long double expected, const long double tolerance)
	{
		if(std::abs(got - expected) <= tolerance)
			return ::testing::AssertionSuccess();
		return ::testing::AssertionFailure() << std::setprecision(20) << got << " differs from " << expected
			<< " by " << std::abs(got - expected) << " > " << tolerance;
	}
}

template<typename T>
class math_types : public ::testing::Test
{
};

using MathTypes = ::testing::Types<
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
TYPED_TEST_SUITE(math_types, MathTypes);

// Correctly rounded functions

TYPED_TEST(math_types, sqrt)
{
	using P = TypeParam;
	// Half an epsilon, plus the precision of the reference
	const long double tolerance = eps<P> * 0.5L * (1 + 1e-12L);
	for(const auto x : raw_values<P>(false))
		ASSERT_TRUE(within(ld(sqrt(x)), std::sqrt(ld(x)), tolerance)) << "x = " << ld(x);
	EXPECT_EQ(P(0), sqrt(P(0)));
}

TYPED_TEST(math_types, cbrt)
{
	using P = TypeParam;
	const long double tolerance = eps<P> * 0.5L * (1 + 1e-12L);
	for(const auto x : raw_values<P>(true))
		ASSERT_TRUE(within(ld(cbrt(x)), std::cbrt(ld(x)), tolerance)) << "x = " << ld(x);
	EXPECT_EQ(P(0), cbrt(P(0)));
}

TYPED_TEST(math_types, hypot)
{
	using P = TypeParam;
	const long double tolerance = eps<P> * 0.5L * (1 + 1e-12L);
	const auto xs = raw_values<P>(true);
	for(std::size_t i = 0; i + 1 < xs.size(); ++i)
	{
		const auto expected = std::hypot(ld(xs[i]), ld(xs[i + 1]));
		const auto got = hypot(xs[i], xs[i + 1]);
		if(expected >= max_value<P>)
			ASSERT_EQ(std::numeric_limits<P>::max(), got); // saturates
		else
			ASSERT_TRUE(within(ld(got), expected, tolerance)) << "x = " << ld(xs[i]) << ", y = " << ld(xs[i + 1]);
	}
	EXPECT_EQ(P(0), hypot(P(0), P(0)));
	EXPECT_EQ(P(5), hypot(P(3), P(-4)));
	EXPECT_EQ(std::numeric_limits<P>::max(), hypot(std::numeric_limits<P>::lowest(), std::numeric_limits<P>::lowest()));
}

// Exact functions

TYPED_TEST(math_types, rounding)
{
	using P = TypeParam;
	for(const auto x : raw_values<P>(true))
	{
		const auto v = ld(x);
		const auto check = [&](const P got, const long double expected, const char* name)
		{
			if(expected <= max_value<P> && expected >= ld(std::numeric_limits<P>::lowest()))
				ASSERT_EQ(expected, ld(got)) << name << "(" << v << ")";
		};
		check(floor(x), std::floor(v), "floor");
		check(ceil(x), std::ceil(v), "ceil");
		check(trunc(x), std::trunc(v), "trunc");
		check(round(x), std::round(v), "round");
		check(nearbyint(x), std::nearbyint(v), "nearbyint");
		check(rint(x), std::rint(v), "rint");

		P integral;
		const auto fraction = modf(x, &integral);
		long double expected_integral;
		const auto expected_fraction = std::modf(v, &expected_integral);
		ASSERT_EQ(expected_fraction, ld(fraction));
		ASSERT_EQ(expected_integral, ld(integral));
	}
}

TYPED_TEST(math_types, remainders)
{
	using P = TypeParam;
	const auto xs = raw_values<P>(true);
	for(std::size_t i = 0; i + 1 < xs.size(); ++i)
	{
		const auto x = xs[i];
		const auto y = xs[i + 1];
		ASSERT_EQ(std::fmod(ld(x), ld(y)), ld(fmod(x, y))) << ld(x) << " fmod " << ld(y);
		ASSERT_EQ(std::remainder(ld(x), ld(y)), ld(remainder(x, y))) << ld(x) << " remainder " << ld(y);

		int quo = 0;
		int expected_quo = 0;
		const auto rem = remquo(x, y, &quo);
		const auto expected_rem = std::remquo(ld(x), ld(y), &expected_quo);
		ASSERT_EQ(expected_rem, ld(rem));
		ASSERT_EQ(expected_quo % 8, quo % 8) << ld(x) << " remquo " << ld(y);
	}

	// The one case where native integer division overflows
	const auto lowest = std::numeric_limits<P>::lowest();
	const auto minus_epsilon = -std::numeric_limits<P>::epsilon();
	EXPECT_EQ(P(0), fmod(lowest, minus_epsilon));
	EXPECT_EQ(P(0), remainder(lowest, minus_epsilon));
}

// Approximations

TYPED_TEST(math_types, sin_cos_tan)
{
	using P = TypeParam;
	// The polynomial's error is below 4e-4; range reduction is precise for all arguments
	const long double tolerance = 5e-4L + 2 * eps<P>;
	for(const auto x : raw_values<P>(true))
	{
		ASSERT_TRUE(within(ld(sin(x)), std::sin(ld(x)), tolerance)) << "sin(" << ld(x) << ")";
		ASSERT_TRUE(within(ld(cos(x)), std::cos(ld(x)), tolerance)) << "cos(" << ld(x) << ")";
	}
	for(const auto x : values<P>(-1.3L, 1.3L))
	{
		const auto t = std::tan(ld(x));
		ASSERT_TRUE(within(ld(tan(x)), t, tolerance * 2 * (1 + t * t))) << "tan(" << ld(x) << ")";
	}
}

TYPED_TEST(math_types, inverse_trigonometry)
{
	using P = TypeParam;
	for(const auto x : raw_values<P>(true))
		ASSERT_TRUE(within(ld(atan(x)), std::atan(ld(x)), 1e-3L + 2 * eps<P>)) << "atan(" << ld(x) << ")";
	for(const auto x : values<P>(-1, 1))
	{
		// Plus the rounding errors of the square root, division and multiplication by two
		ASSERT_TRUE(within(ld(asin(x)), std::asin(ld(x)), 1e-3L + 4 * eps<P>)) << "asin(" << ld(x) << ")";
		ASSERT_TRUE(within(ld(acos(x)), std::acos(ld(x)), 2e-3L + 4 * eps<P>)) << "acos(" << ld(x) << ")";
	}
	for(const auto angle : values<P>(-3.1L, 3.1L))
	{
		const auto y = P(std::sin(static_cast<double>(angle)));
		const auto x = P(std::cos(static_cast<double>(angle)));
		if(x == P(0) && y == P(0))
			continue;
		ASSERT_TRUE(within(ld(atan2(y, x)), std::atan2(ld(y), ld(x)), 1e-3L + 2 * eps<P>)) << "atan2(" << ld(y) << ", " << ld(x) << ")";
	}
}

TYPED_TEST(math_types, logarithms)
{
	using P = TypeParam;
	// The polynomial's error is below 1.3e-5 (in log2)
	for(const auto x : raw_values<P>(false))
	{
		ASSERT_TRUE(within(ld(log2(x)), std::log2(ld(x)), 1.5e-5L + 4 * poly_eps<P, 1> + 2 * eps<P>)) << "log2(" << ld(x) << ")";
		ASSERT_TRUE(within(ld(log(x)), std::log(ld(x)), 1.1e-5L + 2 * eps<P>)) << "log(" << ld(x) << ")";
		ASSERT_TRUE(within(ld(log10(x)), std::log10(ld(x)), 0.5e-5L + 2 * eps<P>)) << "log10(" << ld(x) << ")";
	}
}

TYPED_TEST(math_types, exponentials)
{
	using P = TypeParam;
	const long double max_log2 = std::log2(max_value<P>);
	const long double lowest = ld(std::numeric_limits<P>::lowest());

	for(const auto x : values<P>(std::max(lowest, -max_log2 - 70), max_log2 - 0.01L))
	{
		// The polynomial's relative error is below 1.2e-7, plus the precision of its evaluation
		const auto expected = std::exp2(ld(x));
		ASSERT_TRUE(within(ld(exp2(x)), expected, (2e-7L + 4 * poly_eps<P, 1>) * expected + eps<P>)) << "exp2(" << ld(x) << ")";
	}

	const long double max_log = std::log(max_value<P>);
	for(const auto x : values<P>(std::max(lowest, -max_log - 50), max_log - 0.01L))
	{
		// The polynomial's relative error is below 1.2e-6, and e^n uses e rounded to the type's precision
		const auto expected = std::exp(ld(x));
		const auto n = std::abs(std::floor(ld(x)));
		const auto tolerance = (2e-6L + 4 * poly_eps<P, 2> + 2 * n * eps<P>) * expected + 2 * eps<P>;
		ASSERT_TRUE(within(ld(exp(x)), expected, tolerance)) << "exp(" << ld(x) << ")";
	}

	// Saturation instead of overflow
	EXPECT_EQ(std::numeric_limits<P>::max(), exp(std::numeric_limits<P>::max()));
	EXPECT_EQ(std::numeric_limits<P>::max(), exp2(std::numeric_limits<P>::max()));
	if constexpr(std::is_signed_v<typename P::base_type>)
	{
		EXPECT_EQ(P(0), exp(std::numeric_limits<P>::lowest()));
		EXPECT_EQ(P(0), exp2(std::numeric_limits<P>::lowest()));
	}
	// (The approximations are not exact at 0: the polynomials' errors are around 1e-6 (exp) and 1e-7 (exp2))
	EXPECT_TRUE(within(ld(exp(P(0))), 1, 2e-6L + eps<P>));
	EXPECT_TRUE(within(ld(exp2(P(0))), 1, 2e-7L + eps<P>));
	EXPECT_TRUE(within(ld(exp2(P(2))), 4, 8e-7L + eps<P>));
}

TYPED_TEST(math_types, pow)
{
	using P = TypeParam;
	const long double e = eps<P>;

	// Integer exponents
	for(const int n : {-5, -3, -2, -1, 1, 2, 3, 4, 7})
	{
		for(const auto x : values<P>(-20, 20, 500))
		{
			if(x == P(0))
				continue;
			const auto expected = std::pow(ld(x), n);
			if(std::abs(expected) >= max_value<P> * 0.99L)
				continue;
			const auto ax = std::abs(ld(x));
			const auto tolerance = (n > 0)
				? n * e * std::max(1.0L, std::abs(expected) / ax) + e
				: -n * e * (2 + std::abs(expected) * ax) + e;
			ASSERT_TRUE(within(ld(pow(x, n)), expected, tolerance)) << "pow(" << ld(x) << ", " << n << ")";
		}
	}

	// Fractional exponents: exp2(y * log2(x))
	for(const long double y : {-2.5L, -0.5L, 0.25L, 0.5L, 1.5L, 2.75L})
	{
		for(const auto x : raw_values<P>(false, 500))
		{
			const auto expected = std::pow(ld(x), y);
			if(expected >= max_value<P> * 0.99L)
				continue;
			const auto relative = 0.6931L * (std::abs(y) * (1.5e-5L + 4 * poly_eps<P, 1> + 2 * e) + e) + 3e-7L + 4 * poly_eps<P, 1>;
			ASSERT_TRUE(within(ld(pow(x, P(static_cast<double>(y)))), expected, 1.5L * relative * expected + 2 * e)) << "pow(" << ld(x) << ", " << y << ")";
		}
	}

	// Like std::pow
	EXPECT_EQ(P(1), pow(P(0), 0));
	EXPECT_EQ(P(1), pow(P(0), P(0)));
	EXPECT_EQ(P(0), pow(P(0), 3));
	EXPECT_EQ(P(1), pow(P(5), 0));
}

TEST(math_types, constexpr_evaluation)
{
	using P = fpm::fixed_16_16;
	static_assert(sqrt(P(4)) == P(2));
	static_assert(cbrt(P(-27)) == P(-3));
	static_assert(hypot(P(3), P(4)) == P(5));
	static_assert(floor(P(-1.5)) == P(-2));
	static_assert(ceil(P(-1.5)) == P(-1));
	static_assert(round(P(2.5)) == P(3));
	static_assert(nearbyint(P(2.5)) == P(2));
	static_assert(remainder(P(9.5), P(2)) == P(-0.5));
	static_assert(fmod(P(9.5), P(2)) == P(1.5));
	static_assert(pow(P(2), 10) == P(1024));
	static_assert(pow(P(2), -2) == P(0.25));
	static_assert(exp2(P(3)) == P(8));
	static_assert(abs(log2(P(8)) - P(3)) <= P::from_raw_value(1));
	static_assert(sin(P(0)) == P(0));
	static_assert(abs(cos(P(0)) - P(1)) < P(0.001));
	static_assert(abs(exp(P(1)) - P::e()) < P(0.001));
	static_assert(abs(atan2(P(1), P(1)) - P::pi() / 4) < P(0.001));
}

TEST(math_types, unsigned_base_types)
{
	// Operations on unsigned base types: correct for representable (non-negative) results;
	// negation wraps around like for unsigned integers
	using U = fpm::fixed<uint32_t, uint64_t, 16>;
	constexpr long double tolerance = 2e-3L;
	for(const double v : {0.0, 0.25, 0.5, 0.75, 1.0})
	{
		EXPECT_TRUE(within(ld(asin(U(v))), std::asin(static_cast<long double>(v)), tolerance)) << v;
		EXPECT_TRUE(within(ld(acos(U(v))), std::acos(static_cast<long double>(v)), tolerance)) << v;
	}
	for(const double v : {0.0, 0.5, 1.0, 2.0, 100.0, 30000.0})
	{
		EXPECT_EQ(U(v), abs(U(v)));
		EXPECT_EQ(U(v), copysign(U(v), U(1)));
		EXPECT_TRUE(within(ld(atan(U(v))), std::atan(static_cast<long double>(v)), tolerance)) << v;
		EXPECT_TRUE(within(ld(atan2(U(v), U(1))), std::atan2(static_cast<long double>(v), 1.0L), tolerance)) << v;
		EXPECT_TRUE(within(ld(cbrt(U(v))), std::cbrt(static_cast<long double>(v)), eps<U>)) << v;
		EXPECT_TRUE(within(ld(sqrt(U(v))), std::sqrt(static_cast<long double>(v)), eps<U>)) << v;
	}
	EXPECT_EQ(uint32_t{0} - U(1.5).raw_value(), (-U(1.5)).raw_value());
	static_assert(abs(U(3)) == U(3));
	static_assert((-(-U(3))) == U(3));
}
