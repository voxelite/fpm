#include "common.hpp"
#include <fpm/fixed/ios.hpp>
#include <sstream>

using ::testing::Combine;
using ::testing::Values;

namespace
{
	class fake_numpunct : public std::numpunct<char>
	{
	public:
		fake_numpunct(
			const char decimal_point,
			const char thousands_sep,
			const std::string grouping
		)
			: m_decimal_point(decimal_point)
			, m_thousands_sep(thousands_sep)
			, m_grouping(grouping)
		{}

	protected:
		[[nodiscard]] inline char do_decimal_point() const override { return m_decimal_point; }
		[[nodiscard]] inline char do_thousands_sep() const override { return m_thousands_sep; }
		[[nodiscard]] inline std::string do_grouping() const override { return m_grouping; }
		[[nodiscard]] inline std::string do_truename() const override { return "unused"; }
		[[nodiscard]] inline std::string do_falsename() const override { return "unused"; }

	private:
		char m_decimal_point;
		char m_thousands_sep;
		std::string m_grouping;
	};

	class input : public ::testing::Test
	{
	protected:
		input() : m_locale("C")
		{
		}

		void setlocale(const std::locale locale)
		{
			m_locale = locale;
		}

		template<typename B, typename I, uint32_t F>
		void test_conversion(
			const std::string& text,
			fpm::fixed<B, I, F> expected,
			const std::string& expected_remaining = ""
		)
		{
			std::istringstream ss(text);
			ss.imbue(m_locale);

			fpm::fixed<B, I, F> value;
			ss >> value;

			// Values should match
			EXPECT_EQ(value, expected) << "for text: \"" << text << "\"";
			EXPECT_TRUE(ss);

			EXPECT_EQ(get_remainder(ss), expected_remaining) << "for text: \"" << text << "\"";
		}

		/// Values that are out of range: the nearest value is stored and the extraction fails,
		/// like for the built-in types
		template<typename B, typename I, uint32_t F>
		void test_out_of_range(
			const std::string& text,
			fpm::fixed<B, I, F> expected,
			const std::string& expected_remaining = ""
		)
		{
			std::istringstream ss(text);
			ss.imbue(m_locale);

			fpm::fixed<B, I, F> value{};
			ss >> value;

			EXPECT_EQ(value, expected) << "for text: \"" << text << "\"";
			EXPECT_TRUE(ss.fail()) << "for text: \"" << text << "\"";
			EXPECT_FALSE(ss.bad()) << "for text: \"" << text << "\"";

			EXPECT_EQ(get_remainder(ss), expected_remaining) << "for text: \"" << text << "\"";
		}

		void test_invalid_conversion(
			const std::string& text,
			const std::string& expected_remaining = ""
		)
		{
			std::istringstream ss(text);
			ss.imbue(m_locale);

			fpm::fixed_16_16 value;
			ss >> value;

			// Stream should have a parse error
			EXPECT_TRUE(ss.fail()) << "for text: \"" << text << "\"";;
			EXPECT_FALSE(ss.bad()) << "for text: \"" << text << "\"";;
			EXPECT_FALSE(ss.good()) << "for text: \"" << text << "\"";;

			EXPECT_EQ(get_remainder(ss), expected_remaining) << "for text: \"" << text << "\"";
		}

	private:
		static std::string get_remainder(std::istream& is)
		{
			std::string remaining;
			is.clear();
			is >> std::noskipws >> remaining;
			return remaining;
		}

		std::locale m_locale;
	};
}

TEST_F(input, integers)
{
	using P = fpm::fixed_16_16;
	test_conversion("0", P(0));
	test_conversion("+0", P(0));
	test_conversion("-0", P(0));

	test_conversion("1", P(1));
	test_conversion("+1", P(1));
	test_conversion("-1", P(-1));

	test_conversion("12795", P(12795));
	test_conversion("+12795", P(12795));
	test_conversion("-12795", P(-12795));
}

TEST_F(input, fixed_notation)
{
	using P = fpm::fixed_16_16;
	test_conversion("0.0", P(0));
	test_conversion("+0.0", P(0));
	test_conversion("-0.0", P(0));

	test_conversion("0.5", P(0.5));
	test_conversion("+0.5", P(0.5));
	test_conversion("-0.5", P(-0.5));

	test_conversion(".5", P(0.5));
	test_conversion("+.5", P(0.5));
	test_conversion("-.5", P(-0.5));

	test_conversion("3.", P(3));
	test_conversion("+3.", P(3));
	test_conversion("-3.", P(-3));

	test_conversion("1467.0125", P(1467.0125));
	test_conversion("+1467.0125", P(1467.0125));
	test_conversion("-1467.0125", P(-1467.0125));
}

