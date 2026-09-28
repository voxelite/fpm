---
layout: default
---
# fpm
A C++ header-only fixed-point math library. "fpm" stands for "fixed-point math".

It is designed to serve as a drop-in replacement for floating-point types and aims to provide as much of the standard library's functionality as possible with exclusively integers. `fpm` requires C++26.

## Headers
Every type has its own header, and a directory of that name with the headers for what can be added to it:

| | `fpm::fixed` | `fpm::fraction` |
|---|---|---|
| The type | `<fpm/fixed.hpp>` | `<fpm/fraction.hpp>` |
| Mathematical functions | `<fpm/fixed/math.hpp>` | `<fpm/fraction/math.hpp>` (trigonometry) |
| `to_chars`, `from_chars`, `to_string` | `<fpm/fixed/charconv.hpp>` | `<fpm/fraction/charconv.hpp>` |
| `std::format` | `<fpm/fixed/format.hpp>` | `<fpm/fraction/format.hpp>` |
| Stream operators | `<fpm/fixed/ios.hpp>` | `<fpm/fraction/ios.hpp>` |

## Usage
`fpm` defines the `fpm::fixed` class, which is templated on the underlying integer type and the number of bits in the fraction:
```c++
namespace fpm
{
	template<typename BaseType, typename IntermediateType, uint32_t FractionBits, bool EnableRounding = true>
	struct fixed;
}
```
The fraction must leave at least one bit (besides the sign bit) for the integral part, so every type can represent 1:
a 32-bit signed type has at most 30 fraction bits, a 32-bit unsigned type at most 31.

**Note:** It's recommended to use a *signed* integer type for `BaseType` (and `IntermediateType`) to emulate floating-point numbers
and to allow the compiler to optimize the computations, since overflow and underflow are undefined
for signed integer types.

To use this class, simply include its header:
```c++
#include <fpm/fixed.hpp>
```
You may wish to typedef a particular choice of underlying type, intermediate type and fraction bitcount, e.g.:
```c++
using position = fpm::fixed<std::int32_t, std::int64_t, 16>;
```
This defines a signed 16.16 fixed-point number with a range of -32768 to 32767.999985... and a resolution of 0.0000153... It uses 64-bit integers as intermediate type during calculations to avoid loss of information.

For your convenience, several popular fixed-point formats have been defined in the `fpm` namespace:
```c++
namespace fpm
{
	using fixed_16_16 = fixed<std::int32_t, std::int64_t, 16>;  // Q16.16 format
	using fixed_24_8  = fixed<std::int32_t, std::int64_t, 8>;   // Q24.8 format
	using fixed_8_24  = fixed<std::int32_t, std::int64_t, 24>;  // Q8.24 format

	// 64-bit base types, with a 128-bit intermediate type (FPM_INT128)
	using fixed_32_32 = fixed<std::int64_t, FPM_INT128, 32>;     // and fixed_56_8 ... fixed_8_56
}
```
The 64-bit types use `__int128` (GCC, Clang) or `std::_Signed128` (MSVC and clang-cl) as intermediate type. Define `FPM_INT128` to use another
128-bit type, or `FPM_NO_INT128` to not use 128-bit integers at all. Without a 128-bit type, as on most 32-bit targets, the 64-bit types are not available.

## Fractions
A `fpm::fixed` type cannot use all its bits for the fraction. For values in [0, 1) that do, the header `<fpm/fraction.hpp>`
provides `fpm::fraction`, templated on the unsigned integer type that stores it:
```c++
#include <fpm/fraction.hpp>

using angle = fpm::fraction<std::uint16_t>; // a part of a full turn, in steps of 1/65536

angle a { 0.75 };
a += angle { 0.5 };                         // wraps around: 0.25
auto radians = fpm::fixed_16_16(a) * fpm::fixed_16_16::two_pi();
```
It is meant to store such values, not to calculate with them.
* A fraction wraps around (modulo 1) instead of overflowing, for all its operations: `-angle { 0.25 }` is 0.75.
* It supports `+`, `-` and negation, `*` and `/` by an integer, and comparisons (of the values in [0, 1)).
* It converts explicitly to and from floating-point types, `fpm::fixed` types and fractions with another number of bits.
* For anything else, convert it to a `fpm::fixed` type (with parentheses, as in the example).
* It has specializations of `std::hash` and `std::numeric_limits`, and an order (of the values in [0, 1)) for `std::less`, `std::set` and the like.
  `is_modulo` is true, as it wraps around; `digits10` are the decimal places that are read and written without change.

