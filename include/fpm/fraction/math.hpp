#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "../fixed.hpp"
#include "../fixed/math.hpp"
#include "../fraction.hpp"

// Trigonometry for angles that are stored as a fraction of a full turn: 1/4 is a right angle.
//
// The functions have the names of the ones for fixed-point numbers (in radians). The type of the angle tells them apart,
// and the type of the result is their first template argument:
//   fpm::sin<fpm::fixed_16_16>(angle)                      the sine of an angle, as a fixed-point number
//   fpm::atan2<fpm::fraction<uint16_t>>(y, x)              the angle of a vector of fixed-point numbers
//
// An angle as a fraction needs no reduction to a quadrant, which is a multiplication for radians: its two highest
// bits are the quadrant. So sin and cos are faster than the ones for radians, and the angle has no rounding error.
// The values are calculated like the ones for fixed-point numbers, with the polynomials of <fpm/fixed/math.hpp>.
//
// Precision:
// - sin, cos and tan are as precise as the ones for the fixed-point type of the result.
// - The angles of asin, acos, atan and atan2 are within about 0.52 units of the fraction, if the fraction has at least
//   4 bits fewer than the base type of the numbers (or than 32 bits, for narrower base types): a 16-bit fraction
//   for 32-bit numbers. With more bits they are as precise as the evaluation in that type: to 2 bits fewer than it has.

namespace fpm
{
	namespace detail::fraction_math
	{
		/// 1/(2π) = 0.15915494309189533577: radians to turns
		inline constexpr long_constant inv_two_pi{733972625820500306, 2877973194442330999};

		template<typename FB>
		inline constexpr int32_t bits = std::numeric_limits<FB>::digits;

		/// A quarter of a turn
		template<typename FB>
		inline constexpr FB quarter = static_cast<FB>(FB{1} << (bits<FB> - 2));

		/// The angle as the quadrant (0 to 3) and the position within it in [0, 1), with quarter_turn_bits<B, I, F>
		/// fraction bits: like detail::quarter_turns for radians. Exact, unless the fraction has more bits than that:
		/// then it's rounded to nearest.
		template<typename B, typename I, uint32_t F, typename FB>
		[[nodiscard]] inline constexpr quarter_turns_result<I> quarter_turns(const fraction<FB> x) noexcept
		{
			using SI = signed_intermediate<I>;
			constexpr int32_t Z = quarter_turn_bits<B, I, F>;
			constexpr int32_t P = bits<FB> - 2; // the fraction bits of the position
			const FB raw = x.raw_value();
			if constexpr(P <= Z)
			{
				const auto position = static_cast<FB>(raw & static_cast<FB>(quarter<FB> - 1));
				return {static_cast<int32_t>(raw >> P), static_cast<SI>(static_cast<SI>(position) << (Z - P))};
			}
			else
			{
				// The sum wraps around like the angle does: into the first quadrant
				using U = std::make_unsigned_t<std::common_type_t<FB, unsigned int>>;
				const auto rounded = static_cast<FB>(static_cast<FB>(static_cast<U>(raw) + (U{1} << (P - Z - 1))) >> (P - Z));
				return {static_cast<int32_t>(rounded >> Z), static_cast<SI>(rounded & static_cast<FB>((FB{1} << Z) - 1))};
			}
		}

		/// The fixed-point type to calculate the angle of numbers of the type fixed<B, I, F, R> in. That is the type itself,
		/// unless its base type has fewer than 32 bits and the evaluation in it (with M = detail::poly_bits fraction bits)
		/// is not precise enough for the fraction: then the base type has 32 bits.
		/// (No more than that: wider intermediate types are not available for every platform, and the results may not depend on it.)
		template<typename FB, typename B, typename I, uint32_t F, bool R>
		using evaluation_t = std::conditional_t<
			(sizeof(B) < sizeof(int32_t) && bits<FB> + 4 > poly_bits<B, 1>),
			fixed<std::conditional_t<std::is_signed_v<B>, int32_t, uint32_t>, std::conditional_t<std::is_signed_v<B>, int64_t, uint64_t>, F, R>,
			fixed<B, I, F, R>
		>;

		/// The precision (as the fraction bits of a fixed-point result) to calculate an angle in radians with,
		/// for a fraction with the bits of FB: 1/8 of its unit is 2^-(bits + 3) turns, which is more than 2^-(bits + 1) radians.
		/// At least the fraction bits F of the argument, so that none of those are dropped.
		template<typename FB, uint32_t F>
		inline constexpr uint32_t angle_precision = static_cast<uint32_t>(std::max<int32_t>(bits<FB> - 2, static_cast<int32_t>(F)));