TEST_F(input, scientific_notation)
{
	using P = fpm::fixed_16_16;
	test_conversion("0.e0", P(0));
	test_conversion("+0.e0", P(0));
	test_conversion("-0.e0", P(0));

	test_conversion(".5e1", P(5));
	test_conversion("+.5e1", P(5));
	test_conversion("-.5e1", P(-5));

	test_conversion("1.125e2", P(112.5));
	test_conversion("+1.125e2", P(112.5));
	test_conversion("-1.125e2", P(-112.5));

	test_conversion("1.125E+2", P(112.5));
	test_conversion("+1.125E+2", P(112.5));
	test_conversion("-1.125E+2", P(-112.5));

	test_conversion("9.765625e-4", P(0.0009765625));
	test_conversion("-9.765625e-4", P(-0.0009765625));
}

TEST_F(input, hexfloat_notation)
{
	using P = fpm::fixed_16_16;
	test_conversion("0x0", P(0));
	test_conversion("+0x0", P(0));
	test_conversion("-0x0", P(0));

	test_conversion("0x1a", P(26));
	test_conversion("+0x1a", P(26));
	test_conversion("-0x1a", P(-26));

	test_conversion("0xbcdP3", P(24168));
	test_conversion("+0xbcdP3", P(24168));
	test_conversion("-0xbcdP3", P(-24168));

	test_conversion("0x1.bcdP-3", P(0.217193603515625));
	test_conversion("+0x1.bcdP-3", P(0.217193603515625));
	test_conversion("-0x1.bcdP-3", P(-0.217193603515625));
}

TEST_F(input, decimal_point)
{
	using P = fpm::fixed_16_16;

	// Multiple decimal points stop parsing after the first one
	test_conversion("1..5", P(1), ".5");

   // Switch to a fake locale with a specific decimal separator
	setlocale(std::locale(std::locale(""), new fake_numpunct('\'', ',', "\001\002")));
	test_conversion("1\'234", P(1.234));
}

TEST_F(input, thousands_separator)
{
	using P = fpm::fixed_16_16;

	// Switch to a fake locale with an english thousands separator
	setlocale(std::locale(std::locale(""), new fake_numpunct('.', ',', "\003")));

	// Thousands groupings before decimal point are ignored
	test_conversion("12,345.67", P(12345.67));

	// Thousands groupings after decimal point stop parsing
	test_conversion("1.234,567", P(1.234), ",567");
	test_conversion("1.25,56", P(1.25), ",56");
	test_conversion("1.,125", P(1), ",125");

	// Switch to a fake locale with a specific thousands separator
	setlocale(std::locale(std::locale(""), new fake_numpunct('.', '\'', "\001\002")));
	test_conversion("12\'345.67", P(12345.67));
}

TEST_F(input, fails_on_badbit)
{
	using P = fpm::fixed_16_16;

	// operator>> is a FormattedInputFunction which is supposed to set failbit if badbit is set.
	std::istringstream ss("12345");
	ss.exceptions(std::ios::failbit);
	ss.setstate(std::ios::badbit);
	P x;
	EXPECT_THROW(ss >> x, std::ios::failure);
}

TEST_F(input, handles_eof)
{
	using P = fpm::fixed_16_16;

	// operator>> is a FormattedInputFunction which is supposed to handle EOF correctly
	// (in this case, by throwing an exception)
	std::istringstream ss("1");
	ss.exceptions(std::ios::eofbit);
	P x;
	EXPECT_THROW(ss >> x, std::ios::failure);
}

TEST_F(input, skips_whitespace)
{
	using P = fpm::fixed_16_16;
	test_conversion("   \t\r\n\v\f    1.125E+2", P(112.5));
}

TEST_F(input, ignored_remainder)
{
	using P = fpm::fixed_16_16;

	// Hexadecimal exponent in decimal number
	test_conversion("1.5p1", P(1.5), "p1");

	// Hexadecimal char in decimal exponent
	test_conversion("1e1a", P(10), "a");
	test_conversion("1e1f", P(10), "f");

	// Hexadecimal char in decimal number
	test_conversion("1a", P(1), "a");
	test_conversion("1f", P(1), "f");
}

TEST_F(input, incorrect_inputs)
{
	// Empty sequence
	test_invalid_conversion("");
	test_invalid_conversion("u1", "u1");

	// Decimal point by itself
	test_invalid_conversion(".");

	// Missing exponent value
	test_invalid_conversion("1e");
	test_invalid_conversion("1ee1", "e1");
	test_invalid_conversion("1ea", "a");
	test_invalid_conversion("1ef", "f");
	test_invalid_conversion("1e-e1", "e1");
	test_invalid_conversion("1e--1", "-1");
	test_invalid_conversion("1e++1", "+1");
	test_invalid_conversion("1e+-1", "-1");
	test_invalid_conversion("1e-+1", "+1");

	// Missing significand
	test_invalid_conversion("--1", "-1");
	test_invalid_conversion("++1", "+1");
	test_invalid_conversion("+-1", "-1");
	test_invalid_conversion("-+1", "+1");
}

