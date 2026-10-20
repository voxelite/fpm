// The module `fpm.fraction` on its own: that `import fpm.fraction;` gives what the headers give (see module_fixed.cpp).
#include <gtest/gtest.h>
#include <charconv>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

import fpm.fraction;

namespace
{
	using Q = fpm::fraction<uint16_t>;
}

TEST(module_fraction, types)
{
	static_assert(fpm::is_fraction_v<Q> && fpm::Fraction<Q>);
	static_assert(fpm::is_fraction<Q>::value);
}

TEST(module_fraction, operators)
{
	constexpr Q a{0.25};
	constexpr Q b{0.5};
	static_assert(a + b == Q{0.75});
	static_assert(a - b == Q{0.75});
	static_assert(-a == Q{0.75});
	static_assert(a * 3 == Q{0.75} && b / 2 == a);
	static_assert(a < b && a != b);
}

TEST(module_fraction, difference)
{
	const fpm::rotation<uint16_t> r = fpm::difference(Q{0.25}, Q{0.5});
	EXPECT_EQ(fpm::direction::Clockwise, r.direction);
	EXPECT_EQ(Q{0.25}, r.distance);
}

TEST(module_fraction, text)
{
	char buffer[32];
	const auto [end, ec] = fpm::to_chars(buffer, buffer + sizeof(buffer), Q{0.25});
	ASSERT_EQ(std::errc{}, ec);
	EXPECT_EQ("0.25", std::string(buffer, end));

	Q q;
	const std::string text = "0.375";
	ASSERT_EQ(std::errc{}, fpm::from_chars(text.data(), text.data() + text.size(), q).ec);
	EXPECT_EQ(Q{0.375}, q);

	EXPECT_EQ("0.25", to_string(Q{0.25}));

	std::ostringstream out;
	out << Q{0.25};
	EXPECT_EQ("0.25", out.str());

	std::istringstream in("0.75");
	in >> q;
	EXPECT_EQ(Q{0.75}, q);

	EXPECT_EQ("0.250", std::format("{:.3f}", Q{0.25}));
	EXPECT_EQ(L"0.5", std::format(L"{}", fpm::fraction<uint32_t>{0.5}));
}

TEST(module_fraction, standard_library)
{
	static_assert(std::numeric_limits<Q>::is_specialized && std::numeric_limits<fpm::fraction<uint32_t>>::is_specialized);
	EXPECT_EQ(std::hash<Q>{}(Q{0.5}), std::hash<Q>{}(Q{0.5}));
	EXPECT_EQ(std::hash<fpm::fraction<uint32_t>>{}(fpm::fraction<uint32_t>{0.5}), std::hash<fpm::fraction<uint32_t>>{}(fpm::fraction<uint32_t>{0.5}));
}
