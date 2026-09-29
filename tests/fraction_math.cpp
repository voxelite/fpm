#include "common.hpp"
#include <fpm/fixed/math.hpp>
#include <fpm/fraction/math.hpp>

#include <cmath>
#include <random>

// Trigonometry with angles as fractions of a turn, against `long double` references.
// Every combination of a fraction and a fixed-point type.

namespace
{
	// (Not from <numbers>: libc++ has the constants with the precision of a `double` only)
	constexpr long double two_pi = 6.283185307179586476925286766559005768L;

	template<typename... Ts>
	struct list
	{
	};

	using fractions = list<
		fpm::fraction<uint8_t>,
		fpm::fraction<uint16_t>,
		fpm::fraction<uint32_t>,
		fpm::fraction<uint64_t>
	>;

	using numbers = list<
		fpm::fixed_4_4,
		fpm::fixed_8_8,
		fpm::fixed_16_16,
		fpm::fixed_24_8,
		fpm::fixed_8_24,
		fpm::fixed<int32_t, int64_t, 30>,
		fpm::fixed<int32_t, int64_t, 16, false>
#ifdef FPM_INT128
		,
		fpm::fixed_32_32,
		fpm::fixed_48_16,
		fpm::fixed_16_48,
		fpm::fixed_8_56
#endif
	>;

	/// Calls f<A, Q>() for every fraction A and fixed-point type Q
	template<typename F, typename... As, typename... Qs>
	void for_each_pair(list<As...>, list<Qs...>, F&& f)
	{
		const auto with = [&]<typename A>()
		{
			(f.template operator()<A, Qs>(), ...);
		};
		(with.template operator()<As>(), ...);
	}

	template<typename A>
	constexpr int bits = static_cast<int>(A::fraction_bits);

	/// The angle in turns, exactly
	template<typename A>
	long double turns(const A x)
	{
		return std::ldexp(static_cast<long double>(x.raw_value()), -bits<A>);
	}

	template<typename Q>
	long double ld(const Q x)
	{
		return std::ldexp(static_cast<long double>(x.raw_value()), -static_cast<int>(Q::fraction_bits));
	}

	template<typename Q>
	const long double eps = std::ldexp(1.0L, -static_cast<int>(Q::fraction_bits));

	template<typename A>
	const long double unit = std::ldexp(1.0L, -bits<A>);

	/// The resolution of the polynomial evaluations: M = digits - 1 fraction bits (see fpm::detail::poly_bits)
	template<typename Q>
	const long double poly_eps = std::ldexp(1.0L, 1 - std::numeric_limits<std::make_signed_t<typename Q::base_type>>::digits);

	/// Angles over the whole turn: the quadrants and octants and their neighbours, and random ones of every magnitude
	template<typename A>
	std::vector<A> angles(const int count = 1500)
	{
		using B = typename A::base_type;
		std::vector<A> result;
		for(int octant = 0; octant < 8; ++octant)
		{
			const auto raw = static_cast<B>(static_cast<B>(octant) << (bits<A> - 3));
			for(const int offset : {-2, -1, 0, 1, 2})
				result.push_back(A::from_raw_value(static_cast<B>(raw + static_cast<B>(offset))));
		}
		std::mt19937_64 rng(3000 + static_cast<unsigned>(bits<A>));
		for(int i = 0; i < count; ++i)
		{
			const int magnitude = 1 + static_cast<int>(rng() % static_cast<unsigned>(bits<A>));
			result.push_back(A::from_raw_value(static_cast<B>(rng() >> (64 - magnitude))));
		}
		return result;
	}

