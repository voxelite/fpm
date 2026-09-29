#pragma once

#include <fpm/fixed.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <stdexcept>

namespace fpm
{
	template<typename B, typename I, uint32_t F>
	void PrintTo(const fpm::fixed<B, I, F>& val, ::std::ostream* os)
	{
		auto f = os->flags();
		*os << static_cast<double>(val)
			<< " (0x" << std::hex << std::setw(sizeof(B) * 2) << std::setfill('0') << val.raw_value() << ")";
		os->flags(f);
	}
}

/// The precision of the mathematical functions of the C library for `long double` (like exp, log, pow, remainder), in bits:
/// the one of `double` where they are its functions (Emscripten, although its `long double` has 113 bits)
inline int long_double_functions_digits()
{
	volatile long double one = 1; // (not a constant expression)
	return std::exp(one) == static_cast<long double>(std::exp(1.0)) ? std::numeric_limits<double>::digits : std::numeric_limits<long double>::digits;
}

/// The locale of the environment (""), or "C" where the standard library supports no other (MinGW's libstdc++)
inline std::locale environment_locale()
{
	try
	{
		return std::locale("");
	}
	catch(const std::runtime_error&)
	{
		return std::locale::classic();
	}
}

/// Relative error at most `max_error`, or absolute error at most `max_abs_error`
/// (for results that are too small to be represented with a relative precision)
inline ::testing::AssertionResult HasMaximumError(
	const double value,
	const double reference,
	const double max_error,
	const double max_abs_error = 0
)
{
	const auto diff = std::abs(value - reference);
	if(std::abs(reference) < 1e-10 && diff <= max_error)
		return ::testing::AssertionSuccess();
	if(diff <= max_abs_error)
		return ::testing::AssertionSuccess();
	if(std::abs(diff / reference) <= max_error)
		return ::testing::AssertionSuccess();
	return ::testing::AssertionFailure() << value << " is not within " << (max_error * 100) << "% of " << reference;
}
