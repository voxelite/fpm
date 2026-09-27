#include "common.hpp"
#include <fpm/charconv.hpp>
#include <fpm/ios.hpp>

#include <array>
#include <charconv>
#include <format>
#include <random>
#include <string>
#include <string_view>

namespace
{
	template<typename P>
	std::string to_chars_string(const P value, auto... args)
	{
		std::array<char, 1024> buffer{};
		const auto result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), value, args...);
		EXPECT_EQ(result.ec, std::errc{});
		return std::string(buffer.data(), result.ptr);
	}

	std::string double_to_chars_string(const double value, auto... args)
	{
		std::array<char, 1024> buffer{};
		const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, args...);
		EXPECT_EQ(result.ec, std::errc{});
		return std::string(buffer.data(), result.ptr);
	}

	struct parsed
	{
		std::errc ec;
		std::size_t consumed;
		long long raw;
	};

	/// Parse with fpm::from_chars; `raw` is the raw value (or the untouched sentinel on error)
	template<typename P>
	parsed parse(const std::string_view text, const std::chars_format fmt = std::chars_format::general)
	{
		auto value = P::from_raw_value(42);
		const auto result = fpm::from_chars(text.data(), text.data() + text.size(), value, fmt);
		return {result.ec, static_cast<std::size_t>(result.ptr - text.data()), static_cast<long long>(value.raw_value())};
	}

	/// Random raw values, restricted to 53 significant bits so that they are exact as a `double`
	template<typename P>
	std::vector<P> sample_values(const int count)
	{
		using B = typename P::base_type;
		std::mt19937_64 rng(12345 + sizeof(B) * 100 + P::fraction_bits);
		std::vector<P> values{
			P::from_raw_value(0),
			P::from_raw_value(1),
			std::numeric_limits<P>::lowest(),
		};
		if constexpr(std::numeric_limits<B>::digits <= 53)
			values.push_back(std::numeric_limits<P>::max()); // otherwise not exact as a double
		if constexpr(std::is_signed_v<B>)
			values.push_back(P::from_raw_value(-1));

		constexpr int bits = std::numeric_limits<B>::digits;
		for(int i = 0; i < count; ++i)
		{
			const int width = 1 + static_cast<int>(rng() % std::min(bits, 53));
			const int shift = static_cast<int>(rng() % (bits - width + 1));
			auto raw = static_cast<B>(((rng() >> (64 - width)) | 1) << shift);
			if constexpr(std::is_signed_v<B>)
				if(rng() % 2)
					raw = static_cast<B>(-raw);
			values.push_back(P::from_raw_value(raw));
		}
		return values;
	}

#ifdef __SIZEOF_INT128__
	/// Exact expected raw value of M * 10^E for a fixed-point type, or `nullopt` if out of range.
	/// Uses 128-bit arithmetic, so M must be < 10^12 and E in [-25, 6].
	template<typename P>
	std::optional<long long> exact_raw(const unsigned long long mantissa, const int exponent10, const bool negative)
	{
		using U = unsigned __int128;
		using B = typename P::base_type;
		constexpr auto F = P::fraction_bits;

		U pow10 = 1;
		for(int i = 0; i < (exponent10 < 0 ? -exponent10 : exponent10); ++i)
			pow10 *= 10;

		U mag;
		if(exponent10 >= 0)
		{
			mag = (U{mantissa} * pow10) << F;
		}
		else
		{
			const U numerator = U{mantissa} << F;
			mag = numerator / pow10;
			const U remainder = numerator % pow10;
			if(P::enable_rounding && (remainder * 2 > pow10 || (remainder * 2 == pow10 && (mag & 1) != 0)))
				++mag;
		}

		U limit;
		if constexpr(std::is_signed_v<B>)
			limit = negative ? U{1} << std::numeric_limits<B>::digits : (U{1} << std::numeric_limits<B>::digits) - 1;
		else
			limit = negative ? 0 : U{std::numeric_limits<B>::max()};
		if(mag > limit)
			return std::nullopt;
		const auto m = static_cast<unsigned long long>(mag);
		return static_cast<long long>(negative ? 0 - m : m);
	}
