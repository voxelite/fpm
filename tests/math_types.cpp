#include "common.hpp"
#include <fpm/fixed/math.hpp>

#include <random>

// Accuracy of the math functions for many fixed-point types, against references of `reference_t` (see common.hpp).
//
// Correctly rounded functions must be within half an epsilon. For approximations, the allowed error is
// a model of the approximation's own error plus rounding errors, based on the measured accuracy.
// These guard against losing precision.

namespace
{
	/// The exact value, from the raw one (where the reference type has as many bits as the type: see below)
	template<typename P>
	reference_t exact(const P x)
	{
		return std::ldexp(static_cast<reference_t>(x.raw_value()), -static_cast<int>(P::fraction_bits));
	}

	template<typename P>
	inline const reference_t eps = exact(std::numeric_limits<P>::epsilon());

	template<typename P>
	inline const reference_t max_value = exact(std::numeric_limits<P>::max());

	/// The resolution of the polynomial evaluations: M = digits - 1 fraction bits (see fpm::detail::poly_bits)
	template<typename P>
	inline const reference_t poly_eps = std::ldexp(reference_t{1}, 1 - std::numeric_limits<std::make_signed_t<typename P::base_type>>::digits);

	/// The error bound of the approximations with results of at most 1 in magnitude (so absolute errors):
	/// half an epsilon for the rounding, 1/8 epsilon for the polynomial's own error (see fpm::detail::target_bits),
	/// plus a few units of the evaluation's precision
	template<typename P>
	inline const reference_t approximation_error = 0.625 * eps<P> + 8 * poly_eps<P>;

	/// Random values in [lo, hi] (quantized to P), evenly spread plus the end points
	template<typename P>
	std::vector<P> values(const reference_t lo, const reference_t hi, const int count = 2000)
	{
		std::mt19937_64 rng(1234 + P::fraction_bits * 7 + sizeof(typename P::base_type));
		std::uniform_real_distribution<double> d(0.0, 1.0);
		std::vector<P> result{P(static_cast<double>(lo)), P(static_cast<double>(hi))};
		for(int i = 0; i < count; ++i)
			result.push_back(P(static_cast<double>(lo + (hi - lo) * static_cast<reference_t>(d(rng)))));
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

	::testing::AssertionResult within(const reference_t got, const reference_t expected, const reference_t tolerance)
	{
		if(std::abs(got - expected) <= tolerance)
			return ::testing::AssertionSuccess();
		return ::testing::AssertionFailure() << text(got) << " differs from " << text(expected)
			<< " by " << text(std::abs(got - expected)) << " > " << text(tolerance);
	}
}

template<typename T>
class math_types : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// The references need as many bits as the type
		if(!reference_has(std::numeric_limits<typename T::base_type>::digits))
			GTEST_SKIP() << "the reference type has fewer bits than the type";
	}
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
	const reference_t tolerance = eps<P> * 0.5 * (1 + 1e-12);
	for(const auto x : raw_values<P>(false))
		ASSERT_TRUE(within(exact(sqrt(x)), std::sqrt(exact(x)), tolerance)) << "x = " << text(exact(x));
	EXPECT_EQ(P(0), sqrt(P(0)));
}

TYPED_TEST(math_types, cbrt)
{
	using P = TypeParam;
	const reference_t tolerance = eps<P> * 0.5 * (1 + 1e-12);
	for(const auto x : raw_values<P>(true))
		ASSERT_TRUE(within(exact(cbrt(x)), std::cbrt(exact(x)), tolerance)) << "x = " << text(exact(x));
	EXPECT_EQ(P(0), cbrt(P(0)));
}

