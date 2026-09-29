#include "common.hpp"
#include <fpm/fixed/charconv.hpp>
#include <fpm/fraction/charconv.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <string_view>

template<typename T>
class fraction_charconv : public ::testing::Test
{
};

using FractionTypes = ::testing::Types<
	fpm::fraction<uint8_t>,
	fpm::fraction<uint16_t>,
	fpm::fraction<uint32_t>,
	fpm::fraction<uint64_t>
>;

TYPED_TEST_SUITE(fraction_charconv, FractionTypes);

namespace
{
	// With FPM_FRACTION_STRICT the numbers that are converted must be in [0, 1)
#ifdef FPM_FRACTION_STRICT
	constexpr bool strict = true;
#else
	constexpr bool strict = false;
#endif

	/// Raw values over the whole range: the edges, and random values of every magnitude
	template<typename P>
	std::vector<P> values(const int count = 2000)
	{
		using B = typename P::base_type;
		constexpr B max = std::numeric_limits<B>::max();
		std::vector<P> result;
		for(const B raw : {B{0}, B{1}, B{2}, B{3}, static_cast<B>(max / 2), static_cast<B>(max / 2 + 1), static_cast<B>(max - 1), max})
			result.push_back(P::from_raw_value(raw));
		std::mt19937_64 rng(1000 + P::fraction_bits);
		for(int i = 0; i < count; ++i)
		{
			const int bits = 1 + static_cast<int>(rng() % P::fraction_bits);
			result.push_back(P::from_raw_value(static_cast<B>(rng() >> (64 - bits))));
		}
		return result;
	}

	template<typename P>
	std::string to_chars_string(const P value, auto... args)
	{
		std::array<char, 1024> buffer{};
		const auto result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), value, args...);
		EXPECT_EQ(result.ec, std::errc{});
		return std::string(buffer.data(), result.ptr);
	}

	struct parsed
	{
		std::errc ec;
		std::size_t consumed;
	};

	template<typename P>
	parsed parse(const std::string_view text, P& value, const std::chars_format fmt = std::chars_format::general)
	{
		const auto result = fpm::from_chars(text.data(), text.data() + text.size(), value, fmt);
		return {result.ec, static_cast<std::size_t>(result.ptr - text.data())};
	}

	/// Parses all of the text
	template<typename P>
	::testing::AssertionResult parses_as(const std::string_view text, const P expected, const std::chars_format fmt = std::chars_format::general)
	{
		P value = P::from_raw_value(static_cast<typename P::base_type>(expected.raw_value() ^ 0x55));
		const auto result = parse(text, value, fmt);
		if(result.ec != std::errc{})
			return ::testing::AssertionFailure() << "\"" << text << "\" gives an error";
		if(result.consumed != text.size())
			return ::testing::AssertionFailure() << "\"" << text << "\" is parsed up to " << result.consumed;
		if(value != expected)
			return ::testing::AssertionFailure() << "\"" << text << "\" gives the raw value " << static_cast<uint64_t>(value.raw_value())
				<< " instead of " << static_cast<uint64_t>(expected.raw_value());
		return ::testing::AssertionSuccess();
	}

	/// Parses all of the text, as a number that is out of range: the value is left as it was
	template<typename P>
	::testing::AssertionResult out_of_range(const std::string_view text, const std::chars_format fmt = std::chars_format::general)
	{
		const P before = P::from_raw_value(0x5A);
		P value = before;
		const auto result = parse(text, value, fmt);
		if(result.ec != std::errc::result_out_of_range)
			return ::testing::AssertionFailure() << "\"" << text << "\" is not out of range, but gives the raw value " << static_cast<uint64_t>(value.raw_value());
		if(result.consumed != text.size())
			return ::testing::AssertionFailure() << "\"" << text << "\" is parsed up to " << result.consumed;
		if(value != before)
			return ::testing::AssertionFailure() << "\"" << text << "\" modified the value";
		return ::testing::AssertionSuccess();
	}