### Conversions to a fraction
The conversion of a number to a fraction is modulo 1 as well, from any type and from text:
* It gives the fraction of the number: 1.75 and -0.25 both give 0.75.
  So an angle can be moved by any number: `a += angle(velocity * delta)` works for a negative velocity, and for more than a turn.
* It is rounded to the nearest fraction. A number that rounds up to 1 gives 0, like 0.9999 for 8 bits: the error is half a unit at most, for every number.
* The conversion from a fraction with fewer bits is exact.

Define `FPM_FRACTION_STRICT` (or set the CMake option of that name) to check the numbers that come from a floating-point number or from text:
a number that is not in [0, 1) is an error then, instead of the fraction of it.
`from_chars` reports `std::errc::result_out_of_range` for it, streams set their `failbit`, and the conversion from a floating-point number asserts.
Conversions from `fpm::fixed` types and between fractions are not checked, and neither are numbers in [0, 1) that round up to 1.
The option changes what the functions of the library do, so define it for all of a program.

### Difference
A fraction has no direction: `a - b` is the rotation from `b` to `a` counterclockwise, which can be almost a full turn.
`fpm::difference(a, b)` gives the shortest rotation from `b` to `a`, as a distance of at most half a turn and its direction:
```c++
const auto [distance, way] = fpm::difference(target, current);
const auto step = std::min(distance, limit);
current = (way == fpm::direction::Clockwise) ? current - step : current + step;
```
* `fpm::direction` is `CounterClockwise` (the angle increases) or `Clockwise`. For angles that are half a turn apart it is `CounterClockwise`.
* The result converts explicitly to a `fpm::fixed` type: a number in [-1/2, 1/2], negative for clockwise.

### Angles
The header `<fpm/fraction/math.hpp>` provides the trigonometry for angles that are stored as a fraction of a turn (so 1/4 is a right angle).
The functions have the names of the ones for fixed-point numbers, which use radians. The type of the result is their first template argument:
```c++
#include <fpm/fraction/math.hpp>

using angle = fpm::fraction<std::uint16_t>;
using number = fpm::fixed_16_16;

angle rotation { 0.125 };                          // 45 degrees
number x = fpm::cos<number>(rotation);             // the forward vector: 0.7071, 0.7071
number y = fpm::sin<number>(rotation);
angle back = fpm::atan2<angle>(y, x);              // 0.125
```
* `sin`, `cos` and `tan` take an angle and give a fixed-point number. They are as precise as the ones for that fixed-point type,
  and `sin` and `cos` are faster: the two highest bits of the angle are its quadrant, which takes a multiplication for radians.
  `tan` saturates where the result is too large, also for 1/4 and 3/4 of a turn (where it gives the negative of the maximum).
* `asin`, `acos`, `atan` and `atan2` take fixed-point numbers and give an angle. Negative angles wrap around: `asin` of -0.5 is -1/12 of a turn, which is 0.9167.
  `atan2` gives the angle of a vector over the whole turn, and 0 for the zero vector.
* The axes and the diagonals are exact: `sin` of 1/4 is 1, and `atan2` of (1, 1) is 1/8.
* The angles are within about 0.52 units of the fraction, if it has at least 4 bits fewer than the base type of the numbers: a 16-bit fraction for 32-bit numbers.
  (Numbers with a narrower base type are calculated with 32 bits.) With more bits the angle is as precise as that calculation,
  so a 64-bit fraction from 32-bit numbers has about 30 bits that are right.
* An angle with more bits than the calculation of the result uses is rounded first: this is below the precision of the result.