		/// An angle in [0, π/2] in radians, in QM for the base type B (see detail::poly_bits), as a part of a turn:
		/// the raw value of a fraction in [0, 1/4], rounded to nearest
		template<typename FB, typename B, typename I>
		[[nodiscard]] inline constexpr FB turns(const I angle) noexcept
		{
			using SI = signed_intermediate<I>;
			constexpr int32_t M = poly_bits<B, 1>;
			constexpr int32_t XBits = M + 1; // π/2 < 2
			constexpr int32_t P = std::min<int32_t>(62, value_bits<SI> - XBits - 1);
			const SI product = multiply_by_long_constant<SI, inv_two_pi, XBits, P>(static_cast<SI>(angle)); // Q(M+P)

			constexpr int32_t shift = M + P - bits<FB>;
			if constexpr(shift > 0)
			{
				return static_cast<FB>((product + (SI{1} << (shift - 1))) >> shift);
			}
			else
			{
				// The fraction has more bits than the angle was calculated with
				using U = std::make_unsigned_t<std::common_type_t<FB, SI>>;
				return static_cast<FB>(static_cast<U>(product) << -shift);
			}
		}

		/// atan(a / b) as a part of a turn, for a, b >= 0 (not both 0) with the same scale:
		/// the raw value of a fraction in [0, 1/4]
		template<typename FB, typename B, typename I, uint32_t F>
		[[nodiscard]] inline constexpr FB atan_turns(const I a, const I b) noexcept
		{
			using S = std::make_signed_t<B>;
			constexpr int32_t M = poly_bits<B, 1>;

			// The diagonal is exact
			if(a == b)
				return static_cast<FB>(quarter<FB> >> 1);

			// The smaller over the larger, in [0, 1): an angle below 1/8 turn.
			// For a > b: atan(a / b) = 1/4 - atan(b / a), which is exact for turns.
			const bool swap = a > b;
			const I numerator = swap ? b : a;
			const I denominator = swap ? a : b;
			const auto ratio = static_cast<S>(((numerator << M) + denominator / 2) / denominator);
			const FB angle = turns<FB, B, I>(static_cast<I>(atan_first_octant<B, I, angle_precision<FB, F>>(ratio)));
			return swap ? static_cast<FB>(quarter<FB> - angle) : angle;
		}

		/// The angle for the raw value of a fraction in the first quadrant, mirrored to the opposite side of
		/// the vertical axis (1/2 - angle) and then of the horizontal one (-angle). Exact, and modulo a turn.
		template<typename FB>
		[[nodiscard]] inline constexpr fraction<FB> mirrored(const FB angle, const bool left, const bool below) noexcept
		{
			using U = std::make_unsigned_t<std::common_type_t<FB, unsigned int>>;
			auto result = static_cast<U>(angle);
			if(left)
				result = static_cast<U>((U{quarter<FB>} << 1) - result);
			if(below)
				result = static_cast<U>(U{0} - result);
			return fraction<FB>::from_raw_value(static_cast<FB>(result));
		}
	}

#pragma region Trigonometry functions

	/// The sine of an angle as a part of a turn
	template<Fixed Result, typename FB>
	[[nodiscard]] inline constexpr Result sin(const fraction<FB> x) noexcept
	{
		using B = typename Result::base_type;
		using I = typename Result::intermediate_type;
		const auto [quadrant, position] = detail::fraction_math::quarter_turns<B, I, Result::fraction_bits>(x);
		return detail::sin_quarter_turns<B, I, Result::fraction_bits, Result::enable_rounding>(quadrant, position);
	}

	/// The cosine of an angle as a part of a turn
	template<Fixed Result, typename FB>
	[[nodiscard]] inline constexpr Result cos(const fraction<FB> x) noexcept
	{
		// cos(x) = sin(x + 1/4): one more quarter turn
		using B = typename Result::base_type;
		using I = typename Result::intermediate_type;
		const auto [quadrant, position] = detail::fraction_math::quarter_turns<B, I, Result::fraction_bits>(x);
		return detail::sin_quarter_turns<B, I, Result::fraction_bits, Result::enable_rounding>(quadrant + 1, position);
	}