#ifdef FPM_INT128
	/// The exact decimal expansion of numerator / 2^bits (below 1), with simple wide arithmetic: "0.5", "0.0625"
	std::string exact_decimal(const fpm::int128_t numerator, const int bits)
	{
		using wide = fpm::int128_t;
		const wide mask = (wide{1} << bits) - 1;
		std::string text = "0.";
		wide frac = numerator;
		while(frac != 0)
		{
			frac *= 10;
			text.push_back(static_cast<char>('0' + static_cast<int>(frac >> bits)));
			frac &= mask;
		}
		if(text == "0.")
			text = "0";
		return text;
	}
#endif

	// Constant expressions
	constexpr auto half16 = fpm::fraction<uint16_t>::from_raw_value(0x8000);
	static_assert(fpm::to_string(half16) == "0.5");
	static_assert(fpm::to_string(fpm::fraction<uint8_t>::from_raw_value(1)) == "0.004");
	static_assert(fpm::to_string(fpm::fraction<uint64_t>{}) == "0");
	static_assert([]
	{
		fpm::fraction<uint32_t> value{};
		const std::string_view text = "0.75";
		const auto result = fpm::from_chars(text.data(), text.data() + text.size(), value);
		return result.ec == std::errc{} && value.raw_value() == 0xC000'0000u;
	}());
	static_assert([]
	{
		fpm::fraction<uint64_t> value{0.5};
		const std::string_view text = "-1.25";
		const auto result = fpm::from_chars(text.data(), text.data() + text.size(), value);
		return strict
			? (result.ec == std::errc::result_out_of_range && value == fpm::fraction<uint64_t>{0.5})
			: (result.ec == std::errc{} && value == fpm::fraction<uint64_t>{0.75});
	}());
	static_assert([]
	{
		// Rounds up to 1
		fpm::fraction<uint8_t> value{0.5};
		const std::string_view text = "0.999";
		return fpm::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{} && value.raw_value() == 0;
	}());
}

