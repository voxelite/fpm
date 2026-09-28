#include "common.hpp"
#include <fpm/fixed/ios.hpp>
#include <fpm/fraction/ios.hpp>

#include <cmath>
#include <format>
#include <locale>
#include <random>
#include <sstream>
#include <string>

// `std::format` and the stream operators of fractions.
// The fixed-point type with the same fraction bits is the reference: it has the same values in [0, 1).

template<typename T>
class fraction_format : public ::testing::Test
{
};

using FractionTypes = ::testing::Types<
	fpm::fraction<uint8_t>,
	fpm::fraction<uint16_t>,
	fpm::fraction<uint32_t>,
	fpm::fraction<uint64_t>
>;

TYPED_TEST_SUITE(fraction_format, FractionTypes);

namespace
{
	// With FPM_FRACTION_STRICT the numbers that are converted must be in [0, 1)
#ifdef FPM_FRACTION_STRICT
	constexpr bool strict = true;
#else
	constexpr bool strict = false;
#endif

	template<typename P>
	std::vector<P> values(const int count = 300)
	{
		using B = typename P::base_type;
		constexpr B max = std::numeric_limits<B>::max();
		std::vector<P> result;
		for(const B raw : {B{0}, B{1}, B{2}, B{3}, static_cast<B>(max / 2), static_cast<B>(max / 2 + 1), static_cast<B>(max - 1), max})
			result.push_back(P::from_raw_value(raw));
		std::mt19937_64 rng(2000 + P::fraction_bits);
		for(int i = 0; i < count; ++i)
		{
			const int bits = 1 + static_cast<int>(rng() % P::fraction_bits);
			result.push_back(P::from_raw_value(static_cast<B>(rng() >> (64 - bits))));
		}
		return result;
	}

	/// The fixed-point type with the same fraction bits (if there is one)
	template<typename P>
	struct reference
	{
		using type = void;
	};
	template<>
	struct reference<fpm::fraction<uint8_t>>
	{
		using type = fpm::fixed_8_8;
	};
	template<>
	struct reference<fpm::fraction<uint16_t>>
	{
		using type = fpm::fixed_16_16;
	};
#ifdef FPM_INT128
	template<>
	struct reference<fpm::fraction<uint32_t>>
	{
		using type = fpm::fixed_32_32;
	};
#endif

	const char* const specifications[] = {
		"{}", "{:f}", "{:e}", "{:g}", "{:a}", "{:E}", "{:G}", "{:A}", "{:F}",
		"{:.0f}", "{:.1f}", "{:.3f}", "{:.10f}", "{:.40f}", "{:.80f}",
		"{:.0e}", "{:.2e}", "{:.25e}", "{:.0g}", "{:.4g}", "{:.30g}", "{:.0a}", "{:.3a}", "{:.20a}", "{:.5}",
		"{:12}", "{:<12}", "{:>12}", "{:^12}", "{:*<12.3f}", "{:_>12.3f}", "{:#^13.2e}", "{:012.4f}", "{:012}",
		"{:+}", "{:+.2f}", "{: .2f}", "{:-.2f}", "{:+012.3f}", "{: 012.3e}",
		"{:#}", "{:#.0f}", "{:#.0e}", "{:#.3g}", "{:#g}", "{:#a}", "{:#.0a}",
		"{:\u00e9^12.2f}",
	};

	std::wstring widen(const std::string_view text)
	{
		return std::wstring(text.begin(), text.end());
	}

	template<typename T>
	std::string stream(const T value, const auto... manipulators)
	{
		std::ostringstream os;
		os.imbue(std::locale::classic());
		(os << ... << manipulators) << value;
		EXPECT_TRUE(os.good());
		return os.str();
	}

	template<typename P>
	struct read_result
	{
		P value;
		bool failed;
		bool bad;
		std::string rest;
	};