	/// Values in [-limit, limit]: the ends, zero, and random ones of every magnitude
	template<typename Q>
	std::vector<Q> values(const long double limit, const int count = 1500)
	{
		using B = typename Q::base_type;
		const auto largest = std::min(static_cast<long double>(std::numeric_limits<B>::max()), std::ldexp(limit, static_cast<int>(Q::fraction_bits)));
		const auto top = static_cast<B>(largest);
		std::vector<Q> result{Q::from_raw_value(0), Q::from_raw_value(1), Q::from_raw_value(-1), Q::from_raw_value(top), Q::from_raw_value(static_cast<B>(-top)),
			Q::from_raw_value(static_cast<B>(top - 1)), Q::from_raw_value(static_cast<B>(1 - top)), Q::from_raw_value(static_cast<B>(top / 2))};
		std::mt19937_64 rng(4000 + Q::fraction_bits + sizeof(B));
		const int digits = static_cast<int>(std::bit_width(static_cast<uint64_t>(top)));
		for(int i = 0; i < count; ++i)
		{
			const int magnitude = 1 + static_cast<int>(rng() % static_cast<unsigned>(digits));
			const auto raw = static_cast<B>(std::min(rng() >> (64 - magnitude), static_cast<uint64_t>(top)));
			result.push_back(Q::from_raw_value((rng() % 2 != 0) ? static_cast<B>(-raw) : raw));
		}
		return result;
	}

	::testing::AssertionResult within(const long double got, const long double expected, const long double tolerance)
	{
		if(std::abs(got - expected) <= tolerance)
			return ::testing::AssertionSuccess();
		return ::testing::AssertionFailure() << std::setprecision(20) << got << " differs from " << expected
			<< " by " << std::abs(got - expected) << " > " << tolerance;
	}

	/// The difference of two angles in turns, modulo a turn: in [-1/2, 1/2)
	long double difference(const long double a, const long double b)
	{
		long double d = a - b;
		d -= std::floor(d + 0.5L);
		return d;
	}

	/// The error bound of sin and cos: half an epsilon for the rounding, 1/8 epsilon for the polynomial's own error,
	/// a few units of the evaluation's precision, and the rounding of an angle with more bits than the evaluation uses
	template<typename A, typename Q>
	long double sine_tolerance()
	{
		using B = typename Q::base_type;
		using I = typename Q::intermediate_type;
		const int position_bits = fpm::detail::quarter_turn_bits<B, I, Q::fraction_bits>;
		const long double angle_error = (bits<A> - 2 > position_bits) ? std::ldexp(two_pi / 4, -position_bits - 1) : 0;
		return 0.625L * eps<Q> + 8 * poly_eps<Q> + angle_error;
	}

	/// The error bound of the angles: half a unit of the fraction for the rounding, 1/8 unit for the polynomial's own error,
	/// and a few units of the evaluation's precision (in radians, so fewer in turns). The evaluation has 32 bits at least,
	/// if the fraction needs them.
	template<typename A, typename Q>
	long double angle_tolerance()
	{
		using evaluation = fpm::detail::fraction_math::evaluation_t<typename A::base_type, typename Q::base_type, typename Q::intermediate_type, Q::fraction_bits, Q::enable_rounding>;
		return 0.625L * unit<A> + 8 * poly_eps<evaluation> / two_pi;
	}

	// 32 bits for the narrower types, where the fraction needs them
	static_assert(std::is_same_v<fpm::detail::fraction_math::evaluation_t<uint16_t, int16_t, int32_t, 8, true>, fpm::fixed<int32_t, int64_t, 8>>);
	static_assert(std::is_same_v<fpm::detail::fraction_math::evaluation_t<uint8_t, int16_t, int32_t, 8, true>, fpm::fixed_8_8>);
	static_assert(std::is_same_v<fpm::detail::fraction_math::evaluation_t<uint8_t, int8_t, int16_t, 4, true>, fpm::fixed<int32_t, int64_t, 4>>);
	static_assert(std::is_same_v<fpm::detail::fraction_math::evaluation_t<uint64_t, int32_t, int64_t, 16, false>, fpm::fixed<int32_t, int64_t, 16, false>>);

	// Constant expressions
	using angle16 = fpm::fraction<uint16_t>;
	static_assert(fpm::sin<fpm::fixed_16_16>(angle16{0.25}) == fpm::fixed_16_16{1});
	static_assert(fpm::cos<fpm::fixed_16_16>(angle16{0.5}) == fpm::fixed_16_16{-1});
	static_assert(fpm::tan<fpm::fixed_16_16>(angle16{0.125}) == fpm::fixed_16_16{1});
	static_assert(fpm::atan2<angle16>(fpm::fixed_16_16{-1}, fpm::fixed_16_16{0}) == angle16{0.75});
	static_assert(fpm::asin<angle16>(fpm::fixed_16_16{1}) == angle16{0.25});
	static_assert(fpm::acos<angle16>(fpm::fixed_16_16{-1}) == angle16{0.5});
	static_assert(fpm::atan<angle16>(fpm::fixed_16_16{-1}) == angle16{0.875});
}