TYPED_TEST(fraction_charconv, examples)
{
	using P = TypeParam;
	using B = typename P::base_type;
	constexpr auto top = static_cast<B>(B{1} << (P::fraction_bits - 1));

	EXPECT_EQ("0", to_chars_string(P{}));
	EXPECT_EQ("0.5", to_chars_string(P::from_raw_value(top)));
	EXPECT_EQ("0.25", to_chars_string(P::from_raw_value(static_cast<B>(top / 2))));
	EXPECT_EQ("0.75", to_chars_string(P::from_raw_value(static_cast<B>(top + top / 2))));
	EXPECT_EQ("0.125", to_chars_string(P{0.125}));
	// The shortest digits that give the value: the ones it was parsed from, if those are few
	for(const std::string_view text : {"0.1", "0.3", "0.7", "0.12", "0.99"})
	{
		P parsed{};
		EXPECT_EQ(std::errc{}, parse(text, parsed).ec);
		EXPECT_EQ(text, to_chars_string(parsed));
	}
	EXPECT_EQ("0.5", fpm::to_string(P{0.5}));

	// Found by argument-dependent lookup
	EXPECT_EQ("0.25", to_string(P{0.25}));

	EXPECT_EQ("5e-01", to_chars_string(P{0.5}, std::chars_format::scientific));
	EXPECT_EQ("2.5e-01", to_chars_string(P{0.25}, std::chars_format::scientific));
	EXPECT_EQ("0.5", to_chars_string(P{0.5}, std::chars_format::fixed));
	EXPECT_EQ("0.5", to_chars_string(P{0.5}, std::chars_format::general));
	EXPECT_EQ("1p-1", to_chars_string(P{0.5}, std::chars_format::hex));
	EXPECT_EQ("1.8p-1", to_chars_string(P{0.75}, std::chars_format::hex));
	EXPECT_EQ("0p+0", to_chars_string(P{}, std::chars_format::hex));

	EXPECT_EQ("0.500", to_chars_string(P{0.5}, std::chars_format::fixed, 3));
	EXPECT_EQ("0", to_chars_string(P{0.25}, std::chars_format::fixed, 0));
	EXPECT_EQ("1", to_chars_string(P{0.75}, std::chars_format::fixed, 0));
	EXPECT_EQ("0", to_chars_string(P{0.5}, std::chars_format::fixed, 0)); // tie: to even
	EXPECT_EQ("0.2", to_chars_string(P{0.25}, std::chars_format::fixed, 1)); // tie: to even
	EXPECT_EQ("0.8", to_chars_string(P{0.75}, std::chars_format::fixed, 1)); // tie: to even
	EXPECT_EQ("7.50e-01", to_chars_string(P{0.75}, std::chars_format::scientific, 2));
	EXPECT_EQ("0.75", to_chars_string(P{0.75}, std::chars_format::general, 5));
	EXPECT_EQ("1.80p-1", to_chars_string(P{0.75}, std::chars_format::hex, 2));
	EXPECT_EQ("0.000000", to_chars_string(P{}, std::chars_format::fixed, -1));

	EXPECT_TRUE(parses_as("0", P{}));
	EXPECT_TRUE(parses_as("0.0", P{}));
	EXPECT_TRUE(parses_as("-0", P{}));
	EXPECT_TRUE(parses_as("-0.000", P{}));
	EXPECT_TRUE(parses_as("0e100", P{}));
	EXPECT_TRUE(parses_as("000.5", P{0.5}));
	EXPECT_TRUE(parses_as(".5", P{0.5}));
	EXPECT_TRUE(parses_as("0.5", P{0.5}));
	EXPECT_TRUE(parses_as("0.50000000000000000000000000000000000000000000000000000000000000000000000000000000000", P{0.5}));
	EXPECT_TRUE(parses_as("5e-1", P{0.5}));
	EXPECT_TRUE(parses_as("50E-2", P{0.5}));
	EXPECT_TRUE(parses_as("0.025e1", P{0.25}));
	EXPECT_TRUE(parses_as("0." + std::string(23, '0') + "75e23", P{0.75}));
	EXPECT_TRUE(parses_as("75" + std::string(39, '0') + "e-41", P{0.75}));
	EXPECT_TRUE(parses_as("0." + std::string(500, '0') + "75e500", P{0.75}));
	EXPECT_TRUE(parses_as("1e-2000000000", P{}));
	EXPECT_TRUE(parses_as("0.8", P{0.5}, std::chars_format::hex));
	EXPECT_TRUE(parses_as("1p-1", P{0.5}, std::chars_format::hex));
	EXPECT_TRUE(parses_as("c0p-8", P{0.75}, std::chars_format::hex));
	EXPECT_TRUE(parses_as("0.75", P{0.75}, std::chars_format::fixed));
	EXPECT_TRUE(parses_as("7.5e-1", P{0.75}, std::chars_format::scientific));

	// The rest of the text is not parsed
	P value{};
	EXPECT_EQ(3u, parse("0.5x", value).consumed);
	EXPECT_EQ(P{0.5}, value);
	EXPECT_EQ(3u, parse("0.5e", value).consumed);
	EXPECT_EQ(4u, parse("0.25e-1", value, std::chars_format::fixed).consumed);
	EXPECT_EQ(P{0.25}, value);

	// Not a number
	for(const std::string_view text : {"", "-", ".", "+0.5", " 0.5", "x", "e5", "-.", "0x0.8"})
	{
		value = P{0.125};
		const auto result = parse(text, value, text == "0x0.8" ? std::chars_format::scientific : std::chars_format::general);
		EXPECT_EQ(std::errc::invalid_argument, result.ec) << text;
		EXPECT_EQ(0u, result.consumed) << text;
		EXPECT_EQ(P{0.125}, value) << text;
	}
	EXPECT_EQ(std::errc::invalid_argument, parse("0.5", value, std::chars_format::scientific).ec);
}

namespace
{
	/// A number that is not in [0, 1): its fraction, or out of range if that is checked
	template<typename P>
	::testing::AssertionResult wraps_to(const std::string_view text, const P expected, const std::chars_format fmt = std::chars_format::general)
	{
		return strict ? out_of_range<P>(text, fmt) : parses_as(text, expected, fmt);
	}
}