### Text
Fractions have their own conversions to and from text, which behave like the ones for `fpm::fixed`:
`fpm::to_chars`, `fpm::from_chars` and `fpm::to_string` in `<fpm/fraction/charconv.hpp>`, `std::format` support in `<fpm/fraction/format.hpp>`,
and the stream operators in `<fpm/fraction/ios.hpp>`.
* Text is converted like any other number: "1.75" and "-0.25" give 0.75, and "0.9999" gives 0 for 8 bits. See [the conversions](#conversions-to-a-fraction),
  also for the option to check the numbers instead. "inf" and "nan" are not numbers: they are always an error.
* Digits are rounded when a precision is given, so a fraction close to 1 can be printed as `1.00`.
  Without a precision, the text is the shortest that reads back as the same fraction.

## Mathematical functions
FPM offers the header `<fpm/fixed/math.hpp>` with mathematical functions that operate on its fixed-point types, similar to `<math.hpp>` for floating-point types.
The available functions for fixed-point types include:
* basic functions: `abs`, `fmod`, `remainder`, `copysign`, `remquo`, etc.
* trigonometry functions: `sin`, `cos`, `tan`, `asin`, `acos`, `atan` and `atan2`.
* exponential functions: `exp`, `exp2`, `expm1`, `log`, `log10`, `log2` and `log1p`.
* power functions: `pow`, `sqrt`, `cbrt` and `hypot`.
* classification functions: `fpclassify`, `isnormal`, `isnan`, `isnormal`, etc.

Notes:
* all functions are in the `fpm` namespace, and all are `constexpr`.
* certain functions will always return the same value (e.g. `isnan` and `isinf` will always return false).
* `sqrt`, `cbrt` and `hypot` are correctly rounded; `floor`, `ceil`, `trunc`, `round`, `nearbyint`, `rint`, `modf`, `fmod`, `remainder` and `remquo` are exact.
  Like their standard counterparts, `remainder` and `remquo` round the quotient to nearest (ties to even), `pow(x, 0)` is 1 for any `x` (including 0),
  and `atan2(0, 0)` is 0.
* the other functions are approximations with minimax polynomials, whose degree is chosen at compile time for the type's fraction bits:
  more fraction bits mean a longer polynomial (more precise, slower and larger), fewer mean a shorter one.
  The polynomials are evaluated with all the bits of the base type (e.g. 30 fraction bits for 32-bit types), with the products in the intermediate type:
  * `sin`, `cos`, `atan`, `asin`, `acos`, `atan2`, `log`, `log2` and `log10` are within 0.625 units in the last place (ulp),
    plus a few units of the evaluation's precision (so within about 0.65 ulp for most types, and exact for powers of two in `log2`).
  * `tan` is within about 1 ulp; its error grows near ±π/2 where the tangent grows.
  * `exp`, `exp2` and `pow` have a relative error of a few units of the evaluation's precision (about 2^-29 for 32-bit types).
    This is within about 0.5 ulp for results up to 1, and a few ulp for the largest results, which have all the bits of the type.
    `exp2` is exact for integers.
  * `sin`, `cos` and `tan` reduce their argument with a 124-bit 2/π, so they are precise for large arguments too.
  * the coefficients (in `<fpm/fixed/detail/polynomials.hpp>`) are generated by `tools/polynomials.py`. Only the degree a type uses ends up in a program.
* `exp`, `exp2`, `pow`, `tan` and `hypot` saturate to the maximum value instead of overflowing, and `exp`/`exp2`/`pow` give 0 for results too small to represent.
* be mindful of a function's domain and range: the result of `pow` can quickly overflow with certain inputs.
* to trade precision for speed and code size, convert to a type with fewer fraction bits (or a 32-bit base type) before calling a function.

## Specialized customization points
The header `<fpm/fixed.hpp>` provides specializations for `fpm::fixed` for the following types:
* `std::hash`
* `std::numeric_limits`. Like for floating-point types, `min()` is the smallest positive value and `lowest()` the most negative one.

The header `<fpm/fixed/format.hpp>` provides the specialization of `std::formatter`.

## Conversions
The intent behind `fpm` is to replace floats for purposes of performance or portability. Thus, it guards against accidental usage of floats by requiring explicit conversion:
```c++
fpm::fixed_16_16 a = 0.5;        // Error: implicit construction from float
fpm::fixed_16_16 b { 0.5 };      // OK: explicit construction from float
fpm::fixed_16_16 c = b * 0.5;    // Error: implicit conversion from float
float d = b;                     // Error: implicit conversion to float
float e = static_cast<float>(b); // OK: explicit conversion to float
```

For integers, this still applies to initialization, but arithmetic operations *can* use integers:
```c++
fpm::fixed_16_16 a = 2;        // Error: implicit construction from int
fpm::fixed_16_16 b { 2 };      // OK: explicit construction from int
fpm::fixed_16_16 c = b / 2;    // OK
int d = b;                     // Error: requires explicit conversion
int e = static_cast<int>(b);   // OK: explicit conversion to int
bool f = b < 3;                // OK: comparison with an integer
```
You must still guard against underflow and overflow, though.

Arithmetic and comparisons with integers work for signed and unsigned integers alike (e.g. `total / values.size()`).
Comparisons with integers are exact, also for integers that the fixed-point type cannot represent.

`fpm::fixed<A, B, C>` can be constructed from an `fpm::fixed<D, E, F>` via explicit construction. This allows for conversion between fixed-point numbers of differing precision and range.
Depending on the respective underlying types and number of fraction bits, this conversion may throw away high bits in the integral or low bits in the fraction.

## Printing and reading fixed-point numbers
The `<fpm/fixed/ios.hpp>` header provides streaming operators. Simply stream an expression of type `fpm::fixed` to or from a `std::ostream`.

For instance, the following program prints `"===3.142e+02"`:
```c++
#include <fpm/fixed.hpp>
#include <fpm/fixed/ios.hpp>
#include <iostream>
#include <iomanip>

int main()
{
	fpm::fixed_16_16 x { 314.1516 };
	std::cout << std::setw(12) << std::setfill('=') << std::setprecision(3) << std::scientific << x << std::endl;
	return 0;
}
```

Reading fixed point numbers works similarly, by streaming `fpm::fixed` types from a `std::istream`.
Like for the built-in types, a value that is out of range (or infinity) stores the nearest value, the maximum or the lowest, and sets the stream's `failbit`.

`fpm`'s implementation of the streaming operators emulates streaming native floats as closely as possible without using floating-point types.

### Formatting
The `<fpm/fixed/format.hpp>` header provides `std::format` support, with the same format specifications as floating-point types (except `L`).
It does not depend on streams or locales, so it suits targets where those are too large. `<fpm/fixed/ios.hpp>` includes it as well.
```c++
#include <fpm/fixed/format.hpp>

std::string text = std::format("{:8.3f}", fpm::fixed_16_16{3.14159}); // "   3.142"
```

### Character conversions
The `<fpm/fixed/charconv.hpp>` header (also included by `<fpm/fixed/format.hpp>` and `<fpm/fixed/ios.hpp>`) provides `fpm::to_chars`, `fpm::from_chars` and `fpm::to_string`,
which behave like their standard counterparts for `double`. They are locale-independent, exact, use only integer arithmetic and are `constexpr`:
```c++
#include <fpm/fixed/charconv.hpp>

constexpr auto x = fpm::fixed_16_16(0.1);
static_assert(fpm::to_string(x) == "0.1");                   // shortest representation that reads back as `x`

char buffer[32];
auto [end, ec] = fpm::to_chars(buffer, buffer + 32, x, std::chars_format::fixed, 8); // "0.10000610": exact digits
fpm::fixed_16_16 y;
if(auto result = fpm::from_chars(buffer, end, y)) { /* y == x */ }
```
* Without a precision, `to_chars` and `to_string` produce the shortest text that `from_chars` converts back to the same value, like C++26 `std::to_string` and `std::format("{}", value)`.
* With a precision, `to_chars` behaves like `printf` with `%f`, `%e`, `%g` or `%a` (without `0x`), rounding exactly with ties to even.
* `from_chars` rounds exactly to the nearest value (ties to even), or truncates for types without rounding. Values out of range, as well as infinity and NaN, give `std::errc::result_out_of_range` and leave the value unmodified.
* Call them as `fpm::to_chars(...)` or unqualified (they are found by argument-dependent lookup). There are no overloads in namespace `std`: the standard does not allow adding them.

## Common constants
The following static member functions in the `fpm::fixed` class provide common mathematical constants in the fixed type:
* `e()`: _e_, roughly equal to 2.71828183.
* `pi()`: _π_, roughly equal to 3.14159265.
* `half_pi()`: _½π_, roughly equal to 1.57079633.
* `two_pi()`: _2π_, roughly equal to 6.2831853.

## Accuracy and performance
Please refer to the pages for [accuracy](accuracy.md) and [performance](performance.md) results, including how the number of fraction bits affects the speed.

## Limitations
Unlike floating-point numbers, `fpm::fixed`:
* can not represent Not-a-Number, infinity or negative zero.
* does not have a notion of subnormal numbers.
* does have a risk of overflow and underflow.

Notably the last point requires careful use of fixed-point numbers: like integers, you must ensure that they do not overflow or underflow.

## Alternatives
* [libfixmath](https://github.com/PetteriAimonen/libfixmath): C99 library, only supports Q16.16 format backed by 32-bit integers.
* [Compositional Numeric Library](https://github.com/johnmcfarlane/cnl): an experimental C++ header-only library worked on as part of WG-21/SG-14. Lacks many mathematical functions.
* [fp](https://github.com/mizvekov/fp): a C++14 header-only library. Does not provide any mathematical functions.
