#pragma once

#include <fpm/fixed.hpp>
#include <gtest/gtest.h>
#include <iomanip>
#include <ostream>

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