// The fraction of any number (modulo 1), or out of range with FPM_FRACTION_STRICT
TYPED_TEST(fraction_charconv, not_in_range)
{
	using P = TypeParam;

	// At least 1
	EXPECT_TRUE(wraps_to("1", P{}));
	EXPECT_TRUE(wraps_to("1.0", P{}));
	EXPECT_TRUE(wraps_to("1.25", P{0.25}));
	{
		// The same digits after the decimal point: the same fraction
		P tenth{};
		ASSERT_EQ(std::errc{}, parse("0.1", tenth).ec);
		EXPECT_TRUE(wraps_to("1.1", tenth));
		EXPECT_TRUE(wraps_to("77.1", tenth));
		EXPECT_TRUE(wraps_to("-0.9", tenth));
		EXPECT_TRUE(wraps_to("-5.9", tenth));
	}
	EXPECT_TRUE(wraps_to("001", P{}));
	EXPECT_TRUE(wraps_to("2.5", P{0.5}));
	EXPECT_TRUE(wraps_to("1e0", P{}));
	EXPECT_TRUE(wraps_to("10e-1", P{}));
	EXPECT_TRUE(wraps_to("0.1e1", P{}));
	EXPECT_TRUE(wraps_to("0.5e1", P{}));
	EXPECT_TRUE(wraps_to("0.525e1", P{0.25}));
	EXPECT_TRUE(wraps_to("1275e-2", P{0.75}));
	EXPECT_TRUE(wraps_to("0.00012375e5", P{0.375}));
	EXPECT_TRUE(wraps_to("1e100", P{}));
	EXPECT_TRUE(wraps_to("1e2000000000", P{}));
	EXPECT_TRUE(wraps_to("1.5e2000000000", P{}));
	EXPECT_TRUE(wraps_to("123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890", P{}));
	EXPECT_TRUE(wraps_to("123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890.625", P{0.625}));
	EXPECT_TRUE(wraps_to("123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890625e-3", P{0.625}));
	EXPECT_TRUE(wraps_to(std::string(3000, '7') + ".5", P{0.5}));
	EXPECT_TRUE(wraps_to("1", P{}, std::chars_format::hex));
	EXPECT_TRUE(wraps_to("0.8p1", P{}, std::chars_format::hex));
	EXPECT_TRUE(wraps_to("0.cp1", P{0.5}, std::chars_format::hex));
	EXPECT_TRUE(wraps_to("3.4", P{0.25}, std::chars_format::hex));
	EXPECT_TRUE(wraps_to("ffffffffffffffffffffffff.8", P{0.5}, std::chars_format::hex));
	EXPECT_TRUE(wraps_to("1p100", P{}, std::chars_format::hex));
	EXPECT_TRUE(wraps_to("1.8p2000000000", P{}, std::chars_format::hex));
	EXPECT_TRUE(wraps_to("1.25", P{0.25}, std::chars_format::fixed));
	EXPECT_TRUE(wraps_to("1.25e0", P{0.25}, std::chars_format::scientific));

	// Negative: -0.25 is a quarter before 1
	EXPECT_TRUE(wraps_to("-0.25", P{0.75}));
	EXPECT_TRUE(wraps_to("-0.75", P{0.25}));
	EXPECT_TRUE(wraps_to("-1", P{}));
	EXPECT_TRUE(wraps_to("-1.75", P{0.25}));
	EXPECT_TRUE(wraps_to("-12345.5", P{0.5}));
	EXPECT_TRUE(wraps_to("-2.5e-1", P{0.75}));
	EXPECT_TRUE(wraps_to("-1e-100", P{})); // rounds up to 1
	EXPECT_TRUE(wraps_to("-0." + std::string(90, '0') + "1", P{}));
	EXPECT_TRUE(wraps_to("-0.4", P{0.75}, std::chars_format::hex));
	EXPECT_TRUE(wraps_to("-1p-100", P{}, std::chars_format::hex));
	EXPECT_EQ(P{0.75}, -P{0.25}); // like the negative fraction

	// Zero is in the range, with any sign and exponent
	EXPECT_TRUE(parses_as("-0", P{}));
	EXPECT_TRUE(parses_as("-0.000e5", P{}));
	EXPECT_TRUE(parses_as("000.000e-5", P{}));
	EXPECT_TRUE(parses_as("-0p9", P{}, std::chars_format::hex));

	// In the range, with an exponent
	EXPECT_TRUE(parses_as("2.5e-1", P{0.25}));
	EXPECT_TRUE(parses_as("0.0075e2", P{0.75}));
	EXPECT_TRUE(parses_as("000000000000000000000000000000000000000000000000000000.5", P{0.5}));

	// Not a number: always
	EXPECT_TRUE(out_of_range<P>("inf"));
	EXPECT_TRUE(out_of_range<P>("-inf"));
	EXPECT_TRUE(out_of_range<P>("INFINITY"));
	EXPECT_TRUE(out_of_range<P>("nan"));
	EXPECT_TRUE(out_of_range<P>("NaN(abc)"));
}