	template<typename P>
	read_result<P> read(const std::string& text)
	{
		std::istringstream is(text);
		is.imbue(std::locale::classic());
		P value = P::from_raw_value(0x5A);
		is >> value;
		const bool failed = is.fail();
		const bool bad = is.bad();
		is.clear();
		std::string rest;
		is >> std::noskipws >> rest;
		return {value, failed, bad, rest};
	}
}

TYPED_TEST(fraction_format, examples)
{
	using P = TypeParam;

	EXPECT_EQ("0.5", std::format("{}", P{0.5}));
	EXPECT_EQ("0", std::format("{}", P{}));
	EXPECT_EQ("0.250000", std::format("{:f}", P{0.25}));
	EXPECT_EQ("0.750", std::format("{:.3f}", P{0.75}));
	EXPECT_EQ("7.5e-01", std::format("{:.1e}", P{0.75}));
	EXPECT_EQ("1.8p-1", std::format("{:a}", P{0.75}));
	EXPECT_EQ("1.8P-1", std::format("{:A}", P{0.75}));
	EXPECT_EQ("    0.5", std::format("{:7}", P{0.5}));
	EXPECT_EQ("0.5    ", std::format("{:<7}", P{0.5}));
	EXPECT_EQ("  0.5  ", std::format("{:^7}", P{0.5}));
	EXPECT_EQ("**0.125", std::format("{:*>7}", P{0.125}));
	EXPECT_EQ("00000.5", std::format("{:07}", P{0.5}));
	EXPECT_EQ("+0.5", std::format("{:+}", P{0.5}));
	EXPECT_EQ(" 0.5", std::format("{: }", P{0.5}));
	EXPECT_EQ("+0000.5", std::format("{:+07}", P{0.5}));
	EXPECT_EQ("0.", std::format("{:#.0f}", P{0.25}));
	EXPECT_EQ("  0.250", std::format("{:{}.{}f}", P{0.25}, 7, 3));
	EXPECT_EQ("x 0.75 y", std::format("x {} y", P{0.75}));
	EXPECT_EQ(L"0.75", std::format(L"{}", P{0.75}));
	EXPECT_EQ(L"  0.750", std::format(L"{:7.3f}", P{0.75}));

	// The digits are rounded: the largest fraction can give 1
	const auto last = P::from_raw_value(std::numeric_limits<typename P::base_type>::max());
	EXPECT_EQ("1.00", std::format("{:.2f}", last));
	EXPECT_EQ("1", std::format("{:.0f}", P{0.75}));

	EXPECT_THROW((void)std::vformat("{:L}", std::make_format_args(last)), std::format_error);
	EXPECT_THROW((void)std::vformat("{:d}", std::make_format_args(last)), std::format_error);
	EXPECT_THROW((void)std::vformat("{:.f}", std::make_format_args(last)), std::format_error);
	EXPECT_THROW((void)std::vformat("{:0.2f", std::make_format_args(last)), std::format_error);
}

