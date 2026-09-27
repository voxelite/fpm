#include "common.hpp"
#include <fpm/math.hpp>

TEST(power, exp)
{
	// For several values, verify that fpm::exp is close to std::exp.
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.02;

	for(double value = -5; value <= 5; value += 0.1)
	{
		const auto exp_real = std::exp(value);
		const auto exp_fixed = static_cast<double>(exp(P(value)));
		EXPECT_TRUE(HasMaximumError(exp_fixed, exp_real, MAX_ERROR_PERC));
	}
}

TEST(power, exp2)
{
	// For several values, verify that fpm::exp2 is close to std::exp2.
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.01;

	for(double value = -5; value <= 5; value += 0.1)
	{
		const auto exp_real = std::exp2(value);
		const auto exp_fixed = static_cast<double>(exp2(P(value)));
		EXPECT_TRUE(HasMaximumError(exp_fixed, exp_real, MAX_ERROR_PERC));
	}
}

TEST(power, expm1)
{
	// For several values, verify that fpm::expm1 is close to std::expm1.
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.02;

	for(double value = -5; value <= 5; value += 0.1)
	{
		const auto exp_real = std::expm1(value);
		const auto exp_fixed = static_cast<double>(expm1(P(value)));
		EXPECT_TRUE(HasMaximumError(exp_fixed, exp_real, MAX_ERROR_PERC));
	}
}

TEST(power, log)
{
	// For several values, verify that fpm::log is close to std::log.
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.01;

	// Small numbers
	for(double value = 0.1; value <= 10; value += 0.1)
	{
		const auto log_real = std::log(value);
		const auto log_fixed = static_cast<double>(log(P(value)));
		EXPECT_TRUE(HasMaximumError(log_fixed, log_real, MAX_ERROR_PERC));
	}

	// Larger numbers, step by PI to get an irregular pattern
	for(double value = 1; value <= 1000; value += 3.141593)
	{
		const auto log_real = std::log(value);
		const auto log_fixed = static_cast<double>(log(P(value)));
		EXPECT_TRUE(HasMaximumError(log_fixed, log_real, MAX_ERROR_PERC));
	}

#ifndef NDEBUG
	EXPECT_DEATH(auto v = log(P(0)), "");
	EXPECT_DEATH(auto v = log(P(-1)), "");
#endif
}

TEST(power, log2)
{
	// For several values, verify that fpm::log2 is close to std::log2.
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.01;

	// Small numbers
	for(double value = 0.1; value <= 10; value += 0.1)
	{
		const auto log_real = std::log2(value);
		const auto log_fixed = static_cast<double>(log2(P(value)));
		EXPECT_TRUE(HasMaximumError(log_fixed, log_real, MAX_ERROR_PERC));
	}

	// Larger numbers, step by PI to get an irregular pattern
	for(double value = 1; value <= 1000; value += 3.141593)
	{
		const auto log_real = std::log2(value);
		const auto log_fixed = static_cast<double>(log2(P(value)));
		EXPECT_TRUE(HasMaximumError(log_fixed, log_real, MAX_ERROR_PERC));
	}

#ifndef NDEBUG
	EXPECT_DEATH(auto v = log2(P(0)), "");
	EXPECT_DEATH(auto v = log2(P(-1)), "");
#endif
}

TEST(power, log10)
{
	// For several values, verify that fpm::log10 is close to std::log10exp.
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.01;

	// Small numbers
	for(double value = 0.1; value <= 10; value += 0.1)
	{
		const auto log_real = std::log10(value);
		const auto log_fixed = static_cast<double>(log10(P(value)));
		EXPECT_TRUE(HasMaximumError(log_fixed, log_real, MAX_ERROR_PERC));
	}

	// Larger numbers, step by PI to get an irregular pattern
	for(double value = 1; value <= 1000; value += 3.141593)
	{
		const auto log_real = std::log10(value);
		const auto log_fixed = static_cast<double>(log10(P(value)));
		EXPECT_TRUE(HasMaximumError(log_fixed, log_real, MAX_ERROR_PERC));
	}

#ifndef NDEBUG
	EXPECT_DEATH(auto v = log10(P(0)), "");
	EXPECT_DEATH(auto v = log10(P(-1)), "");
#endif
}

