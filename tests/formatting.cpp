#include <version>
#include <format>
#include <numbers>
#include <random>

#include "common.hpp"
#include <fpm/ios.hpp>

template<typename B, typename I, uint32_t F, bool R>
inline void ExpectFormat(
	const std::string_view format,
	const fpm::fixed<B, I, F, R> valFixed,
	const double valDouble,
	const std::string& expectedValue
)
{
	const auto resFixed = std::vformat(format, std::make_format_args(valFixed));
	const auto resDouble = std::vformat(format, std::make_format_args(valDouble));
	EXPECT_EQ(resFixed, expectedValue);
	EXPECT_EQ(resDouble, expectedValue);
	EXPECT_EQ(resFixed, resDouble);
}

TEST(formatting, basic)
{
	EXPECT_EQ(std::format("{}",    fpm::fixed_16_16{0}), std::string("0"));
	EXPECT_EQ(std::format("{0}",   fpm::fixed_16_16{0}), std::string("0"));
	EXPECT_EQ(std::format("{:+}",  fpm::fixed_16_16{0}), std::string("+0"));
	EXPECT_EQ(std::format("{0:+}", fpm::fixed_16_16{0}), std::string("+0"));

	EXPECT_EQ(std::format("{}",    fpm::fixed_16_16{1}), std::string("1"));
	EXPECT_EQ(std::format("{0}",   fpm::fixed_16_16{1}), std::string("1"));
	EXPECT_EQ(std::format("{:+}",  fpm::fixed_16_16{1}), std::string("+1"));
	EXPECT_EQ(std::format("{0:+}", fpm::fixed_16_16{1}), std::string("+1"));

	EXPECT_EQ(std::format("{}",   fpm::fixed_16_16{42}), std::string("42"));
	EXPECT_EQ(std::format("{:+}", fpm::fixed_16_16{42}), std::string("+42"));

	EXPECT_EQ(std::format("{}",   fpm::fixed_16_16{123}), std::string("123"));
	EXPECT_EQ(std::format("{:+}", fpm::fixed_16_16{123}), std::string("+123"));

	EXPECT_EQ(std::format("{}",   fpm::fixed_16_16{123.25}), std::string("123.25"));
	EXPECT_EQ(std::format("{:+}", fpm::fixed_16_16{123.25}), std::string("+123.25"));

	EXPECT_EQ(std::format("{}",   fpm::fixed_16_16{123.125}), std::string("123.125"));
	EXPECT_EQ(std::format("{:+}", fpm::fixed_16_16{123.125}), std::string("+123.125"));

	EXPECT_EQ(std::format("{}",   fpm::fixed_16_16{123.0625}), std::string("123.0625"));
	EXPECT_EQ(std::format("{:+}", fpm::fixed_16_16{123.0625}), std::string("+123.0625"));
}

TEST(formatting, width)
{
	EXPECT_EQ(std::format("{:0}", fpm::fixed_16_16{0}), std::string("0"));
	EXPECT_EQ(std::format("{:1}", fpm::fixed_16_16{0}), std::string("0"));
	EXPECT_EQ(std::format("{:2}", fpm::fixed_16_16{0}), std::string(" 0"));
	EXPECT_EQ(std::format("{:3}", fpm::fixed_16_16{0}), std::string("  0"));
	EXPECT_EQ(std::format("{:4}", fpm::fixed_16_16{0}), std::string("   0"));
	EXPECT_EQ(std::format("{:5}", fpm::fixed_16_16{0}), std::string("    0"));
	EXPECT_EQ(std::format("{:6}", fpm::fixed_16_16{0}), std::string("     0"));
}

TEST(formatting, width_nested)
{
	for(int precision = 0; precision < 10; precision++)
	{
		// (Nested arguments: std::vformat, as compile-time checked format strings with nested arguments
		// are rejected by some standard library implementations, even for double)
		const fpm::fixed_16_16 fixedZero{0};
		const double doubleZero = 0.0;
		const std::string strFixed  = std::vformat("{:{}}", std::make_format_args(fixedZero, precision));
		const std::string strDouble = std::vformat("{:{}}", std::make_format_args(doubleZero, precision));
		EXPECT_EQ(strFixed, strDouble);
	}
}