// Numbers in [0, 1) that round up to 1 give 0: also with FPM_FRACTION_STRICT
TYPED_TEST(fraction_charconv, rounds_up_to_one)
{
	using P = TypeParam;
	const auto largest = P::from_raw_value(std::numeric_limits<typename P::base_type>::max());

	EXPECT_TRUE(parses_as("0.99999999999999999999999999999999999999", P{}));
	EXPECT_TRUE(parses_as("0." + std::string(500, '9'), P{}));
	EXPECT_TRUE(parses_as("9.9999999999999999999999999999999999999e-1", P{}));
	EXPECT_TRUE(parses_as("0.ffffffffffffffffffffffff", P{}, std::chars_format::hex));
	EXPECT_TRUE(parses_as("f.fffffffffffffffffffffffp-4", P{}, std::chars_format::hex));

	// The largest fraction itself
	EXPECT_TRUE(parses_as(to_chars_string(largest), largest));
	EXPECT_TRUE(parses_as(to_chars_string(largest, std::chars_format::fixed, 80), largest));
	EXPECT_TRUE(parses_as(to_chars_string(largest, std::chars_format::hex), largest, std::chars_format::hex));
}

TYPED_TEST(fraction_charconv, round_trip)
{
	using P = TypeParam;

	for(const P x : values<P>())
	{
		// The shortest representations
		const auto shortest = to_chars_string(x);
		ASSERT_TRUE(parses_as(shortest, x));
		ASSERT_EQ(shortest, fpm::to_string(x));
		ASSERT_TRUE(parses_as(to_chars_string(x, std::chars_format::fixed), x, std::chars_format::fixed));
		ASSERT_TRUE(parses_as(to_chars_string(x, std::chars_format::general), x));
		ASSERT_TRUE(parses_as(to_chars_string(x, std::chars_format::hex), x, std::chars_format::hex));
		if(x != P{})
			ASSERT_TRUE(parses_as(to_chars_string(x, std::chars_format::scientific), x, std::chars_format::scientific));

		// All the digits
		const auto bits = static_cast<int>(P::fraction_bits);
		ASSERT_TRUE(parses_as(to_chars_string(x, std::chars_format::fixed, bits), x));
		ASSERT_TRUE(parses_as(to_chars_string(x, std::chars_format::scientific, bits), x));
	}
}

// No representation with fewer digits parses back to the value
TYPED_TEST(fraction_charconv, shortest_is_minimal)
{
	using P = TypeParam;

	for(const P x : values<P>(300))
	{
		if(x == P{})
			continue;
		const auto shortest = to_chars_string(x, std::chars_format::scientific); // d.ddde-xx
		const auto digits = static_cast<int>(shortest.find('e')) - (shortest.find('.') == std::string::npos ? 0 : 1);
		if(digits == 1)
			continue;

		// With one digit less: rounded down, and rounded up
		auto fewer = to_chars_string(x, std::chars_format::scientific, digits - 2);
		P value{};
		if(parse(fewer, value, std::chars_format::scientific).ec == std::errc{})
			ASSERT_NE(x, value) << shortest << " could be " << fewer;
		for(const char direction : {'+', '-'})
		{
			// The neighbours of the last digit (without a carry)
			auto neighbour = fewer;
			char& last = neighbour[neighbour.find('e') - 1];
			if((direction == '+' && last == '9') || (direction == '-' && last == '0'))
				continue;
			last = static_cast<char>(last + (direction == '+' ? 1 : -1));
			if(parse(neighbour, value, std::chars_format::scientific).ec == std::errc{})
				ASSERT_NE(x, value) << shortest << " could be " << neighbour;
		}
	}
}