TYPED_TEST(fraction_format, like_fixed)
{
	using P = TypeParam;
	using Q = typename reference<P>::type;
	if constexpr(std::is_void_v<Q>)
	{
		GTEST_SKIP() << "no fixed-point type with the same fraction bits";
	}
	else
	{
		for(const P x : values<P>())
		{
			const Q q(x);
			ASSERT_EQ(static_cast<uint64_t>(x.raw_value()), static_cast<uint64_t>(q.raw_value()));
			for(const char* const specification : specifications)
			{
				ASSERT_EQ(std::vformat(specification, std::make_format_args(q)), std::vformat(specification, std::make_format_args(x))) << specification;
				// (The fill character that is not ASCII is encoded for `char` only)
				const auto wide = widen(specification);
				if(std::string_view(specification).find('\xc3') == std::string_view::npos)
					ASSERT_EQ(std::vformat(wide, std::make_wformat_args(q)), std::vformat(wide, std::make_wformat_args(x))) << specification;
			}

			// Streams
			ASSERT_EQ(stream(q), stream(x));
			for(const int precision : {0, 1, 3, 6, 20, 70})
			{
				ASSERT_EQ(stream(q, std::setprecision(precision)), stream(x, std::setprecision(precision)));
				ASSERT_EQ(stream(q, std::fixed, std::setprecision(precision)), stream(x, std::fixed, std::setprecision(precision)));
				ASSERT_EQ(stream(q, std::scientific, std::setprecision(precision)), stream(x, std::scientific, std::setprecision(precision)));
				ASSERT_EQ(stream(q, std::showpoint, std::setprecision(precision)), stream(x, std::showpoint, std::setprecision(precision)));
			}
			ASSERT_EQ(stream(q, std::hexfloat), stream(x, std::hexfloat));
			ASSERT_EQ(stream(q, std::hexfloat, std::uppercase), stream(x, std::hexfloat, std::uppercase));
			ASSERT_EQ(stream(q, std::showpos, std::setw(14), std::setfill('#')), stream(x, std::showpos, std::setw(14), std::setfill('#')));
			ASSERT_EQ(stream(q, std::left, std::setw(14), std::setfill('#')), stream(x, std::left, std::setw(14), std::setfill('#')));
			ASSERT_EQ(stream(q, std::internal, std::showpos, std::setw(14)), stream(x, std::internal, std::showpos, std::setw(14)));
			ASSERT_EQ(stream(q, std::scientific, std::uppercase), stream(x, std::scientific, std::uppercase));
		}
	}
}

// Like `long double`, where it represents the values exactly
TYPED_TEST(fraction_format, like_floating_point)
{
	using P = TypeParam;
	if(std::numeric_limits<long double>::digits < 64)
		GTEST_SKIP() << "long double has less than 64 bits of precision";

	for(const P x : values<P>())
	{
		const long double value = std::ldexp(static_cast<long double>(x.raw_value()), -static_cast<int>(P::fraction_bits));
#if defined(_LIBCPP_VERSION)
		// libc++ formats a `long double` with the precision of a `double`, so it's only a reference for the values
		// that a `double` represents exactly
		if(static_cast<long double>(static_cast<double>(value)) != value)
			continue;
#endif
		for(const char* const specification : specifications)
		{
			// With a precision, or a type that implies one: not the shortest representation (which depends on the type),
			// nor the exact hexadecimal one (which is normalized differently for `long double`)
			const std::string_view view(specification);
			if(view.find_first_of("aA") != std::string_view::npos)
				continue;
			if(view.find('.') == std::string_view::npos && view.find_first_of("feEgGF") == std::string_view::npos)
				continue;
#if defined(_LIBCPP_VERSION)
			// libc++ shows one significant digit too few for "{:#g}" (e.g. "0.50" for "{:#.3g}" of 0.5, where
			// printf("%#.3g") and libstdc++ give "0.500"), so it's no reference for that case.
			if(view.find('#') != std::string_view::npos && view.find_first_of("feEaAF") == std::string_view::npos)
				continue;
#endif
			ASSERT_EQ(std::vformat(specification, std::make_format_args(value)), std::vformat(specification, std::make_format_args(x))) << specification;
		}

		for(const int precision : {0, 1, 3, 6, 20, 70})
		{
			ASSERT_EQ(stream(value, std::setprecision(precision)), stream(x, std::setprecision(precision)));
			ASSERT_EQ(stream(value, std::fixed, std::setprecision(precision)), stream(x, std::fixed, std::setprecision(precision)));
			ASSERT_EQ(stream(value, std::scientific, std::setprecision(precision)), stream(x, std::scientific, std::setprecision(precision)));
			ASSERT_EQ(stream(value, std::showpos, std::setw(30), std::setprecision(precision)), stream(x, std::showpos, std::setw(30), std::setprecision(precision)));
		}
	}
}