#endif
}

template<typename T>
class charconv : public ::testing::Test
{
};

using CharconvTypes = ::testing::Types<
	fpm::fixed_4_4,
	fpm::fixed_8_8,
	fpm::fixed_16_16,
	fpm::fixed_24_8,
	fpm::fixed_8_24,
	fpm::fixed<std::int32_t, std::int64_t, 31>,        // single (sign) integral bit
	fpm::fixed<std::int32_t, std::int64_t, 16, false>, // no rounding
	fpm::fixed<std::uint16_t, std::uint32_t, 8>        // unsigned
#ifdef FPM_INT128
	,
	fpm::fixed_32_32,
	fpm::fixed_48_16,
	fpm::fixed_16_48,
	fpm::fixed_8_56
#endif
>;

TYPED_TEST_SUITE(charconv, CharconvTypes);

TYPED_TEST(charconv, to_chars_with_precision_matches_double)
{
	using P = TypeParam;
	constexpr std::array formats{
		std::chars_format::fixed,
		std::chars_format::scientific,
		std::chars_format::general,
		std::chars_format::hex,
	};
	constexpr std::array precisions{0, 1, 2, 3, 5, 6, 10, 17, 30, 70};

	for(const auto value : sample_values<P>(300))
	{
		const auto d = static_cast<double>(value);
		for(const auto fmt : formats)
		{
			for(const auto precision : precisions)
			{
				ASSERT_EQ(double_to_chars_string(d, fmt, precision), to_chars_string(value, fmt, precision))
					<< "raw=" << static_cast<long long>(value.raw_value()) << " fmt=" << static_cast<int>(fmt) << " precision=" << precision;
			}
		}
		// A negative precision means "as if omitted": 6, or exact for hex
		EXPECT_EQ(double_to_chars_string(d, std::chars_format::fixed, 6), to_chars_string(value, std::chars_format::fixed, -1));
		EXPECT_EQ(double_to_chars_string(d, std::chars_format::hex), to_chars_string(value, std::chars_format::hex, -1));
	}
}

TYPED_TEST(charconv, hex_matches_double)
{
	using P = TypeParam;
	for(const auto value : sample_values<P>(1000))
	{
		EXPECT_EQ(double_to_chars_string(static_cast<double>(value), std::chars_format::hex), to_chars_string(value, std::chars_format::hex))
			<< "raw=" << static_cast<long long>(value.raw_value());
	}
}

TYPED_TEST(charconv, shortest_round_trips)
{
	using P = TypeParam;
	constexpr std::array formats{
		std::chars_format::fixed,
		std::chars_format::scientific,
		std::chars_format::general,
		std::chars_format::hex,
	};

	for(const auto value : sample_values<P>(1000))
	{
		const auto raw = static_cast<long long>(value.raw_value());

		const auto plain = to_chars_string(value);
		const auto parsed_plain = parse<P>(plain);
		ASSERT_EQ(parsed_plain.ec, std::errc{}) << plain;
		ASSERT_EQ(parsed_plain.consumed, plain.size()) << plain;
		ASSERT_EQ(parsed_plain.raw, raw) << plain;

		for(const auto fmt : formats)
		{
			const auto text = to_chars_string(value, fmt);
			const auto result = parse<P>(text, fmt);
			ASSERT_EQ(result.ec, std::errc{}) << text;
			ASSERT_EQ(result.consumed, text.size()) << text;
			ASSERT_EQ(result.raw, raw) << text << " fmt=" << static_cast<int>(fmt);
		}
	}
}