	/// The tangent of an angle as a part of a turn. Results too large to represent saturate to the maximum, or to
	/// its negative. At 1/4 and 3/4 of a turn the tangent is infinite: that gives the negative, like the angles after them.
	template<Fixed Result, typename FB>
	[[nodiscard]] inline constexpr Result tan(const fraction<FB> x) noexcept
	{
		using B = typename Result::base_type;
		using I = typename Result::intermediate_type;
		const auto [quadrant, position] = detail::fraction_math::quarter_turns<B, I, Result::fraction_bits>(x);
		if(position == 0 && (quadrant & 1) != 0) [[unlikely]]
		{
			// Infinite, or too large: the angle itself tells the side, if it was rounded up to here
			constexpr B max = std::numeric_limits<B>::max();
			const auto within_quadrant = static_cast<FB>(x.raw_value() & static_cast<FB>(detail::fraction_math::quarter<FB> - 1));
			const bool before = within_quadrant != 0 && (within_quadrant >> (detail::fraction_math::bits<FB> - 3)) != 0;
			return Result::from_raw_value(before ? max : static_cast<B>(B{0} - max));
		}
		return detail::tan_quarter_turns<B, I, Result::fraction_bits, Result::enable_rounding>(quadrant, position);
	}

	namespace detail::fraction_math
	{
		/// asin, for a number of the type to calculate in
		template<typename FB, typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fraction<FB> asin(const fixed<B, I, F, R> x) noexcept
		{
			const bool negative = is_negative(x.raw_value());
			const I angle = asin_angle<angle_precision<FB, F>>(x);
			return mirrored<FB>(turns<FB, B, I>(negative ? static_cast<I>(I{0} - angle) : angle), false, negative);
		}

		/// atan2, for numbers of the type to calculate in
		template<typename FB, typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fraction<FB> atan2(const fixed<B, I, F, R> y, const fixed<B, I, F, R> x) noexcept
		{
			if(x.raw_value() == 0 && y.raw_value() == 0)
				return fraction<FB>{};

			// atan(|y| / |x|) in [0, 1/4], then mirrored into the quadrant of (x, y)
			const FB angle = atan_turns<FB, B, I, F>(magnitude<B, I>(y.raw_value()), magnitude<B, I>(x.raw_value()));
			return mirrored<FB>(angle, is_negative(x.raw_value()), is_negative(y.raw_value()));
		}
	}

	/// The angle whose sine is x, as a part of a turn: in [-1/4, 1/4], where the negative angles wrap around to [3/4, 1)
	template<Fraction Result, typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr Result asin(const fixed<B, I, F, R> x) noexcept
	{
		using Value [[maybe_unused]] = fixed<B, I, F, R>;
		using FB = typename Result::base_type;
		assert(x <= Value(+1));
		if constexpr(std::is_signed_v<B>)
			assert(x >= Value(-1));
		return detail::fraction_math::asin<FB>(detail::fraction_math::evaluation_t<FB, B, I, F, R>(x));
	}

	/// The angle whose cosine is x, as a part of a turn: in [0, 1/2]
	template<Fraction Result, typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr Result acos(const fixed<B, I, F, R> x) noexcept
	{
		// acos(x) = 1/4 - asin(x)
		using FB = typename Result::base_type;
		return Result::from_raw_value(detail::fraction_math::quarter<FB>) - asin<Result>(x);
	}

	/// The angle whose tangent is x, as a part of a turn: in (-1/4, 1/4), where the negative angles wrap around to (3/4, 1)
	template<Fraction Result, typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr Result atan(const fixed<B, I, F, R> x) noexcept
	{
		// The angle of the vector (1, x)
		using FB = typename Result::base_type;
		using Value = detail::fraction_math::evaluation_t<FB, B, I, F, R>;
		return detail::fraction_math::atan2<FB>(Value(x), Value(1));
	}

	/// The angle of the vector (x, y), as a part of a turn: counterclockwise from the positive x axis.
	/// Like std::atan2, the angle of the zero vector is 0.
	template<Fraction Result, typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr Result atan2(const fixed<B, I, F, R> y, const fixed<B, I, F, R> x) noexcept
	{
		using FB = typename Result::base_type;
		using Value = detail::fraction_math::evaluation_t<FB, B, I, F, R>;
		return detail::fraction_math::atan2<FB>(Value(y), Value(x));
	}

#pragma endregion
}
