#include "common.hpp"
#include <fpm/fraction.hpp>

#include <cmath>
#include <random>
#include <vector>

// Conversions between the fixed-point types or fractions and the floating-point types of a fixed format: `float` and
// `double` (IEEE 754 binary32 and binary64, see common.hpp), and the ones of <stdfloat> that the platform has.
// To a floating-point type the result is the nearest number (so exact where the type represents the value, and infinite
// beyond its range). From one it is rounded or truncated like any other number, modulo the range (for a fraction: modulo 1).
// Against the reference type, where it has the bits of both types. The types of <stdfloat> that the compiler does not
// have are skipped.

namespace
{
	template<typename... Ts>
	struct list
	{
	};

	/// Stands for a type of <stdfloat> that the compiler does not have
	template<typename Name>
	struct missing
	{
		using name = Name;
	};

#ifdef __STDCPP_FLOAT16_T__
	using float16 = std::float16_t;
#else
	struct float16_name { static constexpr const char* value = "std::float16_t"; };
	using float16 = missing<float16_name>;
#endif
#ifdef __STDCPP_BFLOAT16_T__
	using bfloat16 = std::bfloat16_t;
#else
	struct bfloat16_name { static constexpr const char* value = "std::bfloat16_t"; };
	using bfloat16 = missing<bfloat16_name>;
#endif
#ifdef __STDCPP_FLOAT32_T__
	using float32 = std::float32_t;
#else
	struct float32_name { static constexpr const char* value = "std::float32_t"; };
	using float32 = missing<float32_name>;
#endif
#ifdef __STDCPP_FLOAT64_T__
	using float64 = std::float64_t;
#else
	struct float64_name { static constexpr const char* value = "std::float64_t"; };
	using float64 = missing<float64_name>;
#endif
#ifdef __STDCPP_FLOAT128_T__
	using float128 = std::float128_t;
#else
	struct float128_name { static constexpr const char* value = "std::float128_t"; };
	using float128 = missing<float128_name>;
#endif

	using fixed_types = list<
		fpm::fixed_4_4,
		fpm::fixed_8_8,
		fpm::fixed_16_16,
		fpm::fixed_24_8,
		fpm::fixed_8_24,
		fpm::fixed<int32_t, int64_t, 30>,
		fpm::fixed<int32_t, int64_t, 16, false>,
		fpm::fixed<uint16_t, uint32_t, 8>,
		fpm::fixed<uint32_t, uint64_t, 31, false>
#ifdef FPM_INT128
		,
		fpm::fixed_32_32,
		fpm::fixed_8_56,
		fpm::fixed<int64_t, FPM_INT128, 62>,
		fpm::fixed<int64_t, FPM_INT128, 1, false>
#endif
	>;

	using fraction_types = list<
		fpm::fraction<uint8_t>,
		fpm::fraction<uint16_t>,
		fpm::fraction<uint32_t>,
		fpm::fraction<uint64_t>
	>;

	/// Calls f<P>() for every type P of the list whose values the reference type represents exactly, like the ones of
	/// the floating-point type T
	template<typename T, typename F, typename... Ps>
	void for_each(list<Ps...>, F&& f)
	{
		((reference_has(std::numeric_limits<T>::digits) && reference_has(std::numeric_limits<std::make_unsigned_t<typename Ps::base_type>>::digits)
			? f.template operator()<Ps>() : void()), ...);
	}

	template<typename P>
	constexpr int fraction_bits = static_cast<int>(P::fraction_bits);

	/// The exact value of a raw value
	template<typename P>
	reference_t exact(const typename P::base_type raw)
	{
		return std::ldexp(static_cast<reference_t>(raw), -fraction_bits<P>);
	}

	/// The floating-point type in the messages: by its bits of precision
	template<typename T>
	std::string name()
	{
		return std::format("a floating-point type with {} bits", std::numeric_limits<T>::digits);
	}