#ifdef FPM_INT128
TYPED_TEST(fraction_charconv, exact_digits)
{
	using P = TypeParam;
	const auto bits = static_cast<int>(P::fraction_bits);

	for(const P x : values<P>())
	{
		// The exact expansion has as many digits as the lowest set bit's position
		const auto exact = exact_decimal(x.raw_value(), bits);
		std::string expected = (exact == "0") ? "0." : exact;
		ASSERT_LE(expected.size(), static_cast<std::size_t>(bits) + 2);
		expected.resize(static_cast<std::size_t>(bits) + 2, '0');
		ASSERT_EQ(expected, to_chars_string(x, std::chars_format::fixed, bits));
		ASSERT_TRUE(parses_as(exact, x));
	}
}

// Halfway between two values: to the even one. Just above or below: to the nearest.
TYPED_TEST(fraction_charconv, rounds_to_nearest_even)
{
	using P = TypeParam;
	using B = typename P::base_type;
	const auto bits = static_cast<int>(P::fraction_bits);

	for(const P x : values<P>())
	{
		if(x.raw_value() == std::numeric_limits<B>::max())
			continue;
		const P next = P::from_raw_value(static_cast<B>(x.raw_value() + 1));
		const P even = (x.raw_value() % 2 == 0) ? x : next;

		// (2X + 1) / 2^(N+1)
		const auto midpoint = exact_decimal((static_cast<fpm::int128_t>(x.raw_value()) << 1) | 1, bits + 1);
		ASSERT_TRUE(parses_as(midpoint, even));
		ASSERT_TRUE(parses_as(midpoint + "000", even));
		ASSERT_TRUE(parses_as(midpoint + "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001", next));
		ASSERT_TRUE(parses_as(midpoint + "1", next));

		// The last digit of a midpoint is 5
		auto below = midpoint;
		below.back() = '4';
		ASSERT_TRUE(parses_as(below, x));
		ASSERT_TRUE(parses_as(below + "9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999", x));
	}

	// The midpoint below 1, and beyond: up to 1, which is 0 (and even)
	const auto last = P::from_raw_value(std::numeric_limits<B>::max());
	const auto midpoint = exact_decimal((static_cast<fpm::int128_t>(last.raw_value()) << 1) | 1, bits + 1);
	EXPECT_TRUE(parses_as(midpoint, P{}));
	EXPECT_TRUE(parses_as(midpoint + "1", P{}));
	auto below = midpoint;
	below.back() = '4';
	EXPECT_TRUE(parses_as(below, last));
}
#endif

namespace
{
	/// snprintf of the C library. On MinGW its own one: the one of Windows has no `long double` (which is `double` there).
	template<typename... Args>
	void print(std::array<char, 256>& buffer, const char* format, const Args... args)
	{
#ifdef __MINGW32__
		__mingw_snprintf(buffer.data(), buffer.size(), format, args...);
#else
		std::snprintf(buffer.data(), buffer.size(), format, args...);
#endif
	}
}

