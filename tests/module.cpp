// The module `fpm`: that `import fpm;` gives what the headers give (the names, the operators, the functions that are
// found by argument-dependent lookup and the specializations of the standard library). Only that they are there and
// work: the results themselves are the subject of the other tests.
#include <gtest/gtest.h>
#include <charconv>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

import fpm;

namespace
{
	using P = fpm::fixed_16_16;
	using Q = fpm::fraction<uint16_t>;
}

TEST(module, types)
{
	static_assert(fpm::is_fixed_v<P> && fpm::Fixed<P>);
	static_assert(fpm::is_fraction_v<Q> && fpm::Fraction<Q>);
	static_assert(fpm::is_fixed<fpm::fixed_4_4>::value && fpm::is_fixed<fpm::fixed_8_8>::value);
	static_assert(fpm::is_fixed<fpm::fixed_8_24>::value && fpm::is_fixed<fpm::fixed_24_8>::value);
	static_assert(fpm::is_fraction<Q>::value);
#ifdef FPM_INT128
	// (Where the code that imports the module has the macro as well, like with the CMake target)
	static_assert(fpm::is_fixed_v<fpm::fixed_32_32>);
	static_assert(sizeof(fpm::int128_t) > sizeof(int64_t));
#endif
}

TEST(module, operators)
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

	constexpr Q a{0.25};
	constexpr Q b{0.5};
	static_assert(a + b == Q{0.75});
	static_assert(a - b == Q{0.75});
	static_assert(-a == Q{0.75});
	static_assert(a < b);
}

TEST(module, math)
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

	// Angles in turns
	EXPECT_EQ(fpm::fixed_8_24{1}, fpm::sin<fpm::fixed_8_24>(Q{0.25}));
	EXPECT_EQ(Q{0.125}, (fpm::atan2<Q>(P{1}, P{1})));
	const fpm::rotation<uint16_t> r = fpm::difference(Q{0.25}, Q{0.5});
	EXPECT_EQ(fpm::direction::Clockwise, r.direction);
	EXPECT_EQ(Q{0.25}, r.distance);
}

TEST(module, text)
{
	char buffer[32];
	const auto [end, ec] = fpm::to_chars(buffer, buffer + sizeof(buffer), P{1.5});
	ASSERT_EQ(std::errc{}, ec);
	EXPECT_EQ("1.5", std::string(buffer, end));

	P x;
	const std::string text = "-2.25";
	ASSERT_EQ(std::errc{}, fpm::from_chars(text.data(), text.data() + text.size(), x).ec);
	EXPECT_EQ(P{-2.25}, x);

	EXPECT_EQ("1.5", fpm::to_string(P{1.5}));
	EXPECT_EQ("0.25", to_string(Q{0.25}));

	std::ostringstream out;
	out << P{1.5} << ' ' << Q{0.25};
	EXPECT_EQ("1.5 0.25", out.str());

	std::istringstream in("0.75 -1.25");
	Q q;
	in >> q >> x;
	EXPECT_EQ(Q{0.75}, q);
	EXPECT_EQ(P{-1.25}, x);

	EXPECT_EQ("1.50|0.250", std::format("{:.2f}|{:.3f}", P{1.5}, Q{0.25}));
	EXPECT_EQ(L"1.5|0.5", std::format(L"{}|{}", fpm::fixed_24_8{1.5}, fpm::fraction<uint32_t>{0.5}));
}

TEST(module, standard_library)
{
	static_assert(std::numeric_limits<P>::is_specialized);
	static_assert(std::numeric_limits<P>::max() == P::from_raw_value(std::numeric_limits<int32_t>::max()));
	static_assert(std::numeric_limits<Q>::is_specialized);
	EXPECT_EQ(std::hash<P>{}(P{1.5}), std::hash<P>{}(P{1.5}));
	EXPECT_EQ(std::hash<Q>{}(Q{0.5}), std::hash<Q>{}(Q{0.5}));
	static_assert(std::numeric_limits<fpm::fixed_8_8>::is_specialized && std::numeric_limits<fpm::fraction<uint32_t>>::is_specialized);
	EXPECT_EQ(std::hash<fpm::fixed_8_8>{}(fpm::fixed_8_8{1}), std::hash<fpm::fixed_8_8>{}(fpm::fixed_8_8{1}));
	EXPECT_EQ(std::hash<fpm::fraction<uint32_t>>{}(fpm::fraction<uint32_t>{0.5}), std::hash<fpm::fraction<uint32_t>>{}(fpm::fraction<uint32_t>{0.5}));
}