TYPED_TEST(fraction_format, output)
{
	using P = TypeParam;

	EXPECT_EQ("0.5", stream(P{0.5}));
	EXPECT_EQ("0", stream(P{}));
	EXPECT_EQ("0.750", stream(P{0.75}, std::fixed, std::setprecision(3)));
	EXPECT_EQ("7.50e-01", stream(P{0.75}, std::scientific, std::setprecision(2)));
	EXPECT_EQ("0x1.8p-1", stream(P{0.75}, std::hexfloat));
	EXPECT_EQ("+0.25", stream(P{0.25}, std::showpos));
	EXPECT_EQ("  0.25", stream(P{0.25}, std::setw(6)));
	EXPECT_EQ("0.25**", stream(P{0.25}, std::left, std::setw(6), std::setfill('*')));

	// The width applies to one value only
	std::ostringstream os;
	os << std::setw(6) << P{0.5} << P{0.25};
	EXPECT_EQ("   0.50.25", os.str());

	std::wostringstream wide;
	wide << std::fixed << std::setprecision(2) << P{0.75};
	EXPECT_EQ(L"0.75", wide.str());
}

TYPED_TEST(fraction_format, input)
{
	using P = TypeParam;
	using B = typename P::base_type;
	const auto last = P::from_raw_value(std::numeric_limits<B>::max());

	const auto expect = [](const std::string& text, const P value, const std::string& rest = "")
	{
		const auto result = read<P>(text);
		EXPECT_FALSE(result.failed) << text;
		EXPECT_FALSE(result.bad) << text;
		EXPECT_EQ(value, result.value) << text;
		EXPECT_EQ(rest, result.rest) << text;
	};
	// A number that is rejected: the nearest value is stored, and the extraction fails
	const auto expect_out_of_range = [](const std::string& text, const P value, const std::string& rest = "")
	{
		const auto result = read<P>(text);
		EXPECT_TRUE(result.failed) << text;
		EXPECT_FALSE(result.bad) << text;
		EXPECT_EQ(value, result.value) << text;
		EXPECT_EQ(rest, result.rest) << text;
	};
	const auto expect_invalid = [](const std::string& text)
	{
		const auto result = read<P>(text);
		EXPECT_TRUE(result.failed) << text;
		EXPECT_FALSE(result.bad) << text;
	};

	expect("0", P{});
	expect("0.5", P{0.5});
	expect("+0.5", P{0.5});
	expect("  0.25", P{0.25});
	expect(".75", P{0.75});
	expect("-0", P{});
	expect("-0.0", P{});
	expect("5e-1", P{0.5});
	expect("0.025E1", P{0.25});
	expect("0x1p-1", P{0.5});
	expect("0X0.Cp0", P{0.75});
	expect("0.5 0.25", P{0.5}, "");
	expect("0.5abc", P{0.5}, "abc");
	expect("0.5.25", P{0.5}, ".25");
	expect("0.9999999999999999999999999999999999999999", P{}); // rounds up to 1

	// Not in [0, 1): the fraction of the number (modulo 1), or rejected if that is checked
	const auto expect_wrapped = [&](const std::string& text, const P value, const P nearest, const std::string& rest = "")
	{
		if(strict)
			expect_out_of_range(text, nearest, rest);
		else
			expect(text, value, rest);
	};
	expect_wrapped("1", P{}, last);
	expect_wrapped("1.0", P{}, last);
	expect_wrapped("1.25", P{0.25}, last);
	expect_wrapped("+2.5", P{0.5}, last);
	expect_wrapped("1e100", P{}, last);
	expect_wrapped("0.525e1", P{0.25}, last);
	expect_wrapped("1275e-2", P{0.75}, last);
	expect_wrapped("0x1p0", P{}, last);
	expect_wrapped("0x3.4", P{0.25}, last);
	expect_wrapped("123456789012345678901234567890123456789012345678901234567890.625", P{0.625}, last);
	expect_wrapped("-0.25", P{0.75}, P{});
	expect_wrapped("-1", P{}, P{});
	expect_wrapped("-12.5", P{0.5}, P{});
	expect_wrapped("-1e-100", P{}, P{});
	expect_wrapped("1.5x", P{0.5}, last, "x");

	// Not a number
	expect_out_of_range("inf", last);
	expect_out_of_range("+infinity", last);
	expect_out_of_range("-inf", P{});

	expect_invalid("");
	expect_invalid("abc");
	expect_invalid("-");
	expect_invalid(".");
	expect_invalid("0.5e");
	expect_invalid("infi");

	// Several values
	std::istringstream is("0.5 0.25\n7.5e-1");
	P a{}, b{}, c{};
	is >> a >> b >> c;
	EXPECT_TRUE(is.eof());
	EXPECT_FALSE(is.fail());
	EXPECT_EQ(P{0.5}, a);
	EXPECT_EQ(P{0.25}, b);
	EXPECT_EQ(P{0.75}, c);

	// Every value is read back
	for(const P x : values<P>())
	{
		expect(stream(x, std::setprecision(80)), x);
		expect(stream(x, std::hexfloat), x);
		expect(std::format("{}", x), x);
		expect(std::format("{:.80e}", x), x);
		expect(std::format("{:a}", x).insert(0, "0x"), x);
	}
}