TYPED_TEST(charconv, shortest_is_minimal)
{
	using P = TypeParam;
	if constexpr(!P::enable_rounding)
	{
		GTEST_SKIP() << "Minimality is checked against round-to-nearest candidates";
	}
	else
	{
		for(const auto value : sample_values<P>(1000))
		{
			// The significant digits of the scientific shortest form
			const auto text = to_chars_string(value, std::chars_format::scientific);
			const auto mantissa = text.substr(0, text.find('e'));
			const auto significant = static_cast<int>(std::count_if(mantissa.begin(), mantissa.end(), [](char c) { return c >= '0' && c <= '9'; }));
			if(significant <= 1)
				continue;

			// One digit less must not round-trip
			const auto shorter = to_chars_string(value, std::chars_format::scientific, significant - 2);
			const auto result = parse<P>(shorter);
			EXPECT_TRUE(result.ec != std::errc{} || result.raw != static_cast<long long>(value.raw_value()))
				<< text << " could be written as " << shorter;
		}
	}
}

#ifdef __SIZEOF_INT128__
TYPED_TEST(charconv, from_chars_is_exact)
{
	using P = TypeParam;
	using B = typename P::base_type;
	std::mt19937_64 rng(777 + P::fraction_bits);

	for(int i = 0; i < 5000; ++i)
	{
		unsigned long long modulus = 1;
		for(auto len = 1 + rng() % 12; len > 0; --len)
			modulus *= 10;
		const unsigned long long mantissa = rng() % modulus;
		const int exponent10 = static_cast<int>(rng() % 32) - 25;
		const bool negative = std::is_signed_v<B> && rng() % 2;

		// Write M * 10^E with a random decimal point position, leading zeros and exponent
		const auto digits = std::to_string(mantissa);
		const auto point = rng() % (digits.size() + 1);
		const int shown_exponent = exponent10 + static_cast<int>(digits.size() - point);
		const std::string text = (negative ? "-" : "") + std::string(rng() % 3, '0') + digits.substr(0, point) + "." +
			digits.substr(point) + "e" + std::to_string(shown_exponent);

		const auto expected = exact_raw<P>(mantissa, exponent10, negative);
		const auto result = parse<P>(text);
		ASSERT_EQ(result.consumed, text.size()) << text;
		if(expected)
		{
			ASSERT_EQ(result.ec, std::errc{}) << text;
			ASSERT_EQ(result.raw, static_cast<long long>(static_cast<B>(*expected))) << text;
		}
		else
		{
			ASSERT_EQ(result.ec, std::errc::result_out_of_range) << text;
			ASSERT_EQ(result.raw, 42) << text; // untouched
		}
	}
}
#endif

