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

#ifdef __SIZEOF_INT128__
	/// The exact decimal expansion of numerator / 2^bits (below 1), with simple wide arithmetic: "0.5", "0.0625"
	std::string exact_decimal(const unsigned __int128 numerator, const int bits)
	{
		using wide = unsigned __int128;
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
		fpm::fraction<uint64_t> value{};
		const std::string_view text = "1.0";
		return fpm::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc::result_out_of_range;
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

TYPED_TEST(fraction_charconv, out_of_range)
{
	using P = TypeParam;

	// At least 1
	EXPECT_TRUE(out_of_range<P>("1"));
	EXPECT_TRUE(out_of_range<P>("1.0"));
	EXPECT_TRUE(out_of_range<P>("1.25"));
	EXPECT_TRUE(out_of_range<P>("001"));
	EXPECT_TRUE(out_of_range<P>("2"));
	EXPECT_TRUE(out_of_range<P>("1e0"));
	EXPECT_TRUE(out_of_range<P>("10e-1"));
	EXPECT_TRUE(out_of_range<P>("0.1e1"));
	EXPECT_TRUE(out_of_range<P>("0.5e1"));
	EXPECT_TRUE(out_of_range<P>("1e100"));
	EXPECT_TRUE(out_of_range<P>("1e2000000000"));
	EXPECT_TRUE(out_of_range<P>("123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"));
	EXPECT_TRUE(out_of_range<P>("1", std::chars_format::hex));
	EXPECT_TRUE(out_of_range<P>("0.8p1", std::chars_format::hex));
	EXPECT_TRUE(out_of_range<P>("1p100", std::chars_format::hex));

	// Rounds up to 1
	EXPECT_TRUE(out_of_range<P>("0.99999999999999999999999999999999999999"));
	EXPECT_TRUE(out_of_range<P>("0.ffffffffffffffffffffffff", std::chars_format::hex));

	// Negative
	EXPECT_TRUE(out_of_range<P>("-0.25"));
	EXPECT_TRUE(out_of_range<P>("-1"));
	EXPECT_TRUE(out_of_range<P>("-1e-100"));
	EXPECT_TRUE(out_of_range<P>("-0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000001"));
	EXPECT_TRUE(out_of_range<P>("-1p-100", std::chars_format::hex));

	// Not a number in [0, 1)
	EXPECT_TRUE(out_of_range<P>("inf"));
	EXPECT_TRUE(out_of_range<P>("-inf"));
	EXPECT_TRUE(out_of_range<P>("INFINITY"));
	EXPECT_TRUE(out_of_range<P>("nan"));
	EXPECT_TRUE(out_of_range<P>("NaN(abc)"));
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

#ifdef __SIZEOF_INT128__
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
		const auto midpoint = exact_decimal((static_cast<unsigned __int128>(x.raw_value()) << 1) | 1, bits + 1);
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

	// The midpoint below 1
	const auto last = P::from_raw_value(std::numeric_limits<B>::max());
	const auto midpoint = exact_decimal((static_cast<unsigned __int128>(last.raw_value()) << 1) | 1, bits + 1);
	EXPECT_TRUE(out_of_range<P>(midpoint));
	auto below = midpoint;
	below.back() = '4';
	EXPECT_TRUE(parses_as(below, last));
}
#endif

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
			std::snprintf(buffer.data(), buffer.size(), "%.*Lf", precision, reference);
			ASSERT_EQ(std::string(buffer.data()), to_chars_string(x, std::chars_format::fixed, precision));
			std::snprintf(buffer.data(), buffer.size(), "%.*Le", precision, reference);
			ASSERT_EQ(std::string(buffer.data()), to_chars_string(x, std::chars_format::scientific, precision));
			std::snprintf(buffer.data(), buffer.size(), "%.*Lg", precision, reference);
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
					std::snprintf(buffer.data(), buffer.size(), "%a", value);
				else
					std::snprintf(buffer.data(), buffer.size(), "%.*a", precision, value);
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
			if(expected == Q{1})
				ASSERT_TRUE(out_of_range<P>(digits)) << digits; // rounds up to 1
			else
				ASSERT_TRUE(parses_as(digits, P{expected})) << digits;
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
