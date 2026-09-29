#pragma once

#include <fpm/fixed.hpp>
#include <gtest/gtest.h>
#include <charconv>
#include <cmath>
#include <format>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#if __has_include(<stdfloat>)
	#include <stdfloat>
#endif

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

namespace reference_types
{
	/// No type (where the standard library does not provide one)
	struct none
	{
	};

	/// What the tests need of their reference type: the mathematical functions, and the conversions to text
	template<typename T>
	concept complete = requires(const T x, int* const quotient, T* const integral, char* const text)
	{
		std::abs(x); std::floor(x); std::ceil(x); std::trunc(x); std::round(x); std::nearbyint(x); std::rint(x); std::llround(x); std::modf(x, integral);
		std::fmod(x, x); std::remainder(x, x); std::remquo(x, x, quotient); std::nextafter(x, x); std::ldexp(x, 1);
		std::sqrt(x); std::cbrt(x); std::hypot(x, x); std::pow(x, x);
		std::exp(x); std::exp2(x); std::expm1(x); std::log(x); std::log2(x); std::log10(x); std::log1p(x);
		std::sin(x); std::cos(x); std::tan(x); std::asin(x); std::acos(x); std::atan(x); std::atan2(x, x);
		std::to_chars(text, text, x, std::chars_format::hex); std::to_chars(text, text, x, std::chars_format::general, 1); std::format("{}", x);
	};

#ifdef __STDCPP_FLOAT128_T__
	using binary128 = std::float128_t;
#else
	using binary128 = none;
#endif
#ifdef __STDCPP_FLOAT64_T__
	using binary64 = std::float64_t;
#else
	using binary64 = double;
#endif
}

static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53, "double must be IEEE 754 binary64");

/// The floating-point type of the references of the tests (exact values, and the results of the mathematical functions).
/// Of a fixed format: IEEE 754 binary128 where the standard library supports it (its functions too), otherwise binary64.
/// The tests that need more bits than it has are skipped.
using reference_t = std::conditional_t<reference_types::complete<reference_types::binary128>, reference_types::binary128, reference_types::binary64>;
static_assert(reference_types::complete<reference_t>);

/// Whether the reference type represents every number with this many bits (of precision) exactly
inline constexpr bool reference_has(const int bits)
{
	return std::numeric_limits<reference_t>::digits >= bits;
}

/// π, as precise as the reference type
inline constexpr reference_t reference_pi = std::numbers::pi_v<reference_t>;

/// A reference number as text, for the messages of the tests (not every one can be written to a stream)
inline std::string text(const reference_t value)
{
	return std::format("{}", value);
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
