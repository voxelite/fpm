#include "common.hpp"

template<typename T>
class constexprs : public ::testing::Test
{
};

using FixedTypes = ::testing::Types<
	// 16-bit
	fpm::fixed_8_8,
	// 32-bit
	fpm::fixed_16_16,
	fpm::fixed_24_8,
	fpm::fixed_8_24
#ifdef FPM_INT128
	,
	// 64-bit
	fpm::fixed_32_32,
	fpm::fixed_24_40,
	fpm::fixed_16_48,
	fpm::fixed_40_24,
	fpm::fixed_48_16
#endif
>;

TYPED_TEST_SUITE(constexprs, FixedTypes);

TYPED_TEST(constexprs, addition)
{
	using P = TypeParam;

	static_assert(P{0} + P{0} == P{0}, "Arithmetics failed");
	static_assert(P{1} + P{0} == P{1}, "Arithmetics failed");
	static_assert(P{1} + P{1} == P{2}, "Arithmetics failed");
	static_assert(P{1} + P{2} == P{3}, "Arithmetics failed");

	static_assert(P{0} + 0 == P{0}, "Arithmetics failed");
	static_assert(P{1} + 0 == P{1}, "Arithmetics failed");
	static_assert(P{1} + 1 == P{2}, "Arithmetics failed");
	static_assert(P{1} + 2 == P{3}, "Arithmetics failed");

	static_assert(0 + P{0} == P{0}, "Arithmetics failed");
	static_assert(1 + P{0} == P{1}, "Arithmetics failed");
	static_assert(1 + P{1} == P{2}, "Arithmetics failed");
	static_assert(1 + P{2} == P{3}, "Arithmetics failed");
}

TYPED_TEST(constexprs, subtraction)
{
	using P = TypeParam;

	static_assert(P{0} - P{0} == P{ 0}, "Arithmetics failed");
	static_assert(P{1} - P{0} == P{ 1}, "Arithmetics failed");
	static_assert(P{1} - P{1} == P{ 0}, "Arithmetics failed");
	static_assert(P{1} - P{2} == P{-1}, "Arithmetics failed");

	static_assert(P{0} - 0 == P{ 0}, "Arithmetics failed");
	static_assert(P{1} - 0 == P{ 1}, "Arithmetics failed");
	static_assert(P{1} - 1 == P{ 0}, "Arithmetics failed");
	static_assert(P{1} - 2 == P{-1}, "Arithmetics failed");

	static_assert(0 - P{0} == P{ 0}, "Arithmetics failed");
	static_assert(1 - P{0} == P{ 1}, "Arithmetics failed");
	static_assert(1 - P{1} == P{ 0}, "Arithmetics failed");
	static_assert(1 - P{2} == P{-1}, "Arithmetics failed");
}

TYPED_TEST(constexprs, multiplication)
{
	using P = TypeParam;

	static_assert(P{0} * P{0} == P{0}, "Arithmetics failed");
	static_assert(P{1} * P{0} == P{0}, "Arithmetics failed");
	static_assert(P{1} * P{1} == P{1}, "Arithmetics failed");
	static_assert(P{1} * P{2} == P{2}, "Arithmetics failed");

	static_assert(P{0} * 0 == P{0}, "Arithmetics failed");
	static_assert(P{1} * 0 == P{0}, "Arithmetics failed");
	static_assert(P{1} * 1 == P{1}, "Arithmetics failed");
	static_assert(P{1} * 2 == P{2}, "Arithmetics failed");

	static_assert(0 * P{0} == P{0}, "Arithmetics failed");
	static_assert(1 * P{0} == P{0}, "Arithmetics failed");
	static_assert(1 * P{1} == P{1}, "Arithmetics failed");
	static_assert(1 * P{2} == P{2}, "Arithmetics failed");
}

TYPED_TEST(constexprs, division)
{
	using P = TypeParam;

	static_assert(P{1} / P{1} == P{1  }, "Arithmetics failed");
	static_assert(P{1} / P{2} == P{0.5}, "Arithmetics failed");

	static_assert(P{1} / 1 == P{1  }, "Arithmetics failed");
	static_assert(P{1} / 2 == P{0.5}, "Arithmetics failed");

	static_assert(1 / P{1} == P{1  }, "Arithmetics failed");
	static_assert(1 / P{2} == P{0.5}, "Arithmetics failed");
}