TEST(fraction_math, sin_cos)
{
	for_each_pair(fractions{}, numbers{}, []<typename A, typename Q>()
	{
		const auto tolerance = sine_tolerance<A, Q>();
		for(const A x : angles<A>())
		{
			const long double angle = two_pi * turns(x);
			ASSERT_TRUE(within(ld(fpm::sin<Q>(x)), std::sin(angle), tolerance)) << "sin of " << turns(x) << ", " << bits<A> << " bits to " << Q::fraction_bits;
			ASSERT_TRUE(within(ld(fpm::cos<Q>(x)), std::cos(angle), tolerance)) << "cos of " << turns(x) << ", " << bits<A> << " bits to " << Q::fraction_bits;

			// Found by argument-dependent lookup, and the same in every quadrant.
			// (An angle with more bits than the evaluation uses is rounded, with ties upwards: not the same for -x.)
			using B = typename Q::base_type;
			using I = typename Q::intermediate_type;
			if(bits<A> - 2 <= fpm::detail::quarter_turn_bits<B, I, Q::fraction_bits>)
			{
				ASSERT_EQ(sin<Q>(x), -sin<Q>(-x));
				ASSERT_EQ(cos<Q>(x), cos<Q>(-x));
			}
			ASSERT_EQ(sin<Q>(x), -sin<Q>(x + A{0.5}));
			ASSERT_EQ(cos<Q>(x), sin<Q>(x + A{0.25}));
		}

		// The axes are exact
		EXPECT_EQ(Q(0), fpm::sin<Q>(A{}));
		EXPECT_EQ(Q(1), fpm::sin<Q>(A{0.25}));
		EXPECT_EQ(Q(0), fpm::sin<Q>(A{0.5}));
		EXPECT_EQ(Q(-1), fpm::sin<Q>(A{0.75}));
		EXPECT_EQ(Q(1), fpm::cos<Q>(A{}));
		EXPECT_EQ(Q(0), fpm::cos<Q>(A{0.25}));
		EXPECT_EQ(Q(-1), fpm::cos<Q>(A{0.5}));
		EXPECT_EQ(Q(0), fpm::cos<Q>(A{0.75}));
	});
}

TEST(fraction_math, tan)
{
	for_each_pair(fractions{}, numbers{}, []<typename A, typename Q>()
	{
		const long double largest = ld(std::numeric_limits<Q>::max());
		for(const A x : angles<A>())
		{
			if(x == A{0.25} || x == A{0.75})
				continue;
			// The evaluation's errors are amplified by the derivative, 1 + tan^2
			const long double t = std::tan(two_pi * turns(x));
			const long double tolerance = (sine_tolerance<A, Q>() - 0.625L * eps<Q>) * (1 + t * t) + 0.625L * eps<Q>;
			const long double result = ld(fpm::tan<Q>(x));
			if(std::abs(t) >= largest - tolerance)
				ASSERT_TRUE(std::abs(result) >= largest - tolerance && (result < 0) == (t < 0)) << "tan of " << turns(x); // saturated
			else
				ASSERT_TRUE(within(result, t, tolerance)) << "tan of " << turns(x) << ", " << bits<A> << " bits to " << Q::fraction_bits;
			ASSERT_EQ(tan<Q>(x), tan<Q>(x + A{0.5}));
		}

		EXPECT_EQ(Q(0), fpm::tan<Q>(A{}));
		EXPECT_EQ(Q(0), fpm::tan<Q>(A{0.5}));
		// Infinite: saturated, with the sign of the quadrant that starts there
		EXPECT_EQ(-std::numeric_limits<Q>::max(), fpm::tan<Q>(A{0.25}));
		EXPECT_EQ(-std::numeric_limits<Q>::max(), fpm::tan<Q>(A{0.75}));
		if(Q(1) < std::numeric_limits<Q>::max())
			EXPECT_EQ(Q(1), fpm::tan<Q>(A{0.125}));
	});
}