TYPED_TEST(math_types, hypot)
{
	using P = TypeParam;
	const reference_t tolerance = eps<P> * 0.5 * (1 + 1e-12);
	const auto xs = raw_values<P>(true);
	for(std::size_t i = 0; i + 1 < xs.size(); ++i)
	{
		const auto expected = std::hypot(exact(xs[i]), exact(xs[i + 1]));
		const auto got = hypot(xs[i], xs[i + 1]);
		if(expected >= max_value<P>)
			ASSERT_EQ(std::numeric_limits<P>::max(), got); // saturates
		else
			ASSERT_TRUE(within(exact(got), expected, tolerance)) << "x = " << text(exact(xs[i])) << ", y = " << text(exact(xs[i + 1]));
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
		const auto v = exact(x);
		const auto check = [&](const P got, const reference_t expected, const char* name)
		{
			if(expected <= max_value<P> && expected >= exact(std::numeric_limits<P>::lowest()))
				ASSERT_EQ(expected, exact(got)) << name << "(" << text(v) << ")";
		};
		check(floor(x), std::floor(v), "floor");
		check(ceil(x), std::ceil(v), "ceil");
		check(trunc(x), std::trunc(v), "trunc");
		check(round(x), std::round(v), "round");
		check(nearbyint(x), std::nearbyint(v), "nearbyint");
		check(rint(x), std::rint(v), "rint");

		P integral;
		const auto fraction = modf(x, &integral);
		reference_t expected_integral;
		const auto expected_fraction = std::modf(v, &expected_integral);
		ASSERT_EQ(expected_fraction, exact(fraction));
		ASSERT_EQ(expected_integral, exact(integral));
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
		ASSERT_EQ(std::fmod(exact(x), exact(y)), exact(fmod(x, y))) << text(exact(x)) << " fmod " << text(exact(y));
		ASSERT_EQ(std::remainder(exact(x), exact(y)), exact(remainder(x, y))) << text(exact(x)) << " remainder " << text(exact(y));

		int quo = 0;
		int expected_quo = 0;
		const auto rem = remquo(x, y, &quo);
		const auto expected_rem = std::remquo(exact(x), exact(y), &expected_quo);
		ASSERT_EQ(expected_rem, exact(rem));
		ASSERT_EQ(expected_quo % 8, quo % 8) << text(exact(x)) << " remquo " << text(exact(y));
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
	// The range reduction is precise for all arguments
	for(const auto x : raw_values<P>(true))
	{
		ASSERT_TRUE(within(exact(sin(x)), std::sin(exact(x)), approximation_error<P>)) << "sin(" << text(exact(x)) << ")";
		ASSERT_TRUE(within(exact(cos(x)), std::cos(exact(x)), approximation_error<P>)) << "cos(" << text(exact(x)) << ")";
	}
	for(const auto x : values<P>(-1.5, 1.5))
	{
		// The evaluation's errors are amplified by the derivative, 1 + tan^2
		const auto t = std::tan(exact(x));
		if(std::abs(t) >= max_value<P>)
			continue;
		ASSERT_TRUE(within(exact(tan(x)), t, 0.625 * eps<P> + 8 * poly_eps<P> * (1 + t * t))) << "tan(" << text(exact(x)) << ")";
	}
	EXPECT_EQ(P(0), sin(P(0)));
	EXPECT_EQ(P(1), cos(P(0)));
	EXPECT_EQ(P(0), tan(P(0)));
}

// The values closest to π/2: the cosine is below the precision of the evaluation (it used to be a division by zero)
TEST(math, tan_at_pole)
{
	using P = fpm::fixed<int32_t, int64_t, 30>;
	const auto max = std::numeric_limits<P>::max();
	EXPECT_EQ(max, tan(P::from_raw_value(1686629712)));
	EXPECT_EQ(max, tan(P::from_raw_value(1686629713))); // π/2 - 6e-11
	EXPECT_EQ(-max, tan(P::from_raw_value(1686629714)));
	EXPECT_EQ(-max, tan(P::from_raw_value(-1686629713)));

	// Around every pole that a type can reach: saturated, with the sign of the tangent
	const auto check = []<typename Q>()
	{
		using B = typename Q::base_type;
		if(!reference_has(std::numeric_limits<B>::digits))
			return; // (the reference type has fewer bits than the type)
		const reference_t half_pi = reference_pi / 2;
		const reference_t largest = exact(std::numeric_limits<Q>::max());
		for(int pole = -41; pole <= 41; pole += 2)
		{
			const reference_t position = std::ldexp(pole * half_pi, static_cast<int>(Q::fraction_bits));
			if(std::abs(position) >= static_cast<reference_t>(std::numeric_limits<B>::max()) - 8)
				continue;
			for(int offset = -3; offset <= 3; ++offset)
			{
				const auto x = Q::from_raw_value(static_cast<B>(static_cast<B>(std::floor(position)) + offset));
				const reference_t expected = std::tan(exact(x));
				const reference_t result = exact(tan(x));
				if(std::abs(expected) >= largest)
					ASSERT_TRUE(std::abs(result) >= largest - eps<Q> && (result < 0) == (expected < 0)) << "tan(" << text(exact(x)) << ") is " << text(result);
			}
		}
	};
	check.template operator()<fpm::fixed_4_4>();
	check.template operator()<fpm::fixed_8_8>();
	check.template operator()<fpm::fixed_16_16>();
	check.template operator()<fpm::fixed_8_24>();
	check.template operator()<fpm::fixed<int32_t, int64_t, 28>>();
	check.template operator()<fpm::fixed<int32_t, int64_t, 29>>();
	check.template operator()<P>();
#ifdef FPM_INT128
	check.template operator()<fpm::fixed_32_32>();
	check.template operator()<fpm::fixed_8_56>();
	check.template operator()<fpm::fixed<int64_t, FPM_INT128, 62>>();
#endif
}

TYPED_TEST(math_types, inverse_trigonometry)
{
	using P = TypeParam;
	const auto tolerance = approximation_error<P>;
	for(const auto x : raw_values<P>(true))
		ASSERT_TRUE(within(exact(atan(x)), std::atan(exact(x)), tolerance)) << "atan(" << text(exact(x)) << ")";
	for(const auto x : values<P>(-1, 1))
	{
		ASSERT_TRUE(within(exact(asin(x)), std::asin(exact(x)), tolerance)) << "asin(" << text(exact(x)) << ")";
		ASSERT_TRUE(within(exact(acos(x)), std::acos(exact(x)), tolerance)) << "acos(" << text(exact(x)) << ")";
	}
	const auto xs = raw_values<P>(true);
	for(std::size_t i = 0; i + 1 < xs.size(); ++i)
	{
		const auto y = xs[i];
		const auto x = xs[i + 1];
		ASSERT_TRUE(within(exact(atan2(y, x)), std::atan2(exact(y), exact(x)), tolerance)) << "atan2(" << text(exact(y)) << ", " << text(exact(x)) << ")";
	}
	EXPECT_EQ(P(0), atan(P(0)));
	EXPECT_EQ(P(0), asin(P(0)));
}

TYPED_TEST(math_types, logarithms)
{
	using P = TypeParam;
	const auto tolerance = approximation_error<P>;
	for(const auto x : raw_values<P>(false))
	{
		const auto expected = std::log2(exact(x));
		if(std::abs(expected) >= max_value<P>)
			continue;
		ASSERT_TRUE(within(exact(log2(x)), expected, tolerance)) << "log2(" << text(exact(x)) << ")";
		ASSERT_TRUE(within(exact(log(x)), std::log(exact(x)), tolerance)) << "log(" << text(exact(x)) << ")";
		ASSERT_TRUE(within(exact(log10(x)), std::log10(exact(x)), tolerance)) << "log10(" << text(exact(x)) << ")";
	}
	// Exact for powers of two
	EXPECT_EQ(P(0), log2(P(1)));
	EXPECT_EQ(P(3), log2(P(8)));
	EXPECT_EQ(P(-2), log2(P(0.25)));
	EXPECT_EQ(P(0), log(P(1)));
}

TYPED_TEST(math_types, exponentials)
{
	using P = TypeParam;
	const reference_t max_log2 = std::log2(max_value<P>);
	const reference_t lowest = exact(std::numeric_limits<P>::lowest());

	// The results have up to all the bits of the type, but the evaluation has M fraction bits: a relative error of a few 2^-M
	const auto tolerance = [](const reference_t expected) { return 0.5 * eps<P> + 8 * poly_eps<P> * expected; };
	for(const auto x : values<P>(std::max(lowest, -max_log2 - 70), max_log2 - 0.01))
	{
		const auto expected = std::exp2(exact(x));
		ASSERT_TRUE(within(exact(exp2(x)), expected, tolerance(expected))) << "exp2(" << text(exact(x)) << ")";
	}

	const reference_t max_log = std::log(max_value<P>);
	for(const auto x : values<P>(std::max(lowest, -max_log - 50), max_log - 0.01))
	{
		const auto expected = std::exp(exact(x));
		ASSERT_TRUE(within(exact(exp(x)), expected, tolerance(expected))) << "exp(" << text(exact(x)) << ")";
	}

	// Saturation instead of overflow
	EXPECT_EQ(std::numeric_limits<P>::max(), exp(std::numeric_limits<P>::max()));
	EXPECT_EQ(std::numeric_limits<P>::max(), exp2(std::numeric_limits<P>::max()));
	if constexpr(std::is_signed_v<typename P::base_type>)
	{
		EXPECT_EQ(P(0), exp(std::numeric_limits<P>::lowest()));
		EXPECT_EQ(P(0), exp2(std::numeric_limits<P>::lowest()));
	}
	// Exact for integers (exp2) and 0
	EXPECT_EQ(P(1), exp(P(0)));
	EXPECT_EQ(P(1), exp2(P(0)));
	EXPECT_EQ(P(4), exp2(P(2)));
	EXPECT_EQ(P(0.25), exp2(P(-2)));
}

TYPED_TEST(math_types, pow)
{
	using P = TypeParam;
	const reference_t e = eps<P>;

	// Integer exponents
	for(const int n : {-5, -3, -2, -1, 1, 2, 3, 4, 7})
	{
		for(const auto x : values<P>(-20, 20, 500))
		{
			if(x == P(0))
				continue;
			const auto expected = std::pow(exact(x), static_cast<reference_t>(n));
			if(std::abs(expected) >= max_value<P> * 0.99)
				continue;
			const auto ax = std::abs(exact(x));
			const auto tolerance = (n > 0)
				? n * e * std::max(reference_t{1}, std::abs(expected) / ax) + e
				: -n * e * (2 + std::abs(expected) * ax) + e;
			ASSERT_TRUE(within(exact(pow(x, n)), expected, tolerance)) << "pow(" << text(exact(x)) << ", " << n << ")";
		}
	}

	// Fractional exponents: exp2(y * log2(x)), with the product calculated exactly from log2(x) with M fraction bits,
	// so the relative error is a few 2^-M times the exponent
	for(const double y : {-2.5, -0.5, 0.25, 0.5, 1.5, 2.75})
	{
		for(const auto x : raw_values<P>(false, 500))
		{
			const auto expected = std::pow(exact(x), static_cast<reference_t>(y));
			if(expected >= max_value<P> * 0.99)
				continue;
			const auto tolerance = 0.5 * e + 8 * (1 + std::abs(y)) * poly_eps<P> * expected;
			ASSERT_TRUE(within(exact(pow(x, P(y))), expected, tolerance)) << "pow(" << text(exact(x)) << ", " << y << ")";
		}
	}

	// Like std::pow
	EXPECT_EQ(P(1), pow(P(0), 0));
	EXPECT_EQ(P(1), pow(P(0), P(0)));
	EXPECT_EQ(P(0), pow(P(0), 3));
	EXPECT_EQ(P(1), pow(P(5), 0));
	EXPECT_EQ(P(2), pow(P(4), P(0.5)));
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
	const reference_t tolerance = 2e-3;
	for(const double v : {0.0, 0.25, 0.5, 0.75, 1.0})
	{
		EXPECT_TRUE(within(exact(asin(U(v))), std::asin(static_cast<reference_t>(v)), tolerance)) << v;
		EXPECT_TRUE(within(exact(acos(U(v))), std::acos(static_cast<reference_t>(v)), tolerance)) << v;
	}
	for(const double v : {0.0, 0.5, 1.0, 2.0, 100.0, 30000.0})
	{
		EXPECT_EQ(U(v), abs(U(v)));
		EXPECT_EQ(U(v), copysign(U(v), U(1)));
		EXPECT_TRUE(within(exact(atan(U(v))), std::atan(static_cast<reference_t>(v)), tolerance)) << v;
		EXPECT_TRUE(within(exact(atan2(U(v), U(1))), std::atan2(static_cast<reference_t>(v), reference_t{1}), tolerance)) << v;
		EXPECT_TRUE(within(exact(cbrt(U(v))), std::cbrt(static_cast<reference_t>(v)), eps<U>)) << v;
		EXPECT_TRUE(within(exact(sqrt(U(v))), std::sqrt(static_cast<reference_t>(v)), eps<U>)) << v;
	}
	static_assert(abs(U(3)) == U(3));
#ifndef FPM_CHECK_OVERFLOW
	// The negative of an unsigned number wraps around
	EXPECT_EQ(uint32_t{0} - U(1.5).raw_value(), (-U(1.5)).raw_value());
	static_assert((-(-U(3))) == U(3));
#endif
}