TEST(formatting, fill_and_align)
{
	ExpectFormat("{:6}", fpm::fixed_16_16{  0},   0.0, "     0");
	ExpectFormat("{:6}", fpm::fixed_16_16{  1},   1.0, "     1");
	ExpectFormat("{:6}", fpm::fixed_16_16{ 42},  42.0, "    42");
	ExpectFormat("{:6}", fpm::fixed_16_16{123}, 123.0, "   123");
	ExpectFormat("{:6}", fpm::fixed_16_16{ -1},  -1.0, "    -1");
	ExpectFormat("{:6}", fpm::fixed_16_16{-42}, -42.0, "   -42");

	ExpectFormat("{:*>6}", fpm::fixed_16_16{  0},   0.0, "*****0");
	ExpectFormat("{:*>6}", fpm::fixed_16_16{  1},   1.0, "*****1");
	ExpectFormat("{:*>6}", fpm::fixed_16_16{ 42},  42.0, "****42");
	ExpectFormat("{:*>6}", fpm::fixed_16_16{123}, 123.0, "***123");
	ExpectFormat("{:*>6}", fpm::fixed_16_16{ -1},  -1.0, "****-1");
	ExpectFormat("{:*>6}", fpm::fixed_16_16{-42}, -42.0, "***-42");

	ExpectFormat("{:x>6}", fpm::fixed_16_16{  0},   0.0, "xxxxx0");
	ExpectFormat("{:x>6}", fpm::fixed_16_16{  1},   1.0, "xxxxx1");
	ExpectFormat("{:x>6}", fpm::fixed_16_16{ 42},  42.0, "xxxx42");
	ExpectFormat("{:x>6}", fpm::fixed_16_16{123}, 123.0, "xxx123");
	ExpectFormat("{:x>6}", fpm::fixed_16_16{ -1},  -1.0, "xxxx-1");
	ExpectFormat("{:x>6}", fpm::fixed_16_16{-42}, -42.0, "xxx-42");

	ExpectFormat("{:+>6}", fpm::fixed_16_16{  0},   0.0, "+++++0");
	ExpectFormat("{:+>6}", fpm::fixed_16_16{  1},   1.0, "+++++1");
	ExpectFormat("{:+>6}", fpm::fixed_16_16{ 42},  42.0, "++++42");
	ExpectFormat("{:+>6}", fpm::fixed_16_16{123}, 123.0, "+++123");
	ExpectFormat("{:+>6}", fpm::fixed_16_16{ -1},  -1.0, "++++-1");
	ExpectFormat("{:+>6}", fpm::fixed_16_16{-42}, -42.0, "+++-42");

	ExpectFormat("{:>>6}", fpm::fixed_16_16{  0},   0.0, ">>>>>0");
	ExpectFormat("{:>>6}", fpm::fixed_16_16{  1},   1.0, ">>>>>1");
	ExpectFormat("{:>>6}", fpm::fixed_16_16{ 42},  42.0, ">>>>42");
	ExpectFormat("{:>>6}", fpm::fixed_16_16{123}, 123.0, ">>>123");
	ExpectFormat("{:>>6}", fpm::fixed_16_16{ -1},  -1.0, ">>>>-1");
	ExpectFormat("{:>>6}", fpm::fixed_16_16{-42}, -42.0, ">>>-42");

	ExpectFormat("{:0>6}", fpm::fixed_16_16{  0},   0.0, "000000");
	ExpectFormat("{:0>6}", fpm::fixed_16_16{  1},   1.0, "000001");
	ExpectFormat("{:0>6}", fpm::fixed_16_16{ 42},  42.0, "000042");
	ExpectFormat("{:0>6}", fpm::fixed_16_16{123}, 123.0, "000123");
	ExpectFormat("{:0>6}", fpm::fixed_16_16{ -1},  -1.0, "0000-1");
	ExpectFormat("{:0>6}", fpm::fixed_16_16{-42}, -42.0, "000-42");

	ExpectFormat("{:<6}", fpm::fixed_16_16{  0},   0.0, "0     ");
	ExpectFormat("{:<6}", fpm::fixed_16_16{  1},   1.0, "1     ");
	ExpectFormat("{:<6}", fpm::fixed_16_16{ 42},  42.0, "42    ");
	ExpectFormat("{:<6}", fpm::fixed_16_16{123}, 123.0, "123   ");
	ExpectFormat("{:<6}", fpm::fixed_16_16{ -1},  -1.0, "-1    ");
	ExpectFormat("{:<6}", fpm::fixed_16_16{-42}, -42.0, "-42   ");

	ExpectFormat("{:^6}", fpm::fixed_16_16{  0},   0.0, "  0   ");
	ExpectFormat("{:^6}", fpm::fixed_16_16{  1},   1.0, "  1   ");
	ExpectFormat("{:^6}", fpm::fixed_16_16{ 42},  42.0, "  42  ");
	ExpectFormat("{:^6}", fpm::fixed_16_16{123}, 123.0, " 123  ");
	ExpectFormat("{:^6}", fpm::fixed_16_16{ -1},  -1.0, "  -1  ");
	ExpectFormat("{:^6}", fpm::fixed_16_16{-42}, -42.0, " -42  ");

	ExpectFormat("{:^2}", fpm::fixed_16_16{123}, 123.0, "123");
	ExpectFormat("{:^2}", fpm::fixed_16_16{12345}, 12345.0, "12345");
	ExpectFormat("{:^4}", fpm::fixed_16_16{12345}, 12345.0, "12345");
}

