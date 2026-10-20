// The module `fpm.fraction`: everything of the headers of fpm::fraction (<fpm/fraction.hpp> and <fpm/fraction/...>).
// The functions of fractions that take or give fixed-point numbers need `import fpm.fixed;` as well, to name those types.
// It is compiled with the options of the library (FPM_FRACTION_STRICT, ...), not with the ones of the code that
// imports it.

module;

#include <fpm/fraction.hpp>
#include <fpm/fraction/charconv.hpp>
#include <fpm/fraction/format.hpp>
#include <fpm/fraction/ios.hpp>
#include <fpm/fraction/math.hpp>

export module fpm.fraction;

// The specializations of the standard library are reachable from the code that imports the module where the module
// refers to them: GCC discards the declarations of the global module fragment that the module does not refer to
static_assert(std::formattable<fpm::fraction<uint8_t>, char>);
static_assert(std::numeric_limits<fpm::fraction<uint8_t>>::is_specialized);
static_assert(std::is_default_constructible_v<std::hash<fpm::fraction<uint8_t>>>);

// (A using-declaration exports every overload of its name, so these export the ones of fpm::fixed as well, which are
// of use only with `import fpm.fixed;`)
export namespace fpm
{
	// Types
	using fpm::fraction;
	using fpm::direction;
	using fpm::rotation;

	// Traits
	using fpm::is_fraction;
	using fpm::is_fraction_v;
	using fpm::Fraction;

	// Operators (== and <=> are friends of the types)
	using fpm::operator+;
	using fpm::operator-;
	using fpm::operator*;
	using fpm::operator/;
	using fpm::operator<<;
	using fpm::operator>>;

	// Text
	using fpm::to_chars;
	using fpm::from_chars;
	using fpm::to_string;

	// Angles in turns
	using fpm::sin;
	using fpm::cos;
	using fpm::tan;
	using fpm::asin;
	using fpm::acos;
	using fpm::atan;
	using fpm::atan2;
	using fpm::difference;
}