TEST(power, log1p)
{
	// For several values, verify that fpm::log1p is close to std::log1p.
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.01;

	// Small numbers
	for(double value = -0.9; value <= 10; value += 0.1)
	{
		const auto log_real = std::log1p(value);
		const auto log_fixed = static_cast<double>(log1p(P(value)));
		EXPECT_TRUE(HasMaximumError(log_fixed, log_real, MAX_ERROR_PERC));
	}

	// Larger numbers, step by PI to get an irregular pattern
	for(double value = 0; value <= 1000; value += 3.141593)
	{
		const auto log_real = std::log1p(value);
		const auto log_fixed = static_cast<double>(log1p(P(value)));
		EXPECT_TRUE(HasMaximumError(log_fixed, log_real, MAX_ERROR_PERC));
	}

#ifndef NDEBUG
	EXPECT_DEATH(auto v = log1p(P(-1)), "");
	EXPECT_DEATH(auto v = log1p(P(-2)), "");
#endif
}

TEST(power, pow)
{
	// For several combinations of x and y, verify that fpm::pow is close to std::pow.
	using P = fpm::fixed<int32_t, int64_t, 12>;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.11;

	// Small numbers
	for(double base = 0.125; base <= 1; base += 0.125)
	{
		for(double exp = -1; exp <= 1; exp += 0.01)
		{
			const auto pow_real = std::pow(base, exp);
			const auto pow_fixed = static_cast<double>(pow(P(base), P(exp)));
			EXPECT_TRUE(HasMaximumError(pow_fixed, pow_real, MAX_ERROR_PERC));
		}
	}

	// Larger numbers, step by PI to get an irregular pattern
	for(double base = 1; base <= 40; base += 3.141593)
	{
		assert(P(base) != P(0));
		for(double exp = -2; exp <= 2; exp += 0.1)
		{
			const auto pow_real = std::pow(base, exp);
			const auto pow_fixed = static_cast<double>(pow(P(base), P(exp)));
			EXPECT_TRUE(HasMaximumError(pow_fixed, pow_real, MAX_ERROR_PERC));
		}
	}

	// Negative numbers
	for(double base = -20; base < 0; base += 1.0/8)
	{
		// Whole exponents only
		for(double exp = -3; exp < 0; exp++)
		{
			const auto pow_real = std::pow(base, exp);
			const auto pow_fixed = static_cast<double>(pow(P(base), P(exp)));
			// Results can be smaller than the resolution of the type: allow an error of one epsilon
			EXPECT_TRUE(HasMaximumError(pow_fixed, pow_real, MAX_ERROR_PERC, static_cast<double>(std::numeric_limits<P>::epsilon())));
		}
	}

	// Edge cases
	EXPECT_EQ(P(1), pow(P(1), P(0)));
	EXPECT_EQ(P(0), pow(P(0), P(1)));
	EXPECT_EQ(P(1), pow(P(1), P(1)));
	// Like std::pow: x^0 == 1 for any x, including 0
	EXPECT_EQ(P(1), pow(P(0), P(0)));
	EXPECT_EQ(P(1), pow(P(-3), P(0)));
#ifndef NDEBUG
	EXPECT_DEATH(auto v = pow(P(0), P(-1)), "");
#endif
}