TYPED_TEST(fraction_format, locale)
{
	using P = TypeParam;

	struct punctuation : std::numpunct<char>
	{
		char do_decimal_point() const override { return ','; }
		char do_thousands_sep() const override { return '.'; }
		std::string do_grouping() const override { return "\3"; }
	};
	const std::locale locale(std::locale::classic(), new punctuation);

	std::ostringstream os;
	os.imbue(locale);
	os << std::fixed << std::setprecision(3) << P{0.75};
	EXPECT_EQ("0,750", os.str());

	std::istringstream is("0,25");
	is.imbue(locale);
	P value{};
	is >> value;
	EXPECT_FALSE(is.fail());
	EXPECT_EQ(P{0.25}, value);
}

// A large precision needs more room than the buffer on the stack has: also with the alternate form, which adds to it
TYPED_TEST(fraction_format, large_precision)
{
	using P = TypeParam;
	if(std::numeric_limits<long double>::digits < 64)
		GTEST_SKIP() << "long double has less than 64 bits of precision";

	for(const P x : values<P>(20))
	{
		const long double reference = std::ldexp(static_cast<long double>(x.raw_value()), -static_cast<int>(P::fraction_bits));
#if defined(_LIBCPP_VERSION)
		// (See above: libc++ formats with the precision of a `double`)
		if(static_cast<long double>(static_cast<double>(reference)) != reference)
			continue;
#endif
		for(const int precision : {0, 1, 40, 47, 48, 49, 60, 100, 127, 128, 129, 200, 1000, 5000})
		{
			for(const char* const specification : {"{:.{}f}", "{:.{}e}", "{:.{}g}", "{:.{}}", "{:#.{}f}", "{:#.{}e}", "{:#.{}g}", "{:#.{}}", "{:+#020.{}G}"})
			{
#if defined(_LIBCPP_VERSION)
				const std::string_view view(specification);
				if(view.find('#') != std::string_view::npos && view.find_first_of("feE") == std::string_view::npos)
					continue;
#endif
				ASSERT_EQ(std::vformat(specification, std::make_format_args(reference, precision)), std::vformat(specification, std::make_format_args(x, precision)))
					<< specification << " with " << precision;
			}
			ASSERT_EQ(stream(reference, std::setprecision(precision)), stream(x, std::setprecision(precision)));
			ASSERT_EQ(stream(reference, std::showpoint, std::setprecision(precision)), stream(x, std::showpoint, std::setprecision(precision)));
			ASSERT_EQ(stream(reference, std::fixed, std::showpoint, std::setprecision(precision)), stream(x, std::fixed, std::showpoint, std::setprecision(precision)));
			ASSERT_EQ(stream(reference, std::scientific, std::showpoint, std::setprecision(precision)), stream(x, std::scientific, std::showpoint, std::setprecision(precision)));
		}
	}
}