TEST(fraction_math, atan2)
{
	for_each_pair(fractions{}, numbers{}, []<typename A, typename Q>()
	{
		const auto tolerance = angle_tolerance<A, Q>();
		const auto all = values<Q>(1e30L, 600);
		for(std::size_t i = 0; i < all.size(); ++i)
		{
			for(const Q x : {all[(i * 7 + 3) % all.size()], all[(i * 13 + 5) % all.size()], Q::from_raw_value(0), Q(1)})
			{
				const Q y = all[i];
				if(x.raw_value() == 0 && y.raw_value() == 0)
					continue;
				const long double expected = std::atan2(ld(y), ld(x)) / two_pi;
				const long double result = turns(fpm::atan2<A>(y, x));
				ASSERT_LE(std::abs(difference(result, expected)), tolerance) << "atan2(" << ld(y) << ", " << ld(x) << ") is " << result << " instead of " << expected
					<< ", " << Q::fraction_bits << " bits to " << bits<A>;

				// Found by argument-dependent lookup, and the same in every quadrant
				ASSERT_EQ(atan2<A>(y, x), -atan2<A>(-y, x));
				ASSERT_EQ(atan2<A>(y, x), A{0.5} - atan2<A>(y, -x));
				ASSERT_EQ(atan2<A>(y, x), A{0.25} - atan2<A>(x, y));
			}
		}

		// The axes and the diagonals are exact
		EXPECT_EQ(A{}, fpm::atan2<A>(Q(0), Q(0)));
		EXPECT_EQ(A{}, fpm::atan2<A>(Q(0), Q(1)));
		EXPECT_EQ(A{0.25}, fpm::atan2<A>(Q(1), Q(0)));
		EXPECT_EQ(A{0.5}, fpm::atan2<A>(Q(0), Q(-1)));
		EXPECT_EQ(A{0.75}, fpm::atan2<A>(Q(-1), Q(0)));
		EXPECT_EQ(A{0.25}, fpm::atan2<A>(std::numeric_limits<Q>::max(), Q(0)));
		EXPECT_EQ(A{0.5}, fpm::atan2<A>(Q(0), std::numeric_limits<Q>::lowest()));
		EXPECT_EQ(A{0.125}, fpm::atan2<A>(Q(1), Q(1)));
		EXPECT_EQ(A{0.375}, fpm::atan2<A>(Q(1), Q(-1)));
		EXPECT_EQ(A{0.625}, fpm::atan2<A>(Q(-1), Q(-1)));
		EXPECT_EQ(A{0.875}, fpm::atan2<A>(Q(-1), Q(1)));
		EXPECT_EQ(A{0.625}, fpm::atan2<A>(std::numeric_limits<Q>::lowest(), std::numeric_limits<Q>::lowest()));
		EXPECT_EQ(A{0.125}, fpm::atan2<A>(Q::from_raw_value(1), Q::from_raw_value(1)));
	});
}

TEST(fraction_math, asin_acos_atan)
{
	for_each_pair(fractions{}, numbers{}, []<typename A, typename Q>()
	{
		const auto tolerance = angle_tolerance<A, Q>();
		for(const Q x : values<Q>(1))
		{
			const long double expected_asin = std::asin(ld(x)) / two_pi;
			const long double expected_acos = std::acos(ld(x)) / two_pi;
			ASSERT_LE(std::abs(difference(turns(fpm::asin<A>(x)), expected_asin)), tolerance) << "asin(" << ld(x) << "), " << Q::fraction_bits << " bits to " << bits<A>;
			ASSERT_LE(std::abs(difference(turns(fpm::acos<A>(x)), expected_acos)), tolerance) << "acos(" << ld(x) << "), " << Q::fraction_bits << " bits to " << bits<A>;
			ASSERT_EQ(asin<A>(x), -asin<A>(-x));
			ASSERT_EQ(acos<A>(x), A{0.25} - asin<A>(x));
		}
		for(const Q x : values<Q>(1e30L))
		{
			const long double expected = std::atan(ld(x)) / two_pi;
			ASSERT_LE(std::abs(difference(turns(fpm::atan<A>(x)), expected)), tolerance) << "atan(" << ld(x) << "), " << Q::fraction_bits << " bits to " << bits<A>;
			ASSERT_EQ(atan<A>(x), -atan<A>(-x));
			ASSERT_EQ(atan<A>(x), fpm::atan2<A>(x, Q(1)));
		}

		EXPECT_EQ(A{}, fpm::asin<A>(Q(0)));
		EXPECT_EQ(A{0.25}, fpm::acos<A>(Q(0)));
		EXPECT_EQ(A{}, fpm::atan<A>(Q(0)));
	});
}

