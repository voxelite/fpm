// Uses all of the library, with the defaults of ESP-IDF: without exceptions, and without 128-bit integers
// (so without the fixed-point types with a 64-bit base type).

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

#include <cstdio>
#include <limits>
#include <sstream>
#include <string>

namespace
{
	using number = fpm::fixed_16_16;
	using angle = fpm::fraction<std::uint16_t>;

	// The results are the same for every platform
	static_assert(number{1.5} * number{2.5} == number{3.75});
	static_assert(number{1} / 3 == number::from_raw_value(21845));
	static_assert(sqrt(number{2}) == number::from_raw_value(92682));
	static_assert(fpm::to_string(number{0.1}) == "0.1");
#ifndef FPM_FRACTION_STRICT
	static_assert(angle{1.25} == angle{0.25});
#endif
	static_assert(fpm::sin<number>(angle{0.25}) == number{1});
	static_assert(fpm::atan2<angle>(number{1}, number{1}) == angle{0.125});
	static_assert(fpm::difference(angle{0.125}, angle{0.875}).direction == fpm::direction::CounterClockwise);
	static_assert(expm1(log1p(std::numeric_limits<number>::max())) > number{32767});

#ifdef FPM_DEFINED_OVERFLOW
	// The results that a type cannot represent
	static_assert(abs(std::numeric_limits<number>::lowest()) == std::numeric_limits<number>::max());
	static_assert(pow(number{-200}, 3) == std::numeric_limits<number>::lowest());
	static_assert(ceil(std::numeric_limits<number>::max()) == std::numeric_limits<number>::max());
#ifndef FPM_CHECK_OVERFLOW
	static_assert(std::numeric_limits<number>::max() + number::from_raw_value(1) == std::numeric_limits<number>::lowest());
	static_assert(number{200} * number{200} == number{40000 - 65536});
#endif
#endif

	template<typename Number>
	Number calculate(const Number x, const Number y)
	{
		return sin(x) + cos(y) + tan(x) + atan2(x, y) + atan(x) + sqrt(abs(x)) + cbrt(y) + hypot(x, y)
			+ exp(x / 16) + exp2(y / 16) + log(abs(y) + 1) + log2(abs(x) + 1) + log10(abs(x) + 1) + pow(abs(x) + 1, y / 16) + pow(x, 3)
			+ asin(x / 1000) + acos(y / 1000) + floor(x) + ceil(y) + round(x) + trunc(y) + fmod(x, y + 1000) + remainder(x, y + 1000)
			+ expm1(x / 16) + log1p(abs(y)) + copysign(x, y) + nearbyint(y) + nextafter(x, y) + pow(x, y / 16 + 2) + pow(y + 2, -2)
			+ x * y + x / (y + 1000) + x * 3 + y / 7u + Number{1.5f} + Number{2.5};
	}

	template<typename Angle, typename Number>
	Number rotate(Angle& rotation, const Number velocity, const Number delta, const Angle target)
	{
		rotation += Angle(velocity * delta);
		const auto [distance, direction] = fpm::difference(target, rotation);
		rotation = (direction == fpm::direction::Clockwise) ? rotation - distance / 2 : rotation + distance / 2;
		rotation = fpm::atan2<Angle>(fpm::sin<Number>(rotation), fpm::cos<Number>(rotation)) + fpm::asin<Angle>(fpm::cos<Number>(rotation))
			+ fpm::acos<Angle>(fpm::sin<Number>(rotation)) + fpm::atan<Angle>(velocity) + Angle(fpm::fraction<std::uint32_t>(rotation)) * 3;
		return fpm::tan<Number>(rotation) + Number(fpm::difference(target, rotation));
	}

	template<typename Value>
	std::string text(Value value, const char* const input)
	{
		char buffer[96];
		const auto written = fpm::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 6);
		std::string result(buffer, written.ptr);

		const std::string characters(input);
		if(fpm::from_chars(characters.data(), characters.data() + characters.size(), value).ec == std::errc{})
			result += " " + fpm::to_string(value) + " " + std::format("{:>12.4f} {:e} {}", value, value, value);

		std::istringstream in(characters);
		std::ostringstream out;
		if(in >> value)
			out << ' ' << value;
		return result + out.str();
	}
}

extern "C" void app_main()
{
	// (Not constants for the compiler)
	volatile int first = 3;
	volatile int second = 7;

	const auto a = calculate(number{first} / 2, number{second} / 4);
	const auto b = calculate(fpm::fixed_8_8{first} / 2, fpm::fixed_8_8{second} / 4);
	const auto c = calculate(fpm::fixed<std::int32_t, std::int64_t, 16, false>{first} / 2, fpm::fixed<std::int32_t, std::int64_t, 16, false>{second} / 4);
	const auto d = calculate(fpm::fixed<std::uint32_t, std::uint64_t, 16>{first} / 2, fpm::fixed<std::uint32_t, std::uint64_t, 16>{second} / 4);

	angle rotation{0.75};
	const auto e = rotate(rotation, number{first} / 8, number{second} / 64, angle{0.125});
	fpm::fraction<std::uint32_t> wide{0.75};
	const auto f = rotate(wide, number{first} / 8, number{second} / 64, fpm::fraction<std::uint32_t>{0.125});
	fpm::fraction<std::uint8_t> narrow{0.75};
	const auto g = rotate(narrow, fpm::fixed_8_8{first} / 8, fpm::fixed_8_8{second} / 64, fpm::fraction<std::uint8_t>{0.125});

	std::printf("%s\n%s\n%s\n%s\n",
		text(a + number(b) + number(c) + number(d) + e + f + number(g), "1234.5678").c_str(),
		text(rotation, "0.875").c_str(),
		text(wide, "-12.25e-1").c_str(),
		text(narrow, "0.3").c_str());
}