	/// Raw values: the ends of the range, small ones, and random ones of every magnitude
	template<typename B>
	std::vector<B> raws(const unsigned seed, const int count = 1000)
	{
		constexpr B max = std::numeric_limits<B>::max();
		constexpr B lowest = std::numeric_limits<B>::lowest();
		std::vector<B> result{B{0}, B{1}, B{2}, B{3}, max, static_cast<B>(max - 1), lowest, static_cast<B>(lowest + 1),
			static_cast<B>(B{0} - B{1}), static_cast<B>(B{0} - B{2})};
		std::mt19937_64 rng(seed);
		constexpr int bits = static_cast<int>(sizeof(B) * 8);
		for(int i = 0; i < count; ++i)
		{
			const auto raw = static_cast<B>(rng() >> (64 - bits) >> (rng() % bits));
			result.push_back(raw);
			result.push_back(static_cast<B>(B{0} - raw));
		}
		return result;
	}

	/// The finite numbers of a floating-point type: the ends of its range, the ones around 2^integral and its multiples
	/// (the ends of the range of a fixed-point type), and random ones of every magnitude
	template<typename T>
	std::vector<T> numbers(const int integral, const unsigned seed, const int count = 1000)
	{
		using L = std::numeric_limits<T>;
		std::vector<reference_t> candidates{0, 1, -1, 0.5, -0.5, 0.25, -0.75, 1e9, -1e9, 1e30, -1e30,
			static_cast<reference_t>(L::max()), static_cast<reference_t>(L::lowest()), static_cast<reference_t>(L::min()),
			-static_cast<reference_t>(L::min()), static_cast<reference_t>(L::denorm_min()), -static_cast<reference_t>(L::denorm_min())};
		for(const int k : {-3, -2, -1, 1, 2, 3, 1000, -1000})
		{
			const reference_t end = std::ldexp(static_cast<reference_t>(k), integral);
			// With the neighbours in the floating-point type, and halfway to them
			int exponent = 0;
			static_cast<void>(std::frexp(end, &exponent));
			const reference_t unit = std::ldexp(reference_t{1}, exponent - L::digits);
			for(const reference_t offset : {reference_t{0}, unit, -unit, unit / 2, -unit / 2, reference_t{0.25}, reference_t{-0.25}, reference_t{0.5},
				reference_t{-0.5}, reference_t{1}, reference_t{-1}})
			{
				candidates.push_back(end + offset);
			}
		}
		std::mt19937_64 rng(seed);
		for(int i = 0; i < count; ++i)
		{
			const int exponent = static_cast<int>(rng() % 140) - 70;
			const reference_t mantissa = std::ldexp(static_cast<reference_t>(rng() >> 11), -53) + 1;
			candidates.push_back(std::ldexp((rng() % 2 == 0) ? mantissa : -mantissa, exponent));
		}

		std::vector<T> result;
		for(const reference_t candidate : candidates)
		{
			const auto number = static_cast<T>(candidate);
			if(number - number == T{0})
				result.push_back(number);
		}
		return result;
	}

	/// The raw value of a fraction for a number, with exact arithmetic: its fraction, rounded to nearest (ties upwards),
	/// modulo 1
	template<typename A>
	typename A::base_type expected_fraction(const reference_t value)
	{
		using B = typename A::base_type;
		const reference_t scaled = std::ldexp(value - std::floor(value), fraction_bits<A>); // in [0, 2^bits]
		reference_t whole = std::floor(scaled);
		if(scaled - whole >= reference_t{0.5})
			whole += 1;
		return static_cast<B>(static_cast<uint64_t>(std::fmod(whole, std::ldexp(reference_t{1}, fraction_bits<A>)))); // 2^bits is 0
	}
}

template<typename T>
class floating_types : public ::testing::Test
{
};

using FloatingTypes = ::testing::Types<float, double, float16, bfloat16, float32, float64, float128>;
TYPED_TEST_SUITE(floating_types, FloatingTypes);

TYPED_TEST(floating_types, fixed_to_floating)
{
	using T = TypeParam;
	if constexpr(!std::floating_point<T>)
	{
		GTEST_SKIP() << T::name::value << " does not exist here";
	}
	else
	{
		for_each<T>(fixed_types{}, []<typename P>()
		{
			using B = typename P::base_type;
			for(const B raw : raws<B>(100 + P::fraction_bits))
			{
				const T expected = static_cast<T>(exact<P>(raw)); // the nearest number
				const T result = static_cast<T>(P::from_raw_value(raw));
				ASSERT_TRUE(expected == result) << "the raw value " << +raw << " with " << fraction_bits<P> << " fraction bits to " << name<T>()
					<< ": " << text(static_cast<reference_t>(result)) << " instead of " << text(static_cast<reference_t>(expected));
			}
		});
	}
}