TEST(formatting, sign)
{
	ExpectFormat("{0:},{0:+},{0:-},{0: }", fpm::fixed_16_16{ 1},  1.0, "1,+1,1, 1");
	ExpectFormat("{0:},{0:+},{0:-},{0: }", fpm::fixed_16_16{-1}, -1.0, "-1,-1,-1,-1");
}

TEST(formatting, zero_padding)
{
	ExpectFormat("{:02}", fpm::fixed_16_16{ 1},  1.0, "01");
	ExpectFormat("{:02}", fpm::fixed_16_16{-1}, -1.0, "-1");

	ExpectFormat("{:03}", fpm::fixed_16_16{ 1},  1.0, "001");
	ExpectFormat("{:03}", fpm::fixed_16_16{-1}, -1.0, "-01");

	ExpectFormat("{:04}", fpm::fixed_16_16{ 1},  1.0, "0001");
	ExpectFormat("{:04}", fpm::fixed_16_16{-1}, -1.0, "-001");

	ExpectFormat("{:06}", fpm::fixed_16_16{ 1},  1.0, "000001");
	ExpectFormat("{:06}", fpm::fixed_16_16{-1}, -1.0, "-00001");

	ExpectFormat("{:<02}", fpm::fixed_16_16{ 1},  1.0, "1 ");
	ExpectFormat("{:<02}", fpm::fixed_16_16{-1}, -1.0, "-1");

	ExpectFormat("{:<06}", fpm::fixed_16_16{ 1},  1.0, "1     ");
	ExpectFormat("{:<06}", fpm::fixed_16_16{-1}, -1.0, "-1    ");
}

TEST(formatting, precision)
{
	const double dblValue = 2.015625;
	const auto fpmValue = fpm::fixed_16_16(2.015625);

	ExpectFormat("{:10f}",   fpmValue, dblValue, "  2.015625");
	ExpectFormat("{:.5f}",   fpmValue, dblValue, "2.01562");
	ExpectFormat("{:10.5f}", fpmValue, dblValue, "   2.01562");
	ExpectFormat("{:10.6f}", fpmValue, dblValue, "  2.015625");
	ExpectFormat("{:10.3f}", fpmValue, dblValue, "     2.016");
	ExpectFormat("{:10.2f}", fpmValue, dblValue, "      2.02");
}

TEST(formatting, precision_pi)
{
	const double dblValue = std::numbers::pi_v<double>;
	const auto fpmValue = fpm::fixed_8_24::pi(); // fpm::fixed_16_16 does not have enough precision

	ExpectFormat("{:10f}",   fpmValue, dblValue, "  3.141593");
	ExpectFormat("{:.5f}",   fpmValue, dblValue, "3.14159");

	ExpectFormat("{:10.6f}", fpmValue, dblValue, "  3.141593");
	ExpectFormat("{:10.5f}", fpmValue, dblValue, "   3.14159");
	ExpectFormat("{:10.4f}", fpmValue, dblValue, "    3.1416");
	ExpectFormat("{:10.3f}", fpmValue, dblValue, "     3.142");
	ExpectFormat("{:10.2f}", fpmValue, dblValue, "      3.14");
	ExpectFormat("{:10.1f}", fpmValue, dblValue, "       3.1");
	ExpectFormat("{:10.0f}", fpmValue, dblValue, "         3");
}

TEST(formatting, precision_nested)
{
	const double dblValue = 2.015625;
	const auto fpmValue = fpm::fixed_16_16(2.015625);

	for(int precision = 0; precision < 10; precision++)
	{
		const std::string strFixed  = std::vformat("{:2.{}f}", std::make_format_args(fpmValue, precision));
		const std::string strDouble = std::vformat("{:2.{}f}", std::make_format_args(dblValue, precision));
		EXPECT_EQ(strFixed, strDouble);
	}

	for(int precision = 0; precision < 10; precision++)
	{
		for(int width = 0; width < precision + 2; width++)
		{
			const std::string strFixed  = std::vformat("{:{}.{}f}", std::make_format_args(fpmValue, width, precision));
			const std::string strDouble = std::vformat("{:{}.{}f}", std::make_format_args(dblValue, width, precision));
			EXPECT_EQ(strFixed, strDouble);
		}

		for(int width = 0; width < precision + 4; width++)
		{
			const std::string strFixed  = std::vformat("{:{}.{}f}", std::make_format_args(fpmValue, width, precision));
			const std::string strDouble = std::vformat("{:{}.{}f}", std::make_format_args(dblValue, width, precision));
			EXPECT_EQ(strFixed, strDouble);
		}
	}
}

