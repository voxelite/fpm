// The module `fpm.fixed`: that `import fpm.fixed;` gives what the headers give (the names, the operators, the functions
// that are found by argument-dependent lookup and the specializations of the standard library). Only that they are
// there and work: the results themselves are the subject of the other tests.
#include <gtest/gtest.h>
#include <charconv>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

import fpm.fixed;

namespace
{
	using P = fpm::fixed_16_16;
}

TEST(module_fixed, types)
{
	static_assert(fpm::is_fixed_v<P> && fpm::Fixed<P>);
	static_assert(fpm::is_fixed<fpm::fixed_4_4>::value && fpm::is_fixed<fpm::fixed_8_8>::value);
	static_assert(fpm::is_fixed<fpm::fixed_8_24>::value && fpm::is_fixed<fpm::fixed_24_8>::value);
#ifdef FPM_INT128
	// (Where the code that imports the module has the macro as well, like with the CMake target)
	static_assert(fpm::is_fixed_v<fpm::fixed_32_32>);
	static_assert(sizeof(fpm::int128_t) > sizeof(int64_t));
#endif
}

TEST(module_fixed, operators)
{
	constexpr P x{3};
	constexpr P y{2};
	static_assert(x + y == P{5});
	static_assert(x - y == P{1});
	static_assert(-x == P{-3});
	static_assert(x * y == P{6});
	static_assert(x / y == P{1.5});
	static_assert(x + 1 == P{4} && 1 + x == P{4} && x * 2 == P{6} && 6 / x == P{2});
	static_assert(x > y && x != y && x == 3 && x < 4);
}

TEST(module_fixed, math)
{
	// Qualified, and unqualified (argument-dependent lookup)
	EXPECT_EQ(P{2}, fpm::sqrt(P{4}));
	EXPECT_EQ(P{2}, sqrt(P{4}));
	EXPECT_EQ(P{3}, abs(P{-3}));
	EXPECT_EQ(P{1}, floor(P{1.5}));
	EXPECT_EQ(P{2}, ceil(P{1.5}));
	EXPECT_EQ(P{8}, pow(P{2}, P{3}));
	EXPECT_EQ(P{0}, sin(P{0}));
	EXPECT_TRUE(isfinite(P{1}));
}

TEST(module_fixed, text)
{
	char buffer[32];
	const auto [end, ec] = fpm::to_chars(buffer, buffer + sizeof(buffer), P{1.5});
	ASSERT_EQ(std::errc{}, ec);
	EXPECT_EQ("1.5", std::string(buffer, end));

	P x;
	const std::string text = "-2.25";
	ASSERT_EQ(std::errc{}, fpm::from_chars(text.data(), text.data() + text.size(), x).ec);
	EXPECT_EQ(P{-2.25}, x);

	EXPECT_EQ("1.5", to_string(P{1.5}));

	std::ostringstream out;
	out << P{1.5};
	EXPECT_EQ("1.5", out.str());

	std::istringstream in("-1.25");
	in >> x;
	EXPECT_EQ(P{-1.25}, x);

	EXPECT_EQ("1.50", std::format("{:.2f}", P{1.5}));
	EXPECT_EQ(L"1.5", std::format(L"{}", fpm::fixed_24_8{1.5}));
}

TEST(module_fixed, standard_library)
{
	static_assert(std::numeric_limits<P>::max() == P::from_raw_value(std::numeric_limits<int32_t>::max()));
	static_assert(std::numeric_limits<fpm::fixed_8_8>::is_specialized);
	EXPECT_EQ(std::hash<P>{}(P{1.5}), std::hash<P>{}(P{1.5}));
	EXPECT_EQ(std::hash<fpm::fixed_8_8>{}(fpm::fixed_8_8{1}), std::hash<fpm::fixed_8_8>{}(fpm::fixed_8_8{1}));
}