TYPED_TEST(floating_types, floating_to_fixed)
{
	using T = TypeParam;
	if constexpr(!std::floating_point<T>)
	{
		GTEST_SKIP() << T::name::value << " does not exist here";
	}
	else
	{
		for_each<T>(fixed_types{}, []<typename P>()
		{
			using B = typename P::base_type;
			constexpr int integral = std::numeric_limits<B>::digits - fraction_bits<P>;
			for(const T number : numbers<T>(integral, 200 + P::fraction_bits))
			{
				const auto value = static_cast<reference_t>(number);
				ASSERT_EQ(expected_raw<P>(value), P{number}.raw_value()) << text(value) << " of " << name<T>() << " with " << fraction_bits<P> << " fraction bits";
			}
		});
	}
}

TYPED_TEST(floating_types, fraction_to_floating)
{
	using T = TypeParam;
	if constexpr(!std::floating_point<T>)
	{
		GTEST_SKIP() << T::name::value << " does not exist here";
	}
	else
	{
		for_each<T>(fraction_types{}, []<typename A>()
		{
			using B = typename A::base_type;
			for(const B raw : raws<B>(300 + A::fraction_bits))
			{
				const T expected = static_cast<T>(exact<A>(raw)); // the nearest number: 1 for the fractions that are closer to it
				const T result = static_cast<T>(A::from_raw_value(raw));
				ASSERT_TRUE(expected == result) << "the raw value " << +raw << " of a fraction with " << fraction_bits<A> << " bits to " << name<T>()
					<< ": " << text(static_cast<reference_t>(result)) << " instead of " << text(static_cast<reference_t>(expected));
			}
		});
	}
}

TYPED_TEST(floating_types, floating_to_fraction)
{
	using T = TypeParam;
	if constexpr(!std::floating_point<T>)
	{
		GTEST_SKIP() << T::name::value << " does not exist here";
	}
	else
	{
		for_each<T>(fraction_types{}, []<typename A>()
		{
			for(const T number : numbers<T>(0, 400 + A::fraction_bits))
			{
				const auto value = static_cast<reference_t>(number);
	#ifdef FPM_FRACTION_STRICT
				if(!(value >= 0 && value < 1))
					continue; // (only the numbers in [0, 1))
	#endif
				ASSERT_EQ(expected_fraction<A>(value), A{number}.raw_value()) << text(value) << " of " << name<T>() << " to a fraction with " << fraction_bits<A> << " bits";
			}
		});
	}
}

// In constant expressions as well
#ifndef FPM_FRACTION_STRICT
static_assert(fpm::fraction<uint32_t>{-3.509121597744524478912353515625e-06f}.raw_value() == 4294952224u); // (1 - m is not exact in a float)
#endif
#ifdef __STDCPP_FLOAT16_T__
static_assert(static_cast<std::float16_t>(fpm::fixed_16_16{-1.5}) == static_cast<std::float16_t>(-1.5));
static_assert(static_cast<std::float16_t>(fpm::fixed_24_8::from_raw_value(65535)) == std::float16_t{256}); // 255.99609375, nearest
// Subnormal: 2.5 units of 2^-24 is a tie, to the even 2 units; just above it, 3 units
static_assert(static_cast<std::float16_t>(fpm::fraction<uint32_t>::from_raw_value(640)) == std::numeric_limits<std::float16_t>::denorm_min() * 2);
static_assert(static_cast<std::float16_t>(fpm::fraction<uint32_t>::from_raw_value(641)) == std::numeric_limits<std::float16_t>::denorm_min() * 3);
static_assert(fpm::fixed_16_16{static_cast<std::float16_t>(-2.25)} == fpm::fixed_16_16{-2.25});
static_assert(fpm::fraction<uint32_t>{static_cast<std::float16_t>(0.75)} == fpm::fraction<uint32_t>{0.75});
static_assert(static_cast<std::float16_t>(fpm::fraction<uint64_t>::from_raw_value(1)) == std::float16_t{0});
#endif