TEST(formatting, types)
{
	const double dblValue = 2.015625;
	const auto fpmValue = fpm::fixed_16_16(2.015625);

	// Fixed
	{
		ExpectFormat("{:10.6f}", fpmValue, dblValue, "  2.015625");
		ExpectFormat("{:10.6F}", fpmValue, dblValue, "  2.015625");

		ExpectFormat("{:10.3f}", fpmValue, dblValue, "     2.016");
		ExpectFormat("{:10.3F}", fpmValue, dblValue, "     2.016");
	}
	// Hex
	{
		ExpectFormat("{:10.6a}", fpmValue, dblValue, "1.020000p+1");
		ExpectFormat("{:10.6A}", fpmValue, dblValue, "1.020000P+1");
		ExpectFormat("{:a}", fpmValue, dblValue, "1.02p+1");
	}
	// Scientific
	{
		ExpectFormat("{:10.6e}", fpmValue, dblValue, "2.015625e+00");
		ExpectFormat("{:10.6E}", fpmValue, dblValue, "2.015625E+00");

		ExpectFormat("{:10.3e}", fpmValue, dblValue, " 2.016e+00");
		ExpectFormat("{:10.3E}", fpmValue, dblValue, " 2.016E+00");
	}
	// General
	{
		ExpectFormat("{:10.6g}", fpmValue, dblValue, "   2.01562");
		ExpectFormat("{:10.6G}", fpmValue, dblValue, "   2.01562");

		ExpectFormat("{:10.3g}", fpmValue, dblValue, "      2.02");
		ExpectFormat("{:10.3G}", fpmValue, dblValue, "      2.02");
	}
}

namespace
{
	template<typename P>
	std::vector<P> exact_values()
	{
		// At most 53 significant bits, so the values are exact as a double
		using B = typename P::base_type;
		std::mt19937_64 rng(11);
		std::vector<P> values{P(0), P(1), P(-1), P(0.5), P(-2.25), P(100)};
		for(int i = 0; i < 20; ++i)
		{
			const int bits = 1 + static_cast<int>(rng() % std::min(53, std::numeric_limits<B>::digits));
			auto raw = static_cast<B>(rng() >> (64 - bits));
			if(rng() % 2)
				raw = static_cast<B>(-raw);
			values.push_back(P::from_raw_value(raw));
		}
		return values;
	}

	template<typename P>
	void expect_format_like_double(const std::string& spec)
	{
		for(const auto value : exact_values<P>())
		{
			const auto d = static_cast<double>(value);
			std::string expected;
			std::string actual;
			try { expected = std::vformat(spec, std::make_format_args(d)); } catch(const std::format_error&) { expected = "<error>"; }
			try { actual = std::vformat(spec, std::make_format_args(value)); } catch(const std::format_error&) { actual = "<error>"; }
			ASSERT_EQ(expected, actual) << "format \"" << spec << "\" of " << d;
		}
	}
}

TEST(formatting, specifications_like_double)
{
	// A random selection of all combinations of fill/align, sign, '#', '0', width, precision and type.
	// Without type and precision, fixed-point numbers are printed shortest for their own precision, so skip those.
	const std::vector<std::string> aligns{"", "<", ">", "^", "*<", "*>", "*^", "0>"};
	const std::vector<std::string> signs{"", "+", "-", " "};
	const std::vector<std::string> alternates{"", "#"};
	const std::vector<std::string> zeros{"", "0"};
	const std::vector<std::string> widths{"", "1", "8", "25"};
	const std::vector<std::string> precisions{"", ".0", ".1", ".3", ".6", ".12", ".20"};
	const std::vector<std::string> types{"", "a", "A", "e", "E", "f", "F", "g", "G"};

	std::mt19937 rng(3);
	const auto pick = [&](const std::vector<std::string>& v) { return v[rng() % v.size()]; };
	for(int i = 0; i < 1500; ++i)
	{
		const auto precision = pick(precisions);
		const auto type = pick(types);
		if(type.empty() && precision.empty())
			continue;
		const auto alternate = pick(alternates);
#if defined(_LIBCPP_VERSION)
		// libc++ shows one significant digit too few for "{:#g}" (e.g. "0.50000" for "{:#.6g}" of 0.5, where
		// printf("%#.6g") and libstdc++ give "0.500000"), so it's no reference for that case.
		if(!alternate.empty() && (type == "g" || type == "G" || type.empty()))
			continue;
#endif
		const auto spec = "{:" + pick(aligns) + pick(signs) + alternate + pick(zeros) + pick(widths) + precision + type + "}";
		expect_format_like_double<fpm::fixed_16_16>(spec);
		expect_format_like_double<fpm::fixed_8_24>(spec);
		expect_format_like_double<fpm::fixed_24_8>(spec);
#ifdef FPM_INT128
		expect_format_like_double<fpm::fixed_32_32>(spec);
		expect_format_like_double<fpm::fixed_48_16>(spec);
#endif
	}
}