// Like printf, where `long double` represents the values exactly
TYPED_TEST(fraction_charconv, precision_like_printf)
{
	using P = TypeParam;
	if(std::numeric_limits<long double>::digits < 64)
		GTEST_SKIP() << "long double has less than 64 bits of precision";

	for(const P x : values<P>(300))
	{
		const long double reference = std::ldexp(static_cast<long double>(x.raw_value()), -static_cast<int>(P::fraction_bits));
		for(const int precision : {0, 1, 2, 3, 6, 10, 17, 30, 70})
		{
			std::array<char, 256> buffer{};
			print(buffer, "%.*Lf", precision, reference);
			ASSERT_EQ(std::string(buffer.data()), to_chars_string(x, std::chars_format::fixed, precision));
			print(buffer, "%.*Le", precision, reference);
			ASSERT_EQ(std::string(buffer.data()), to_chars_string(x, std::chars_format::scientific, precision));
			print(buffer, "%.*Lg", precision, reference);
			ASSERT_EQ(std::string(buffer.data()), to_chars_string(x, std::chars_format::general, precision));
		}

		// Hexadecimal: `double` normalizes like `to_chars` (1.xxx), for the values it represents exactly
		const auto value = static_cast<double>(reference);
		if(static_cast<long double>(value) == reference)
		{
			for(const int precision : {-1, 0, 1, 2, 5, 13, 20})
			{
				std::array<char, 256> buffer{};
				if(precision < 0)
					print(buffer, "%a", value);
				else
					print(buffer, "%.*a", precision, value);
				const std::string expected(buffer.data() + 2); // without "0x"
				if(precision < 0)
					ASSERT_EQ(expected, to_chars_string(x, std::chars_format::hex));
				ASSERT_EQ(expected, to_chars_string(x, std::chars_format::hex, precision));
			}
		}
	}
}

TYPED_TEST(fraction_charconv, buffer_too_small)
{
	using P = TypeParam;
	const auto text = to_chars_string(P{0.3});

	std::array<char, 64> buffer{};
	for(std::size_t size = 0; size < text.size(); ++size)
	{
		const auto result = fpm::to_chars(buffer.data(), buffer.data() + size, P{0.3});
		EXPECT_EQ(std::errc::value_too_large, result.ec);
		EXPECT_EQ(buffer.data() + size, result.ptr);
	}
	const auto result = fpm::to_chars(buffer.data(), buffer.data() + text.size(), P{0.3});
	EXPECT_EQ(std::errc{}, result.ec);
	EXPECT_EQ(text, std::string_view(buffer.data(), result.ptr));
}

namespace
{
	/// The fixed-point type with the same fraction bits has the same grid: its conversions are another implementation
	template<typename P, typename Q>
	void compare_with_fixed()
	{
		using B = typename P::base_type;
		ASSERT_EQ(P::fraction_bits, Q::fraction_bits);
		ASSERT_TRUE(Q::enable_rounding);

		const auto all = (P::fraction_bits <= 16) ? std::vector<P>{} : values<P>(20000);
		const uint64_t count = (P::fraction_bits <= 16) ? (uint64_t{1} << P::fraction_bits) : all.size();
		for(uint64_t i = 0; i < count; ++i)
		{
			const P x = (P::fraction_bits <= 16) ? P::from_raw_value(static_cast<B>(i)) : all[i];
			const Q q(x);
			ASSERT_EQ(static_cast<uint64_t>(x.raw_value()), static_cast<uint64_t>(q.raw_value()));

			std::array<char, 256> buffer{};
			const auto text = [&](auto... args)
			{
				const auto result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), q, args...);
				return std::string(buffer.data(), result.ptr);
			};
			ASSERT_EQ(text(), to_chars_string(x));
			ASSERT_EQ(text(std::chars_format::scientific), to_chars_string(x, std::chars_format::scientific));
			ASSERT_EQ(text(std::chars_format::hex), to_chars_string(x, std::chars_format::hex));
			for(const int precision : {0, 1, 4, 9, 40})
			{
				ASSERT_EQ(text(std::chars_format::fixed, precision), to_chars_string(x, std::chars_format::fixed, precision));
				ASSERT_EQ(text(std::chars_format::scientific, precision), to_chars_string(x, std::chars_format::scientific, precision));
				ASSERT_EQ(text(std::chars_format::general, precision), to_chars_string(x, std::chars_format::general, precision));
				ASSERT_EQ(text(std::chars_format::hex, precision), to_chars_string(x, std::chars_format::hex, precision));
			}
		}

		// Random digits
		std::mt19937_64 rng(P::fraction_bits);
		for(int i = 0; i < 50000; ++i)
		{
			std::string digits = (rng() % 2 == 0) ? "0." : ".";
			const auto length = 1 + rng() % 45;
			// Also close to a value of the grid, and to the midpoint between two: those have many zeros and nines
			const auto kind = rng() % 4;
			for(uint64_t j = 0; j < length; ++j)
				digits.push_back(static_cast<char>('0' + (kind == 0 ? rng() % 10 : (rng() % 8 == 0 ? rng() % 10 : (kind == 1 ? 0 : (kind == 2 ? 9 : 5))))));
			if(rng() % 4 == 0)
			{
				// The same number with an exponent
				const auto shift = static_cast<int>(rng() % 5);
				digits = "0." + std::string(static_cast<std::size_t>(shift), '0') + digits.substr(digits.find('.') + 1) + "e" + std::to_string(shift);
			}

			Q expected{};
			const auto reference = fpm::from_chars(digits.data(), digits.data() + digits.size(), expected);
			ASSERT_EQ(std::errc{}, reference.ec) << digits;
			// (1 for a number that rounds up to it: that is 0)
			ASSERT_TRUE(parses_as(digits, P{expected})) << digits;

			// With an integral part and a sign: its fraction
			if(!strict && std::is_signed_v<typename Q::base_type> && Q::integral_bits > 8 && digits.find('e') == std::string::npos)
			{
				const std::string number = ((rng() % 2 == 0) ? "-" : "") + std::to_string(rng() % 100) + digits.substr(digits.find('.'));
				const auto whole = fpm::from_chars(number.data(), number.data() + number.size(), expected);
				ASSERT_EQ(std::errc{}, whole.ec) << number;
				ASSERT_TRUE(parses_as(number, P{expected})) << number;
			}
		}
	}
}