TEST_F(input, overflow)
{
	using P = fpm::fixed_16_16;
	const auto max = std::numeric_limits<P>::max();
	const auto lowest = std::numeric_limits<P>::lowest();

	test_out_of_range("1e100", max);
	test_out_of_range("-1e100", lowest);

	test_out_of_range("100000", max);
	test_out_of_range("-100000", lowest);

	test_out_of_range("1000000000000000000000000000000000000000000000", max);
	test_out_of_range("-1000000000000000000000000000000000000000000000", lowest);
}

TEST_F(input, overflow_saturates)
{
	using P = fpm::fixed_16_16;
	const auto max = std::numeric_limits<P>::max();
	const auto lowest = std::numeric_limits<P>::lowest();

	// Just out of range: these used to wrap around
	test_out_of_range("32768", max);
	test_out_of_range("-32769", lowest);
	test_out_of_range("40000", max);
	test_out_of_range("4e4", max);
	test_out_of_range("32767.999993", max);  // rounds up to 32768
	test_out_of_range("-32768.000008", lowest);

	// Exactly in range
	test_conversion("-32768", lowest);
	test_conversion("32767.99998", max);
	test_conversion("32767.99999", max);   // rounds down to the maximum

	// Huge binary exponents (used to be undefined behavior)
	test_out_of_range("0x1p100", max);
	test_out_of_range("-0x1p100", lowest);
	test_conversion("0x1p-100", P(0));
	test_out_of_range("0x1p2147483647", max);

	// Huge decimal exponents are fast
	test_conversion("1e-2000000000", P(0));
	test_out_of_range("1e2000000000", max);
}

TEST_F(input, rounding)
{
	using P = fpm::fixed_16_16;
	// Exactly rounded to nearest, ties to even (like std::from_chars)
	test_conversion("0.00000762939453125", P(0));                              // 0.5 epsilon
	test_conversion("0.0000076293945312500001", P::from_raw_value(1));          // just over 0.5 epsilon
	test_conversion("0.00002288818359375", P::from_raw_value(2));              // 1.5 epsilon
	test_conversion("1.00001525878906250000000000000000001", P::from_raw_value(0x10001));
	test_conversion("123456789012345678901234567890e-26", P(1234.5678901234567));
}

TEST_F(input, case_insensitive_infinity)
{
	using P = fpm::fixed_16_16;
	test_out_of_range("INF", std::numeric_limits<P>::max());
	test_out_of_range("-Infinity", std::numeric_limits<P>::lowest());
}

TEST_F(input, non_ascii)
{
	// Characters outside of ASCII end the input (and are not undefined behavior in classification)
	test_invalid_conversion("\xE9", "\xE9");
	test_conversion("1.5\xE9", fpm::fixed_16_16(1.5), "\xE9");
}

TEST(input_types, round_trip)
{
	// Output with enough precision and reading back gives the same value, for many types
	const auto check = []<typename P>(P)
	{
		for(const double v : {0.0, 1.0, -1.0, 0.1, -3.14159, 100.25, 1.0 / 3})
		{
			const P x(v);
			std::stringstream ss;
			ss << std::setprecision(std::numeric_limits<P>::max_digits10) << x;
			P y{};
			ss >> y;
			EXPECT_EQ(x, y) << ss.str();
		}
	};
	check(fpm::fixed_8_8{});
	check(fpm::fixed_16_16{});
	check(fpm::fixed_24_8{});
	check(fpm::fixed_8_24{});
#ifdef FPM_INT128
	check(fpm::fixed_32_32{});
	check(fpm::fixed_48_16{});
	check(fpm::fixed_16_48{});
	check(fpm::fixed_8_56{});
#endif
}

TEST_F(input, infinity)
{
	using P = fpm::fixed_16_16;

	// Infinity cannot be represented: like a value that is out of range
	test_out_of_range("inf", std::numeric_limits<P>::max());
	test_out_of_range("infinity", std::numeric_limits<P>::max());

	test_out_of_range("-inf", std::numeric_limits<P>::lowest());
	test_out_of_range("-infinity", std::numeric_limits<P>::lowest());

	test_invalid_conversion("infinit", "");
	test_invalid_conversion("infini", "");
	test_invalid_conversion("infin", "");
	test_invalid_conversion("infi", "");
	test_invalid_conversion("in", "");
	test_invalid_conversion("i", "");

	test_out_of_range("infa", std::numeric_limits<P>::max(), "a");
	test_invalid_conversion("infinix", "x");
	test_invalid_conversion("ib", "b");
	test_invalid_conversion("-ic", "c");
}