TEST(charconv, shortest_format_selection)
{
	using P = fpm::fixed_16_16;
#ifdef FPM_INT128
	using L = fpm::fixed_48_16;
#endif
	using S = fpm::fixed_8_24;

	// Plain: the shorter of fixed and scientific, fixed on ties (like double)
	EXPECT_EQ("0", to_chars_string(P(0)));
	EXPECT_EQ("1", to_chars_string(P(1)));
	EXPECT_EQ("-1.5", to_chars_string(P(-1.5)));
	EXPECT_EQ("30000", to_chars_string(P(30000)));
#ifdef FPM_INT128
	EXPECT_EQ("1e+05", to_chars_string(L(100000)));
#endif
#ifdef FPM_INT128
	EXPECT_EQ("123456", to_chars_string(L(123456)));
#endif
	EXPECT_EQ("1e-04", to_chars_string(S(0.0001)));
	EXPECT_EQ("0.00012", to_chars_string(S(0.00012)));

	// Fewest digits that round-trip, not the exact binary value
	EXPECT_EQ("0.1", to_chars_string(P(0.1)));
	EXPECT_EQ("3.14159", to_chars_string(P::pi()));
	EXPECT_EQ("3.1415927", to_chars_string(S::pi()));
	EXPECT_EQ("32767.99998", to_chars_string(std::numeric_limits<P>::max()));
	EXPECT_EQ("-32768", to_chars_string(std::numeric_limits<P>::lowest()));
	EXPECT_EQ("2e-05", to_chars_string(std::numeric_limits<P>::epsilon()));

	// General: fixed if the exponent is in [-4, 6), like double
#ifdef FPM_INT128
	EXPECT_EQ("100000", to_chars_string(L(100000), std::chars_format::general));
#endif
#ifdef FPM_INT128
	EXPECT_EQ("1e+06", to_chars_string(L(1000000), std::chars_format::general));
#endif
	EXPECT_EQ("0.0001", to_chars_string(S(0.0001), std::chars_format::general));
	EXPECT_EQ("1e-05", to_chars_string(S(0.00001), std::chars_format::general));
	EXPECT_EQ("0", to_chars_string(P(0), std::chars_format::general));

#ifdef FPM_INT128
	EXPECT_EQ("100000", to_chars_string(L(100000), std::chars_format::fixed));
#endif
	EXPECT_EQ("0.1", to_chars_string(P(0.1), std::chars_format::fixed));
	EXPECT_EQ("1e-01", to_chars_string(P(0.1), std::chars_format::scientific));
	EXPECT_EQ("0e+00", to_chars_string(P(0), std::chars_format::scientific));
	EXPECT_EQ("1.8p+0", to_chars_string(P(1.5), std::chars_format::hex));
	EXPECT_EQ("-1p-16", to_chars_string(-std::numeric_limits<P>::epsilon(), std::chars_format::hex));
	EXPECT_EQ("0p+0", to_chars_string(P(0), std::chars_format::hex));

	// Without rounding, the parser truncates, so the shortest form is rounded away from zero
	using T = fpm::fixed<std::int32_t, std::int64_t, 16, false>;
	EXPECT_EQ(6553, T(0.1).raw_value());
	EXPECT_EQ("0.1", to_chars_string(T(0.1)));
	EXPECT_EQ("-0.1", to_chars_string(T(-0.1)));
}

TEST(charconv, to_chars_buffer_too_small)
{
	using P = fpm::fixed_16_16;
	std::array<char, 8> buffer{};

	const auto fits = fpm::to_chars(buffer.data(), buffer.data() + 4, P(-1.5));
	EXPECT_EQ(fits.ec, std::errc{});
	EXPECT_EQ(std::string_view(buffer.data(), fits.ptr), "-1.5");

	const auto too_small = fpm::to_chars(buffer.data(), buffer.data() + 3, P(-1.5));
	EXPECT_EQ(too_small.ec, std::errc::value_too_large);
	EXPECT_EQ(too_small.ptr, buffer.data() + 3);

	EXPECT_EQ(fpm::to_chars(buffer.data(), buffer.data(), P(0)).ec, std::errc::value_too_large);
	EXPECT_EQ(fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), P(1), std::chars_format::fixed, 1'000'000).ec, std::errc::value_too_large);
	EXPECT_EQ(fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), P(1), std::chars_format::hex, 1'000'000).ec, std::errc::value_too_large);
}