// The buffers have the size that a type needs, for the fractions with the most characters:
// with every notation and precision, the text is the one of `to_chars`
TYPED_TEST(fraction_format, buffers)
{
	using P = TypeParam;
	using B = typename P::base_type;
	const int digits = std::max<int>(static_cast<int>(P::fraction_bits), 6);

	const auto characters = [](const P value, const char type, const int precision)
	{
		std::string buffer(static_cast<std::size_t>(std::max(precision, 0)) + 200, '\0');
		char* const first = buffer.data();
		char* const last = first + buffer.size();
		std::to_chars_result result{};
		switch(type)
		{
			case 'f': result = fpm::to_chars(first, last, value, std::chars_format::fixed, precision < 0 ? 6 : precision); break;
			case 'e': result = fpm::to_chars(first, last, value, std::chars_format::scientific, precision < 0 ? 6 : precision); break;
			case 'g': result = fpm::to_chars(first, last, value, std::chars_format::general, precision < 0 ? 6 : precision); break;
			case 'a': result = fpm::to_chars(first, last, value, std::chars_format::hex, precision); break;
			default: result = (precision < 0) ? fpm::to_chars(first, last, value) : fpm::to_chars(first, last, value, std::chars_format::general, precision); break;
		}
		EXPECT_EQ(std::errc{}, result.ec);
		return std::string(first, result.ptr);
	};

	for(const B raw : {B{0}, B{1}, B{3}, std::numeric_limits<B>::max(), static_cast<B>(std::numeric_limits<B>::max() - 1), static_cast<B>(std::numeric_limits<B>::max() / 3), static_cast<B>(std::numeric_limits<B>::max() / 7 * 5)})
	{
		const P x = P::from_raw_value(raw);
		for(int precision = -1; precision <= digits + 40; ++precision)
		{
			for(const char type : {'\0', 'f', 'e', 'g', 'a'})
			{
				std::string specification = "{:";
				if(precision >= 0)
					specification += "." + std::to_string(precision);
				if(type != '\0')
					specification += type;
				specification += "}";
				const auto expected = characters(x, type, precision);
				ASSERT_EQ(expected, std::vformat(specification, std::make_format_args(x))) << specification;

				// The alternate form adds to the text, and other options are around it
				const auto alternate = std::vformat("{:#" + specification.substr(2), std::make_format_args(x));
				ASSERT_GE(alternate.size(), expected.size()) << specification;
				ASSERT_EQ(std::string(199 - alternate.size(), '*') + "+" + alternate, std::vformat("{:*>+#200" + specification.substr(2), std::make_format_args(x))) << specification;
			}

			if(precision >= 0)
			{
				ASSERT_EQ(characters(x, 'g', precision), stream(x, std::setprecision(precision)));
				ASSERT_EQ(characters(x, 'f', precision), stream(x, std::fixed, std::setprecision(precision)));
				ASSERT_EQ("+" + characters(x, 'e', precision), stream(x, std::scientific, std::showpos, std::setprecision(precision)));

				std::wostringstream wide;
				wide << std::fixed << std::showpos << std::setprecision(precision) << x;
				const auto text = "+" + characters(x, 'f', precision);
				ASSERT_EQ(std::wstring(text.begin(), text.end()), wide.str());
			}
		}
	}

	// As many characters as a type needs
	static_assert(fpm::detail::fraction_charconv::text_size<uint16_t>() == 24);
	static_assert(fpm::detail::fraction_charconv::text_size<uint8_t>() == 16);
	static_assert(fpm::detail::fraction_charconv::text_size<uint64_t>() == 72);
	static_assert(fpm::detail::fraction_charconv::text_size<uint16_t>(40) == 48);
}