TEST(power, pow_int)
{
	// For several combinations of x and y, verify that fpm::pow is close to std::pow.
	using P = fpm::fixed_16_16;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.01;

	// Small numbers
	for(double base = -1; base <= 1; base += 1.0/8)
	{
		if(base != 0)
		{
			for(int exp = -4; exp <= 4; exp++)
			{
				const auto pow_real = std::pow(base, exp);
				const auto pow_fixed = static_cast<double>(pow(P(base), exp));
				EXPECT_TRUE(HasMaximumError(pow_fixed, pow_real, MAX_ERROR_PERC));
			}
		}
	}

	// Larger numbers, step by PI to get an irregular pattern
	for(double base = -40; base <= 40; base += 3.141593)
	{
		assert(P(base) != P(0));
		for(int exp = -2; exp <= 2; exp++)
		{
			const auto pow_real = std::pow(base, exp);
			const auto pow_fixed = static_cast<double>(pow(P(base), exp));
			EXPECT_TRUE(HasMaximumError(pow_fixed, pow_real, MAX_ERROR_PERC));
		}
	}

	// Edge cases
	EXPECT_EQ(P(1), pow(P(1), 0));
	EXPECT_EQ(P(0), pow(P(0), 1));
	EXPECT_EQ(P(1), pow(P(1), 1));
	// Like std::pow: x^0 == 1 for any x, including 0
	EXPECT_EQ(P(1), pow(P(0), 0));
	EXPECT_EQ(P(1), pow(P(-3), 0));
#ifndef NDEBUG
	EXPECT_DEATH(auto v = pow(P(0), -1), "");
#endif
}

TEST(power, sqrt)
{
	// For several values, verify that fpm::sqrt is close to std::sqrt.
	using P = fpm::fixed_16_16;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.0003;

	// Small numbers
	for(double value = 0; value <= 100; value += 0.01)
	{
		const auto sqrt_real = std::sqrt(value);
		const auto sqrt_fixed = static_cast<double>(sqrt(P(value)));
		EXPECT_TRUE(HasMaximumError(sqrt_fixed, sqrt_real, MAX_ERROR_PERC));
	}

	// Larger numbers, step by PI/10 to get an irregular pattern
	for(double value = 0; value <= 10000; value += 0.3141593)
	{
		const auto sqrt_real = std::sqrt(value);
		const auto sqrt_fixed = static_cast<double>(sqrt(P(value)));
		EXPECT_TRUE(HasMaximumError(sqrt_fixed, sqrt_real, MAX_ERROR_PERC));
	}

#ifndef NDEBUG
	EXPECT_DEATH(auto v = sqrt(P(-1)), "");
#endif
}

TEST(power, sqrt_24)
{
	// High-precision test of sqrt
	using P = fpm::fixed_8_24;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.0000005;

	// Small numbers
	for(double value = 0; value <= 100; value += 0.01)
	{
		const auto sqrt_real = std::sqrt(value);
		const auto sqrt_fixed = static_cast<double>(sqrt(P(value)));
		EXPECT_TRUE(HasMaximumError(sqrt_fixed, sqrt_real, MAX_ERROR_PERC));
	}

}

TEST(power, cbrt)
{
	// For several values, verify that fpm::cbrt is close to std::cbrt.
	using P = fpm::fixed_16_16;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.00005;

	// Small numbers
	for(double value = -100; value <= 100; value += 0.125)
	{
		const auto cbrt_real = std::cbrt(value);
		const auto cbrt_fixed = static_cast<double>(cbrt(P(value)));
		EXPECT_TRUE(HasMaximumError(cbrt_fixed, cbrt_real, MAX_ERROR_PERC));
	}

	// Larger numbers, step by PI*10 to get an irregular pattern
	for(double value = -10000; value <= 10000; value += 31.41593)
	{
		const auto cbrt_real = std::cbrt(value);
		const auto cbrt_fixed = static_cast<double>(cbrt(P(value)));
		EXPECT_TRUE(HasMaximumError(cbrt_fixed, cbrt_real, MAX_ERROR_PERC));
	}
}

TEST(power, cbrt_24)
{
	// High-precision test of cbrt
	using P = fpm::fixed_8_24;

	// Maximum relative error (percentage) we allow
	constexpr auto MAX_ERROR_PERC = 0.0000005;

	// Small numbers
	for(double value = -100; value <= 100; value += 0.125)
	{
		const auto cbrt_real = std::cbrt(value);
		const auto cbrt_fixed = static_cast<double>(cbrt(P(value)));
		EXPECT_TRUE(HasMaximumError(cbrt_fixed, cbrt_real, MAX_ERROR_PERC));
	}
}