TEST(charconv, from_chars_grammar)
{
	using P = fpm::fixed_16_16;
	constexpr auto raw = [](double v) { return static_cast<long long>(P(v).raw_value()); };
	const auto expect = [](const parsed& r, std::errc ec, std::size_t consumed, long long value, std::string_view text)
	{
		EXPECT_EQ(r.ec, ec) << text;
		EXPECT_EQ(r.consumed, consumed) << text;
		EXPECT_EQ(r.raw, value) << text;
	};
	constexpr auto ok = std::errc{};
	constexpr auto invalid = std::errc::invalid_argument;
	constexpr auto range = std::errc::result_out_of_range;
	constexpr long long untouched = 42;

	// Invalid inputs: `ptr` is `first` and the value is untouched
	for(const auto text : {"", "-", "+1", " 1", ".", "-.", "e5", "x", "--1", ".e1"})
		expect(parse<P>(text), invalid, 0, untouched, text);
	expect(parse<P>("0x", std::chars_format::hex), ok, 1, 0, "hex 0x");
	expect(parse<P>("0x"), ok, 1, 0, "0x");
	expect(parse<P>("g", std::chars_format::hex), invalid, 0, untouched, "hex g");

	// Valid prefixes
	expect(parse<P>("1"), ok, 1, raw(1), "1");
	expect(parse<P>("-0"), ok, 2, 0, "-0");
	expect(parse<P>("1.5"), ok, 3, raw(1.5), "1.5");
	expect(parse<P>(".5"), ok, 2, raw(0.5), ".5");
	expect(parse<P>("5."), ok, 2, raw(5), "5.");
	expect(parse<P>("1..5"), ok, 2, raw(1), "1..5");
	expect(parse<P>("1.5.3"), ok, 3, raw(1.5), "1.5.3");
	expect(parse<P>("0x10"), ok, 1, 0, "0x10");
	expect(parse<P>("1e"), ok, 1, raw(1), "1e");
	expect(parse<P>("1e+"), ok, 1, raw(1), "1e+");
	expect(parse<P>("1e2x"), ok, 3, raw(100), "1e2x");
	expect(parse<P>("1E-2"), ok, 4, raw(0.01), "1E-2");
	expect(parse<P>("00012.50"), ok, 8, raw(12.5), "00012.50");
	expect(parse<P>("1 2"), ok, 1, raw(1), "1 2");
	expect(parse<P>("1,5"), ok, 1, raw(1), "1,5"); // locale-independent

	// Format-dependent exponent handling
	expect(parse<P>("1.5e3", std::chars_format::fixed), ok, 3, raw(1.5), "fixed 1.5e3");
	expect(parse<P>("1.5e3", std::chars_format::scientific), ok, 5, raw(1500), "scientific 1.5e3");
	expect(parse<P>("1.5", std::chars_format::scientific), invalid, 0, untouched, "scientific 1.5");
	expect(parse<P>("1e", std::chars_format::scientific), invalid, 0, untouched, "scientific 1e");
	expect(parse<P>("1.8p1", std::chars_format::hex), ok, 5, raw(3), "hex 1.8p1");
	expect(parse<P>("1.8", std::chars_format::hex), ok, 3, raw(1.5), "hex 1.8");
	expect(parse<P>("-A.8P-1", std::chars_format::hex), ok, 7, raw(-5.25), "hex -A.8P-1");
	expect(parse<P>("1e1", std::chars_format::hex), ok, 3, raw(0x1e1), "hex 1e1");
	expect(parse<P>("1p", std::chars_format::hex), ok, 1, raw(1), "hex 1p");
	expect(parse<P>("0x10", std::chars_format::hex), ok, 1, 0, "hex 0x10");

	// Out of range: `ptr` is past the pattern and the value is untouched
	expect(parse<P>("32768"), range, 5, untouched, "32768");
	expect(parse<P>("-32769"), range, 6, untouched, "-32769");
	expect(parse<P>("32767.999993"), range, 12, untouched, "32767.999993"); // rounds up to 32768
	expect(parse<P>("32767.99999"), ok, 11, 0x7FFF'FFFF, "32767.99999"); // rounds down to max
	expect(parse<P>("1e5"), range, 3, untouched, "1e5");
	expect(parse<P>("1e999999999999999"), range, 17, untouched, "1e999999999999999");
	expect(parse<P>("8000p0", std::chars_format::hex), range, 6, untouched, "hex 8000p0");
	expect(parse<P>("1p100000000000", std::chars_format::hex), range, 14, untouched, "hex 1p100000000000");
	expect(parse<P>("inf"), range, 3, untouched, "inf");
	expect(parse<P>("-Infinity"), range, 9, untouched, "-Infinity");
	expect(parse<P>("infinit"), range, 3, untouched, "infinit");
	expect(parse<P>("NaN"), range, 3, untouched, "NaN");
	expect(parse<P>("nan(abc_1)x"), range, 10, untouched, "nan(abc_1)x");
	expect(parse<P>("nan(abc"), range, 3, untouched, "nan(abc");

	// Limits are exact
	expect(parse<P>("-32768"), ok, 6, raw(-32768), "-32768");
	expect(parse<P>("32767.99998"), ok, 11, 0x7FFF'FFFF, "32767.99998");
	expect(parse<P>("-0x8000p0", std::chars_format::hex), ok, 2, 0, "hex -0x8000p0"); // "-0", no prefix in hex
	expect(parse<P>("-8000p0", std::chars_format::hex), ok, 7, raw(-32768), "hex -8000p0");

	// Tiny values
	expect(parse<P>("1e-999999999999999"), ok, 18, 0, "1e-999999999999999");
	expect(parse<P>("1p-100000000000", std::chars_format::hex), ok, 15, 0, "hex 1p-100000000000");
	expect(parse<P>("0e999999999"), ok, 11, 0, "0e999999999");
}

TEST(charconv, from_chars_rounding)
{
	using P = fpm::fixed_16_16;
	using T = fpm::fixed<std::int32_t, std::int64_t, 16, false>;

	// 2^-17 is exactly half an epsilon: ties go to even
	EXPECT_EQ(0, parse<P>("0.00000762939453125").raw);
	EXPECT_EQ(2, parse<P>("0.00002288818359375").raw); // 1.5 epsilon
	EXPECT_EQ(-2, parse<P>("-0.00002288818359375").raw);

	// Anything beyond the tie rounds up, even far behind the significant digits
	EXPECT_EQ(1, parse<P>("0.0000076293945312500000000000000000000000000000000000000000000000000000000000000000000000000000000000001").raw);
	EXPECT_EQ(1, parse<P>("0.00000762939453125000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001").raw);
	EXPECT_EQ(0, parse<P>("0.00000762939453124999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999").raw);
	EXPECT_EQ(1, parse<P>("0.00000762939453125000000000000000000000000000000000000001e0").raw);
	EXPECT_EQ(1, parse<P>("0.0000800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001p0", std::chars_format::hex).raw);
	EXPECT_EQ(0, parse<P>("0.00008p0", std::chars_format::hex).raw);
	EXPECT_EQ(2, parse<P>("0.00018p0", std::chars_format::hex).raw);

	// Many digits before the point still overflow correctly
	EXPECT_EQ(std::errc::result_out_of_range, parse<P>("1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000").ec);
	EXPECT_EQ(std::errc{}, parse<P>("1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e-105").ec);
	EXPECT_EQ(fpm::fixed_16_16(1000).raw_value(), parse<P>("1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e-105").raw);

	// Truncation without rounding
	EXPECT_EQ(0, parse<T>("0.0000152587").raw);
	EXPECT_EQ(1, parse<T>("0.0000152588").raw);
	EXPECT_EQ(-1, parse<T>("-0.0000152588").raw);
	EXPECT_EQ(0x7FFF'FFFF, parse<T>("32767.999999").raw);
}

TEST(charconv, unsigned_types)
{
	using U = fpm::fixed<std::uint16_t, std::uint32_t, 8>;
	EXPECT_EQ(std::errc::result_out_of_range, parse<U>("-1").ec);
	EXPECT_EQ(std::errc{}, parse<U>("-0").ec);
	EXPECT_EQ(0xFFFF, parse<U>("255.99609375").raw);
	EXPECT_EQ(std::errc::result_out_of_range, parse<U>("256").ec);
	EXPECT_EQ("255.996", to_chars_string(std::numeric_limits<U>::max()));
}

namespace
{
	template<typename P>
	constexpr bool to_chars_equals(const P value, const std::string_view expected, auto... args)
	{
		std::array<char, 64> buffer{};
		const auto result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), value, args...);
		return result.ec == std::errc{} && std::string_view(buffer.data(), result.ptr) == expected;
	}

	template<typename P>
	constexpr P from_chars_value(const std::string_view text, const std::chars_format fmt = std::chars_format::general)
	{
		P value{};
		const auto result = fpm::from_chars(text.data(), text.data() + text.size(), value, fmt);
		return result ? value : P::from_raw_value(-1); // C++26: from_chars_result converts to bool
	}
}

TEST(charconv, constexpr_evaluation)
{
	using P = fpm::fixed_16_16;

	static_assert(to_chars_equals(P(1.5), "1.5"));
	static_assert(to_chars_equals(P::pi(), "3.14159"));
	static_assert(to_chars_equals(P::pi(), "3.1416", std::chars_format::fixed, 4));
	static_assert(to_chars_equals(P::pi(), "3.14159e+00", std::chars_format::scientific, 5));
	static_assert(to_chars_equals(P::pi(), "3.142", std::chars_format::general, 4));
	static_assert(to_chars_equals(P::pi(), "1.921f8p+1", std::chars_format::hex));

	static_assert(from_chars_value<P>("1.5") == P(1.5));
	static_assert(from_chars_value<P>("-2.25e1") == P(-22.5));
	static_assert(from_chars_value<P>("1.8p1", std::chars_format::hex) == P(3));
	static_assert(from_chars_value<P>("1e9") == P::from_raw_value(-1));

	static_assert(fpm::to_string(P(-0.25)) == "-0.25");
#ifdef FPM_INT128
	static_assert(fpm::to_string(fpm::fixed_8_56::pi()) == "3.14159265358979324");
#endif
}

TEST(charconv, to_string)
{
	using P = fpm::fixed_16_16;
	EXPECT_EQ("0", fpm::to_string(P(0)));
	EXPECT_EQ("123.0625", fpm::to_string(P(123.0625)));
	EXPECT_EQ("-0.1", fpm::to_string(P(-0.1)));
#ifdef FPM_INT128
	EXPECT_EQ("1e+05", fpm::to_string(fpm::fixed_48_16(100000)));
#endif

	// C++26 defines `std::to_string(floating-point)` as `std::format("{}", value)`
	for(const auto value : sample_values<P>(500))
		EXPECT_EQ(std::format("{}", value), fpm::to_string(value));

	// Found by argument-dependent lookup
	EXPECT_EQ("2.5", to_string(P(2.5)));
}

TEST(charconv, std_overloads)
{
	using P = fpm::fixed_16_16;
	std::array<char, 32> buffer{};

	// Qualified calls into `std` keep working
	auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), P(1.25));
	EXPECT_EQ("1.25", std::string_view(buffer.data(), result.ptr));
	result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), P(1.25), std::chars_format::scientific);
	EXPECT_EQ("1.25e+00", std::string_view(buffer.data(), result.ptr));
	result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), P(1.25), std::chars_format::fixed, 3);
	EXPECT_EQ("1.250", std::string_view(buffer.data(), result.ptr));

	P value{};
	const std::string_view text = "7.75";
	EXPECT_TRUE(std::from_chars(text.data(), text.data() + text.size(), value));
	EXPECT_EQ(P(7.75), value);

	// Unqualified calls with both `std` and `fpm` visible are not ambiguous
	using std::to_chars;
	using std::from_chars;
	result = to_chars(buffer.data(), buffer.data() + buffer.size(), P(-3));
	EXPECT_EQ("-3", std::string_view(buffer.data(), result.ptr));
	EXPECT_TRUE(from_chars(text.data(), text.data() + text.size(), value, std::chars_format::fixed));
}

TEST(charconv, format_default_is_shortest)
{
	using P = fpm::fixed_16_16;
	EXPECT_EQ("123.0625", std::format("{}", P(123.0625)));
	EXPECT_EQ("0.1", std::format("{}", P(0.1)));
	EXPECT_EQ("+0.1", std::format("{:+}", P(0.1)));
	EXPECT_EQ("  0.1", std::format("{:5}", P(0.1)));
	EXPECT_EQ(L"0.1", std::format(L"{}", P(0.1)));
	// With a precision, the general format of the stream operator is used
	EXPECT_EQ("0.100006", std::format("{:.6}", P(0.1)));
}