TEST(formatting, shortest_with_options)
{
	// Without type and precision: shortest representation, with the other options applied
	using P = fpm::fixed_16_16;
	EXPECT_EQ("0.1", std::format("{}", P(0.1)));
	EXPECT_EQ("+0.1", std::format("{:+}", P(0.1)));
	EXPECT_EQ(" 0.1", std::format("{: }", P(0.1)));
	EXPECT_EQ("-0.1", std::format("{: }", P(-0.1)));
	EXPECT_EQ("  0.1", std::format("{:5}", P(0.1)));
	EXPECT_EQ("000.1", std::format("{:05}", P(0.1)));
	EXPECT_EQ("-00.1", std::format("{:05}", P(-0.1)));
	EXPECT_EQ(" 0001", std::format("{: 05}", P(1)));
	EXPECT_EQ("1.", std::format("{:#}", P(1)));
#ifdef FPM_INT128
	EXPECT_EQ("1.e+05", std::format("{:#}", fpm::fixed_48_16(100000)));
#endif
	EXPECT_EQ("0.1**", std::format("{:*<5}", P(0.1)));
	EXPECT_EQ("    1", std::format("{:>05}", P(1))); // explicit alignment disables zero padding
}

TEST(formatting, nested_width_and_precision)
{
	// (std::vformat: compile-time checked format strings with nested arguments are rejected by some
	// standard library implementations, even for double)
	const auto fixed = fpm::fixed_16_16(2.015625);
	const double dbl = 2.015625;
	for(int width = 1; width < 13; ++width)
	{
		for(int precision = 0; precision < 8; ++precision)
		{
			EXPECT_EQ(std::vformat("{:{}.{}f}", std::make_format_args(dbl, width, precision)), std::vformat("{:{}.{}f}", std::make_format_args(fixed, width, precision)));
			EXPECT_EQ(std::vformat("{0:{1}.{2}e}", std::make_format_args(dbl, width, precision)), std::vformat("{0:{1}.{2}e}", std::make_format_args(fixed, width, precision)));
		}
		EXPECT_EQ(std::vformat("{:*^{}g}", std::make_format_args(dbl, width)), std::vformat("{:*^{}g}", std::make_format_args(fixed, width)));
	}
	EXPECT_THROW(static_cast<void>(std::vformat("{:{}}", std::make_format_args(fixed, fixed))), std::format_error);
	int negative = -1;
	EXPECT_THROW(static_cast<void>(std::vformat("{:.{}}", std::make_format_args(fixed, negative))), std::format_error);
}

TEST(formatting, unicode_fill)
{
	const auto value = fpm::fixed_16_16(1.5);
	EXPECT_EQ(std::format("{:é^9}", 1.5), std::format("{:é^9}", value));
	EXPECT_EQ(std::format("{:€>7}", 1.5), std::format("{:€>7}", value));
}

TEST(formatting, locale_independent)
{
	struct comma : std::numpunct<char>
	{
		char do_decimal_point() const override { return ','; }
		char do_thousands_sep() const override { return '.'; }
		std::string do_grouping() const override { return "\3"; }
	};
	const auto previous = std::locale::global(std::locale(std::locale::classic(), new comma));
	const auto fixed = std::format("{0:.3f} {0} {0:e}", fpm::fixed_16_16(1234.5));
	std::locale::global(previous);
	EXPECT_EQ("1234.500 1234.5 1.234500e+03", fixed);
}

TEST(formatting, invalid_specifications)
{
	const auto value = fpm::fixed_16_16(1);
	for(const auto spec : {"{:d}", "{:x}", "{:L}", "{:.}", "{:.f}", "{:s}", "{:00}"})
		EXPECT_THROW(static_cast<void>(std::vformat(spec, std::make_format_args(value))), std::format_error) << spec;
}