// The angle of the vector of an angle
TEST(fraction_math, round_trip)
{
	using A = fpm::fraction<uint16_t>;
	using Q = fpm::fixed<int32_t, int64_t, 30>;

	for(uint32_t raw = 0; raw <= 0xFFFF; ++raw)
	{
		const auto angle = A::from_raw_value(static_cast<uint16_t>(raw));
		ASSERT_EQ(angle, fpm::atan2<A>(fpm::sin<Q>(angle), fpm::cos<Q>(angle))) << raw;
	}
}

// Every angle of the small types
TEST(fraction_math, exhaustive)
{
	using Q = fpm::fixed_16_16;
	long double worst = 0;
	for(uint32_t raw = 0; raw <= 0xFFFF; ++raw)
	{
		const auto x = fpm::fraction<uint16_t>::from_raw_value(static_cast<uint16_t>(raw));
		const long double angle = two_pi * turns(x);
		worst = std::max(worst, std::abs(ld(fpm::sin<Q>(x)) - std::sin(angle)));
		worst = std::max(worst, std::abs(ld(fpm::cos<Q>(x)) - std::cos(angle)));

		if(raw % 256 == 0)
		{
			// The same angle with fewer bits
			const auto y = fpm::fraction<uint8_t>::from_raw_value(static_cast<uint8_t>(raw >> 8));
			ASSERT_EQ(fpm::sin<Q>(x), fpm::sin<Q>(y));
			ASSERT_EQ(fpm::cos<Q>(x), fpm::cos<Q>(y));
			ASSERT_EQ(fpm::tan<Q>(x), fpm::tan<Q>(y));
		}
	}
	EXPECT_LE(worst, (sine_tolerance<fpm::fraction<uint16_t>, Q>()));
	RecordProperty("worst_error_in_units", std::to_string(static_cast<double>(worst / eps<Q>)));
}

// The radians of the fixed-point functions give the same, within their rounding
TEST(fraction_math, like_radians)
{
	using A = fpm::fraction<uint16_t>;
	using Q = fpm::fixed_16_16;
	using W = fpm::fixed<int32_t, int64_t, 28>;

	for(const A x : angles<A>())
	{
		// The angle in radians, rounded to the precision of the fixed-point type: its error passes on to the result
		const Q radians{static_cast<double>(two_pi * turns(x))};
		EXPECT_TRUE(within(ld(fpm::sin<Q>(x)), ld(sin(radians)), 2 * eps<Q>));
		EXPECT_TRUE(within(ld(fpm::cos<Q>(x)), ld(cos(radians)), 2 * eps<Q>));
	}
	for(const W v : values<W>(1))
	{
		EXPECT_TRUE(within(two_pi * difference(turns(fpm::asin<A>(v)), 0), ld(asin(v)), two_pi * unit<A>));
		EXPECT_TRUE(within(two_pi * turns(fpm::acos<A>(v)), ld(acos(v)), two_pi * unit<A>));
	}
}

#ifndef NDEBUG
TEST(fraction_math, domain)
{
	using A = fpm::fraction<uint16_t>;
	using Q = fpm::fixed_16_16;
	EXPECT_DEATH_IF_SUPPORTED(auto v = fpm::asin<A>(Q(1.5)), "");
	EXPECT_DEATH_IF_SUPPORTED(auto v = fpm::acos<A>(Q(-1.5)), "");
}
#endif
