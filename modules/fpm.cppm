// The module `fpm`: everything of the headers, for `import fpm;`.
// It is compiled with the options of the library (FPM_INT128, FPM_DEFINED_OVERFLOW, ... and NDEBUG for the assertions
// of FPM_CHECK_OVERFLOW), not with the ones of the code that imports it. Macros are not exported: FPM_INT128 is not
// visible to that code, `fpm::int128_t` is exported where the module has it.

module;

#include <fpm/fixed.hpp>
#include <fpm/fixed/charconv.hpp>
#include <fpm/fixed/format.hpp>
#include <fpm/fixed/ios.hpp>
#include <fpm/fixed/math.hpp>

#include <fpm/fraction.hpp>
#include <fpm/fraction/charconv.hpp>
#include <fpm/fraction/format.hpp>
#include <fpm/fraction/ios.hpp>
#include <fpm/fraction/math.hpp>

export module fpm;

// The specializations of the standard library are reachable from the code that imports the module where the module
// refers to them: GCC discards the declarations of the global module fragment that the module does not refer to
static_assert(std::formattable<fpm::fixed_16_16, char> && std::formattable<fpm::fraction<uint8_t>, char>);
static_assert(std::numeric_limits<fpm::fixed_16_16>::is_specialized && std::numeric_limits<fpm::fraction<uint8_t>>::is_specialized);
static_assert(std::is_default_constructible_v<std::hash<fpm::fixed_16_16>> && std::is_default_constructible_v<std::hash<fpm::fraction<uint8_t>>>);

export namespace fpm
{
	// Types
	using fpm::fixed;
	using fpm::fraction;
	using fpm::direction;
	using fpm::rotation;
#ifdef FPM_INT128
	using fpm::int128_t;
#endif

	using fpm::fixed_4_4;
	using fpm::fixed_8_8;
	using fpm::fixed_8_24;
	using fpm::fixed_16_16;
	using fpm::fixed_24_8;
#ifdef FPM_INT128
	using fpm::fixed_56_8;
	using fpm::fixed_48_16;
	using fpm::fixed_40_24;
	using fpm::fixed_32_32;
	using fpm::fixed_24_40;
	using fpm::fixed_16_48;
	using fpm::fixed_8_56;
#endif

	// Traits
	using fpm::is_fixed;
	using fpm::is_fixed_v;
	using fpm::Fixed;
	using fpm::is_fraction;
	using fpm::is_fraction_v;
	using fpm::Fraction;

	// Operators
	using fpm::operator+;
	using fpm::operator-;
	using fpm::operator*;
	using fpm::operator/;
	using fpm::operator==;
	using fpm::operator<=>;
	using fpm::operator<<;
	using fpm::operator>>;

	// Text
	using fpm::to_chars;
	using fpm::from_chars;
	using fpm::to_string;

	// Mathematical functions
	using fpm::abs;
	using fpm::fmod;
	using fpm::remainder;
	using fpm::remquo;
	using fpm::copysign;
	using fpm::nextafter;
	using fpm::nexttoward;
	using fpm::modf;
	using fpm::ceil;
	using fpm::floor;
	using fpm::trunc;
	using fpm::round;
	using fpm::nearbyint;
	using fpm::rint;
	using fpm::fpclassify;
	using fpm::isfinite;
	using fpm::isinf;
	using fpm::isnan;
	using fpm::isnormal;
	using fpm::signbit;
	using fpm::isgreater;
	using fpm::isgreaterequal;
	using fpm::isless;
	using fpm::islessequal;
	using fpm::islessgreater;
	using fpm::isunordered;
	using fpm::exp;
	using fpm::exp2;
	using fpm::expm1;
	using fpm::log;
	using fpm::log2;
	using fpm::log10;
	using fpm::log1p;
	using fpm::pow;
	using fpm::sqrt;
	using fpm::cbrt;
	using fpm::hypot;
	using fpm::sin;
	using fpm::cos;
	using fpm::tan;
	using fpm::asin;
	using fpm::acos;
	using fpm::atan;
	using fpm::atan2;
	using fpm::difference;
}