TEST(fraction_charconv, like_fixed)
{
	compare_with_fixed<fpm::fraction<uint16_t>, fpm::fixed_16_16>();
	compare_with_fixed<fpm::fraction<uint8_t>, fpm::fixed_8_8>();
	compare_with_fixed<fpm::fraction<uint16_t>, fpm::fixed<uint32_t, uint64_t, 16>>();
#ifdef FPM_INT128
	compare_with_fixed<fpm::fraction<uint32_t>, fpm::fixed_32_32>();
#endif
}

// Decimal places (-min_exponent10): numbers with that many of them are read and written without change.
// Not significant digits, which is what digits10 is about: that is 0.
TYPED_TEST(fraction_charconv, decimal_places)
{
	using P = TypeParam;
	const int digits = -std::numeric_limits<P>::min_exponent10;
	ASSERT_GE(digits, 2);
	EXPECT_EQ(0, std::numeric_limits<P>::digits10);

	uint64_t count = 1;
	for(int i = 0; i < digits; ++i)
		count *= 10;
	std::mt19937_64 rng(10);
	const bool all = count <= 100000;
	for(uint64_t i = 0; i < (all ? count : 100000); ++i)
	{
		// Every number, or random ones and the largest ones
		const uint64_t number = all ? i : (i < 1000 ? count - 1 - i : rng() % count);
		auto text = std::to_string(number);
		text = "0." + std::string(static_cast<std::size_t>(digits) - text.size(), '0') + text;

		P value{};
		ASSERT_EQ(std::errc{}, parse(text, value).ec) << text;
		ASSERT_EQ(text, to_chars_string(value, std::chars_format::fixed, digits));
	}

	// Not with one more
	bool changed = false;
	for(uint64_t number = 0; number < 20000 && !changed; ++number)
	{
		auto text = std::to_string(number);
		text = "0." + std::string(static_cast<std::size_t>(digits) + 1 - text.size(), '0') + text;
		P value{};
		ASSERT_EQ(std::errc{}, parse(text, value).ec) << text;
		changed = text != to_chars_string(value, std::chars_format::fixed, digits + 1);
	}
	EXPECT_TRUE(changed);
}
