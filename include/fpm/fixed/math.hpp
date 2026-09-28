#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <utility>

#include "../fixed.hpp"
#include "detail/polynomials.hpp"

namespace fpm
{
	#pragma region Helper functions
	namespace detail
	{

		/// Value bits (without the sign) of a signed integer type. From its size, so this works for 128-bit types as well.
		template<typename T>
		inline constexpr int32_t value_bits = static_cast<int32_t>(sizeof(T)) * 8 - 1;

		/// Returns the index of the most-significant set bit of a positive value, in the value's own width
		/// (up to 128 bits)
		template<typename T>
		[[nodiscard]] inline constexpr int32_t find_highest_bit(const T value) noexcept
		{
			assert(value > 0);
			if constexpr(sizeof(T) > sizeof(uint64_t))
			{
				const auto high = static_cast<uint64_t>(value >> 64);
				if(high != 0)
					return 64 + find_highest_bit(high);
				return find_highest_bit(static_cast<uint64_t>(value));
			}
			else
			{
				return static_cast<int32_t>(std::bit_width(static_cast<std::make_unsigned_t<T>>(value))) - 1;
			}
		}

		/// |x| of a raw value, in the intermediate type (where it cannot overflow)
		template<typename B, typename I>
		[[nodiscard]] inline constexpr I magnitude(const B raw) noexcept
		{
			if constexpr(std::is_signed_v<B>)
				return raw < 0 ? static_cast<I>(-static_cast<I>(raw)) : static_cast<I>(raw);
			else
				return static_cast<I>(raw);
		}

		/// The magnitude of a raw value in the unsigned type of its width, which holds the one of the lowest value too
		template<typename B>
		[[nodiscard]] inline constexpr std::make_unsigned_t<B> unsigned_magnitude(const B raw) noexcept
		{
			using U = std::make_unsigned_t<B>;
			return is_negative(raw) ? static_cast<U>(U{0} - static_cast<U>(raw)) : static_cast<U>(raw);
		}

		/// Integer square root of a non-negative value, rounded to nearest
		template<typename T>
		[[nodiscard]] inline constexpr T sqrt_rounded(T num) noexcept
		{
			assert(num >= 0);
			if(num == 0)
				return 0;
			T res = 0;
			for(T bit = T{1} << (find_highest_bit(num) / 2 * 2); bit != 0; bit >>= 2)
			{
				const T val = res + bit;
				res >>= 1;
				if(num >= val)
				{
					num -= val;
					res += bit;
				}
			}
			// Round the last digit up if necessary: (res + 0.5)^2 = res^2 + res + 0.25
			if(num > res)
				++res;
			return res;
		}

		/// Integer square root of a non-negative value below 4^Steps, rounded to nearest.
		/// A fixed number of branch-free steps (known at compile time), one per bit of the result. T must be signed.
		template<int32_t Steps, typename T>
		[[nodiscard]] inline constexpr T sqrt_steps(T num) noexcept
		{
			static_assert(T(-1) < T(0) && 2 * Steps < value_bits<T>);
			assert(num >= 0);
			T res = 0;
			for(int32_t i = Steps - 1; i >= 0; --i)
			{
				const T bit = T{1} << (2 * i);
				const T value = res + bit;
				const T difference = num - value;
				// All ones if num < value (the bit is not set), from the sign: a comparison would become an unpredictable branch
				const T below = difference >> value_bits<T>;
				num = difference + (value & below);
				res = (res >> 1) + (bit & ~below);
			}
			// Round the last digit up if necessary: (res + 0.5)^2 = res^2 + res + 0.25
			return num > res ? res + 1 : res;
		}

		/// Quotient of x / y rounded to nearest (ties to even), and the matching remainder x - quotient * y
		template<typename T>
		struct rounded_division
		{
			T quotient;
			T remainder;
		};

		/// `Q` must be able to hold the quotient of `lowest() / -1` (so it's wider than `T` for signed types)
		template<typename Q, typename T>
		[[nodiscard]] inline constexpr rounded_division<Q> divide_to_nearest(const T x, const T y) noexcept
		{
			assert(y != 0);
			if constexpr(std::is_signed_v<T>)
			{
				// The one case where native division overflows: the division is exact
				if(y == -1)
					return {static_cast<Q>(-static_cast<Q>(x)), Q{0}};
			}
			auto q = static_cast<Q>(x / y);
			auto r = static_cast<T>(x % y);

			// Compare |r| with |y| / 2 using unsigned magnitudes, which cannot overflow
			using U = std::make_unsigned_t<T>;
			const U abs_r = r < 0 ? static_cast<U>(U{0} - static_cast<U>(r)) : static_cast<U>(r);
			const U abs_y = y < 0 ? static_cast<U>(U{0} - static_cast<U>(y)) : static_cast<U>(y);
			const U rest = abs_y - abs_r; // distance to the next multiple of y
			if(abs_r > rest || (abs_r == rest && q % 2 != 0))
			{
				// Move the quotient away from zero; the new remainder has the opposite sign, |r| - |y|
				if((r < 0) == (y < 0))
				{
					++q;
					r = static_cast<T>(r - y);
				}
				else
				{
					--q;
					r = static_cast<T>(r + y);
				}
			}
			return {q, r};
		}

		template<typename I>
		[[nodiscard]] consteval auto signed_type_of() noexcept
		{
			if constexpr(I(-1) < I(0))
				return std::type_identity<I>{};
			else if constexpr(sizeof(I) <= sizeof(int64_t))
				return std::type_identity<std::make_signed_t<I>>{};
#ifdef FPM_INT128
			else
				return std::type_identity<FPM_INT128>{};
#endif
		}

		/// The signed type with the width of the intermediate type I, for calculations that can be negative
		/// even for unsigned types
		template<typename I>
		using signed_intermediate = typename decltype(signed_type_of<I>())::type;

		/// value * 2^Shift: shifted to the left, or to the right (towards negative infinity) for a negative Shift
		template<int32_t Shift, typename T>
		[[nodiscard]] inline constexpr T shift_by(const T value) noexcept
		{
			if constexpr(Shift >= 0)
				return static_cast<T>(value << Shift);
			else
				return static_cast<T>(value >> -Shift);
		}

		/// value / 2^Shift rounded to nearest (ties upwards), or value * 2^-Shift for a negative Shift
		template<int32_t Shift, typename T>
		[[nodiscard]] inline constexpr T round_shift(const T value) noexcept
		{
			if constexpr(Shift > 0)
				return static_cast<T>((value + (T{1} << (Shift - 1))) >> Shift);
			else
				return shift_by<-Shift>(value);
		}

		/// A constant `value` in Q`from`, rounded to nearest in Q`to`
		template<typename T>
		[[nodiscard]] inline constexpr T round_constant(const int64_t value, const int32_t from, const int32_t to) noexcept
		{
			if(to >= from)
				return static_cast<T>(static_cast<T>(value) << (to - from));
			return static_cast<T>(((value >> (from - to - 1)) + 1) >> 1);
		}

		/// A constant c in [0, 2) with 124 fraction bits: c = hi / 2^62 + lo / 2^124, with 0 <= lo < 2^62
		struct long_constant
		{
			uint64_t hi;
			uint64_t lo;

			/// floor(c * 2^q) modulo 2^count, for 0 <= q <= 124 and count <= 63
			[[nodiscard]] consteval int64_t bits(const int32_t q, const int32_t count) const noexcept
			{
				const uint64_t value = (q <= 62) ? hi >> (62 - q) : (hi << (q - 62)) | (lo >> (124 - q));
				return static_cast<int64_t>(value & ((uint64_t{1} << count) - 1));
			}
		};

		inline constexpr long_constant log2_e{6653256548922161245, 4021995779230246865};     // log2(e) = 1.4426950408889634074
		inline constexpr long_constant two_over_pi{2935890503282001226, 2288520740914548188}; // 2/π = 0.63661977236758134308

		/// x * c with P fraction bits for a raw value |x| < 2^XBits, calculated with up to 124 bits of c (as far as they
		/// affect the result) in the signed type T. c has P + 1 bits, and x * c must fit: P <= value_bits<T> - XBits - 1.
		template<typename T, long_constant C, int32_t XBits, int32_t P>
		[[nodiscard]] inline constexpr T multiply_by_long_constant(const T x) noexcept
		{
			static_assert(P >= 0 && P <= 62 && XBits + P + 1 <= value_bits<T>);
			// c = high / 2^P + low / 2^(P + E): x * low needs XBits + E bits
			constexpr int32_t E = std::min<int32_t>({63, value_bits<T> - XBits, 124 - P});
			constexpr auto high = static_cast<T>(C.bits(P, 63));
			constexpr auto low = static_cast<T>(C.bits(P + E, E));
			return static_cast<T>(x * high + ((x * low) >> E));
		}

		/// Polynomial approximations are evaluated with more fraction bits than the type has: in the signed type with
		/// the width of B, keeping `IntegralBits` integral bits for the coefficients and intermediate values.
		/// Products are calculated in the intermediate type I, so this costs no more than regular fixed-point products.
		template<typename B, int32_t IntegralBits>
		inline constexpr int32_t poly_bits = std::numeric_limits<std::make_signed_t<B>>::digits - IntegralBits;

		/// a * b in QM, rounded to nearest: truncating would add up to a bias of one unit per product
		template<typename I, int32_t M, typename S>
		[[nodiscard]] inline constexpr S poly_multiply(const S a, const S b) noexcept
		{
			return static_cast<S>((static_cast<I>(a) * b + (I{1} << (M - 1))) >> M);
		}

		/// The coefficients (Q62) of a polynomial, in QM
		template<typename B, int32_t M, std::size_t N>
		[[nodiscard]] consteval std::array<std::make_signed_t<B>, N> poly_coefficients(const std::array<int64_t, N>& q62) noexcept
		{
			std::array<std::make_signed_t<B>, N> result{};
			for(std::size_t i = 0; i < N; ++i)
				result[i] = round_constant<std::make_signed_t<B>>(q62[i], 62, M);
			return result;
		}

		/// The polynomial with coefficients `c` (lowest order first) at x, in QM.
		/// Estrin's scheme: shorter dependency chains than Horner's, so more of the products are calculated in parallel.
		template<typename I, int32_t M, typename S, std::size_t N>
		[[nodiscard]] inline constexpr S estrin(const std::array<S, N>& c, const S x) noexcept
		{
			if constexpr(N == 1)
				return c[0];
			else
			{
				// c0 + c1 x, c2 + c3 x, ...: a polynomial in x^2
				constexpr std::size_t pairs = (N + 1) / 2;
				const auto pair = [&](const std::size_t i)
				{
					return (2 * i + 1 < N) ? static_cast<S>(c[2 * i] + poly_multiply<I, M, S>(c[2 * i + 1], x)) : c[2 * i];
				};
				const auto next = [&]<std::size_t... Index>(std::index_sequence<Index...>)
				{
					return std::array<S, pairs>{pair(Index)...};
				}(std::make_index_sequence<pairs>{});
				return estrin<I, M, S, pairs>(next, poly_multiply<I, M, S>(x, x));
			}
		}

		/// The polynomial approximations (see polynomials.hpp) with at least `Bits` bits of precision, in QM
		template<typename B, int32_t M, int32_t Bits>
		inline constexpr auto exp2_coefficients = poly_coefficients<B, M>(exp2_polynomial<exp2_precision.degree_for(Bits)>);
		template<typename B, int32_t M, int32_t Bits>
		inline constexpr auto log2_coefficients = poly_coefficients<B, M>(log2_polynomial<log2_precision.degree_for(Bits)>);
		template<typename B, int32_t M, int32_t Bits>
		inline constexpr auto sin_coefficients = poly_coefficients<B, M>(sin_polynomial<sin_precision.degree_for(Bits)>);
		template<typename B, int32_t M, int32_t Bits>
		inline constexpr auto atan_coefficients = poly_coefficients<B, M>(atan_polynomial<atan_precision.degree_for(Bits)>);

		/// The precision to aim for in a result with F fraction bits: three more bits than the result, so the approximation's error
		/// adds at most 1/8 unit to the rounding error. But at most one bit more than the evaluation's M bits, which limit
		/// the precision anyway.
		template<uint32_t F, int32_t M>
		inline constexpr int32_t target_bits = std::min<int32_t>(static_cast<int32_t>(F) + 3, M + 1);

	}
	#pragma endregion

	#pragma region Classification methods

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr int fpclassify(fixed<B, I, F, R> x) noexcept
	{
		return (x.raw_value() == 0) ? FP_ZERO : FP_NORMAL;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool isfinite(fixed<B, I, F, R>) noexcept
	{
		return true;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool isinf(fixed<B, I, F, R>) noexcept
	{
		return false;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool isnan(fixed<B, I, F, R>) noexcept
	{
		return false;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool isnormal(fixed<B, I, F, R> x) noexcept
	{
		return x.raw_value() != 0;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool signbit(fixed<B, I, F, R> x) noexcept
	{
		return x.raw_value() < 0;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool isgreater(fixed<B, I, F, R> x, fixed<B, I, F, R> y) noexcept
	{
		return x > y;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool isgreaterequal(fixed<B, I, F, R> x, fixed<B, I, F, R> y) noexcept
	{
		return x >= y;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool isless(fixed<B, I, F, R> x, fixed<B, I, F, R> y) noexcept
	{
		return x < y;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool islessequal(fixed<B, I, F, R> x, fixed<B, I, F, R> y) noexcept
	{
		return x <= y;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool islessgreater(fixed<B, I, F, R> x, fixed<B, I, F, R> y) noexcept
	{
		return x != y;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool isunordered(fixed<B, I, F, R>, fixed<B, I, F, R>) noexcept
	{
		return false;
	}

	#pragma endregion

	#pragma region Nearest integer operations

	namespace detail
	{
		/// x = floor + fraction / 2^F with fraction in [0, 2^F): `floor` via an arithmetic shift, `fraction` via a mask.
		/// Neither can overflow, and they work for any number of fraction bits.
		template<typename B, uint32_t F>
		struct floor_parts
		{
			using U = std::make_unsigned_t<B>;
			static constexpr U mask = static_cast<U>((U{1} << F) - 1);
			static constexpr U half = static_cast<U>(U{1} << (F - 1));

			B floor;
			U fraction;

			constexpr explicit floor_parts(const B raw) noexcept
				: floor(static_cast<B>(raw >> F))
				, fraction(static_cast<U>(static_cast<U>(raw) & mask))
			{}

			/// The fixed-point value of an integer. Like static_cast, results out of range wrap (no overflow).
			template<typename I, bool R>
			[[nodiscard]] static constexpr fixed<B, I, F, R> to_fixed(const B integer) noexcept
			{
				return fixed<B, I, F, R>::from_raw_value(static_cast<B>(static_cast<U>(static_cast<U>(integer) << F)));
			}

			/// The fixed-point value of `floor`, or of the integer above it if `up`. The one above the largest integer
			/// is not a number of the type: with FPM_DEFINED_OVERFLOW the result saturates to the maximum.
			template<typename I, bool R>
			[[nodiscard]] constexpr fixed<B, I, F, R> rounded(const bool up) const noexcept
			{
				if constexpr(defined_overflow)
				{
					// That integer is 2^(bits - F), which wraps around: to the lowest value of a signed type, where the sign
					// changes from positive to negative, and to 0 for an unsigned type, where the highest bit goes from 1 to 0.
					// One less than that is the maximum. Without a branch, so loops can be vectorized.
					constexpr int32_t top = std::numeric_limits<U>::digits - 1;
					const auto result = static_cast<U>(static_cast<U>(static_cast<U>(floor) + (up ? 1u : 0u)) << F);
					const auto before = static_cast<U>(static_cast<U>(floor) << F);
					const auto changed = static_cast<U>(std::is_signed_v<B> ? (~before & result) : (before & ~result));
					return fixed<B, I, F, R>::from_raw_value(static_cast<B>(result - static_cast<U>(changed >> top)));
				}
				else
					return to_fixed<I, R>(static_cast<B>(floor + (up ? 1 : 0)));
			}
		};
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> ceil(fixed<B, I, F, R> x) noexcept
	{
		const detail::floor_parts<B, F> parts(x.raw_value());
		return parts.template rounded<I, R>(parts.fraction != 0);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> floor(fixed<B, I, F, R> x) noexcept
	{
		const detail::floor_parts<B, F> parts(x.raw_value());
		return parts.template to_fixed<I, R>(parts.floor);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> trunc(fixed<B, I, F, R> x) noexcept
	{
		const detail::floor_parts<B, F> parts(x.raw_value());
		// Negative values with a fraction round up
		return parts.template to_fixed<I, R>(static_cast<B>(parts.floor + (x.raw_value() < 0 && parts.fraction != 0 ? 1 : 0)));
	}

	/// Round to nearest, ties away from zero
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> round(fixed<B, I, F, R> x) noexcept
	{
		using Parts = detail::floor_parts<B, F>;
		const Parts parts(x.raw_value());
		const bool up = parts.fraction > Parts::half || (parts.fraction == Parts::half && x.raw_value() >= 0);
		return parts.template rounded<I, R>(up);
	}

	/// Round to nearest, ties to even (rounding mode is assumed to be FE_TONEAREST)
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> nearbyint(fixed<B, I, F, R> x) noexcept
	{
		using Parts = detail::floor_parts<B, F>;
		const Parts parts(x.raw_value());
		const bool up = parts.fraction > Parts::half || (parts.fraction == Parts::half && (parts.floor & 1) != 0);
		return parts.template rounded<I, R>(up);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> rint(fixed<B, I, F, R> x) noexcept
	{
		// Rounding mode is assumed to be FE_TONEAREST
		return nearbyint(x);
	}

	#pragma endregion

	namespace detail
	{
		/// The raw value of a result in the base type. With FPM_DEFINED_OVERFLOW the mathematical functions saturate:
		/// a result beyond the range is the maximum or the lowest value. `Check` is whether that can happen for the type:
		/// only then it is looked for.
		template<typename B, bool Check, typename T>
		[[nodiscard]] inline constexpr B saturated(const T value) noexcept
		{
			if constexpr(Check)
			{
				if(value > static_cast<T>(std::numeric_limits<B>::max()))
					return std::numeric_limits<B>::max();
				if(value < static_cast<T>(std::numeric_limits<B>::lowest()))
					return std::numeric_limits<B>::lowest();
			}
			return static_cast<B>(value);
		}

		/// Whether the type cannot represent every logarithm: the ones of numbers below 1 are negative and down to -F (in base 2)
		template<typename B, uint32_t F>
		inline constexpr bool log_saturates = defined_overflow
			&& (!std::is_signed_v<B> || static_cast<uint64_t>(F) > (static_cast<uint64_t>(std::numeric_limits<B>::max()) >> F) + 1);

		/// Whether the type cannot represent every angle from -π to π. The angles of numbers that are not negative are
		/// at most π/2, which every type can represent.
		template<typename B, uint32_t F>
		inline constexpr bool angle_saturates = defined_overflow && std::is_signed_v<B> && std::numeric_limits<B>::digits - static_cast<int32_t>(F) < 2;

		/// x *= y. With FPM_DEFINED_OVERFLOW the product wraps around, and `representable` becomes false
		/// if the type cannot represent it.
		template<typename B, typename I, uint32_t F, bool R>
		inline constexpr void multiply_power(fixed<B, I, F, R>& x, const fixed<B, I, F, R> y, [[maybe_unused]] bool& representable) noexcept
		{
			if constexpr(defined_overflow)
			{
				const I product = shift_right<R>(static_cast<I>(static_cast<I>(x.raw_value()) * static_cast<I>(y.raw_value())), F);
				const auto narrow = static_cast<B>(product);
				x = fixed<B, I, F, R>::from_raw_value(narrow);
				representable &= static_cast<I>(narrow) == product;
			}
			else
				x *= y;
		}

		/// x = 1 / x. With FPM_DEFINED_OVERFLOW the quotient wraps around, and `representable` becomes false
		/// if the type cannot represent it.
		template<typename B, typename I, uint32_t F, bool R>
		inline constexpr void invert_power(fixed<B, I, F, R>& x, [[maybe_unused]] bool& representable) noexcept
		{
			if constexpr(defined_overflow)
			{
				const I divisor = static_cast<I>(x.raw_value());
				const I quotient = R
					? shift_right<true>(static_cast<I>((I{1} << (2 * F + 1)) / divisor), 1)
					: static_cast<I>((I{1} << (2 * F)) / divisor);
				const auto narrow = static_cast<B>(quotient);
				x = fixed<B, I, F, R>::from_raw_value(narrow);
				representable &= static_cast<I>(narrow) == quotient;
			}
			else
				x = fixed<B, I, F, R>(1) / x;
		}
	}

	#pragma region Mathematical functions

	/// The absolute value. With FPM_DEFINED_OVERFLOW it saturates: the one of the lowest value is the maximum.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> abs(fixed<B, I, F, R> x) noexcept
	{
		if constexpr(!std::is_signed_v<B>)
			return x; // unsigned values are never negative
		else if constexpr(!detail::defined_overflow)
			return (x >= fixed<B, I, F, R>{0}) ? x : -x;
		else
		{
			using U = std::make_unsigned_t<B>;
			// The magnitude of the lowest value is the only one with the highest bit: 1 less is the maximum.
			// Without a branch, so loops can be vectorized.
			const U magnitude = detail::unsigned_magnitude(x.raw_value());
			return fixed<B, I, F, R>::from_raw_value(static_cast<B>(magnitude - static_cast<U>(magnitude >> (std::numeric_limits<U>::digits - 1))));
		}
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> fmod(fixed<B, I, F, R> x, fixed<B, I, F, R> y) noexcept
	{
		assert(y.raw_value() != 0);
		if constexpr(std::is_signed_v<B>)
		{
			// `lowest() % -1` overflows, but any value modulo -1 is 0
			if(y.raw_value() == -1) [[unlikely]]
				return fixed<B, I, F, R>::from_raw_value(0);
		}
		return fixed<B, I, F, R>::from_raw_value(static_cast<B>(x.raw_value() % y.raw_value()));
	}

	/// x - n * y, where n is x / y rounded to the nearest integer (ties to even). The result is exact.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> remainder(fixed<B, I, F, R> x, fixed<B, I, F, R> y) noexcept
	{
		const auto division = detail::divide_to_nearest<I>(x.raw_value(), y.raw_value());
		return fixed<B, I, F, R>::from_raw_value(static_cast<B>(division.remainder));
	}

	/// Same result as `remainder`. Also stores the sign and the low 30 bits of the rounded quotient x / y in `*quo`.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> remquo(fixed<B, I, F, R> x, fixed<B, I, F, R> y, int* quo) noexcept
	{
		assert(quo != nullptr);
		const auto division = detail::divide_to_nearest<I>(x.raw_value(), y.raw_value());
		const I quotient = division.quotient;
		const auto low_bits = static_cast<int>(static_cast<uint32_t>(quotient < 0 ? -quotient : quotient) & 0x3FFF'FFFFu);
		*quo = quotient < 0 ? -low_bits : low_bits;
		return fixed<B, I, F, R>::from_raw_value(static_cast<B>(division.remainder));
	}

	#pragma endregion

	#pragma region Manipulation functions

	/// The magnitude of x with the sign of y. With FPM_DEFINED_OVERFLOW it saturates: to the maximum for the lowest value
	/// with a positive sign, and to 0 for an unsigned type with a negative one.
	template<typename B, typename I, uint32_t F, bool R, typename C, typename J, uint32_t G, bool S>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> copysign(fixed<B, I, F, R> x, fixed<C, J, G, S> y) noexcept
	{
		if constexpr(!detail::defined_overflow)
		{
			x = abs(x);
			return (y >= fixed<C, J, G, S>{0}) ? x : -x;
		}
		else
		{
			if(!detail::is_negative(y.raw_value()))
				return abs(x);
			if constexpr(std::is_signed_v<B>)
			{
				// The negative of the magnitude is a number of the type for every magnitude
				using U = std::make_unsigned_t<B>;
				return fixed<B, I, F, R>::from_raw_value(static_cast<B>(U{0} - detail::unsigned_magnitude(x.raw_value())));
			}
			else
				return fixed<B, I, F, R>{};
		}
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> nextafter(fixed<B, I, F, R> from, fixed<B, I, F, R> to) noexcept
	{
		if(from == to)
			return to;
		else if(to > from)
			return from + fixed<B, I, F, R>::from_raw_value(1);
		else
			return from - fixed<B, I, F, R>::from_raw_value(1);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> nexttoward(fixed<B, I, F, R> from, fixed<B, I, F, R> to) noexcept
	{
		return nextafter(from, to);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> modf(fixed<B, I, F, R> x, fixed<B, I, F, R>* iptr) noexcept
	{
		assert(iptr != nullptr);
		// The integral part rounds towards zero; the fraction keeps the sign of x
		*iptr = trunc(x);
		return fixed<B, I, F, R>::from_raw_value(static_cast<B>(x.raw_value() - iptr->raw_value()));
	}

	#pragma endregion

	#pragma region Power functions

	/// base^exp. With FPM_DEFINED_OVERFLOW, results too large to represent saturate: to the maximum,
	/// or to the lowest value if they are negative.
	template<typename B, typename I, uint32_t F, bool R, std::integral T>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> pow(fixed<B, I, F, R> base, T exp) noexcept
	{
		using Fixed = fixed<B, I, F, R>;

		// Like std::pow: x^0 == 1 for any x, including 0
		if(exp == 0)
			return Fixed(1);

		if(base == Fixed(0))
		{
			assert(exp > 0);
			return Fixed(0);
		}

		using U = std::make_unsigned_t<T>;
		U n = exp < 0 ? static_cast<U>(U{0} - static_cast<U>(exp)) : static_cast<U>(exp);

		// With FPM_DEFINED_OVERFLOW: a power that the type cannot represent is a factor of the result, and the other ones
		// are at least 1, so then the result is one that it cannot represent either. The powers are calculated all the same
		// (they wrap around), so that there is no branch for it in the loop.
		const Fixed beyond = (detail::is_negative(base.raw_value()) && (n % 2) != 0) ? std::numeric_limits<Fixed>::lowest() : std::numeric_limits<Fixed>::max();
		bool representable = true;

		// Negative exponent:
		// - |base| >= 1: divide by the powers of base (most precise), but switch to multiplying with the powers
		//   of 1/base if a power would overflow, since the result can still be representable;
		// - |base| < 1: multiply with the powers of 1/base, as the small powers of base lose precision quickly.
		bool divide = exp < 0;
		bool small_base = base < Fixed(1);
		if constexpr(std::is_signed_v<B>)
			small_base = small_base && base > -Fixed(1);
		if(divide && small_base)
		{
			detail::invert_power(base, representable);
			divide = false;
		}

		// Largest raw magnitude whose square is representable
		constexpr I max_square_root = detail::sqrt_rounded(static_cast<I>(std::numeric_limits<B>::max()) << F) - 1;

		// Exponentiation by squaring
		Fixed result{1};
		for(;;)
		{
			if((n % 2) != 0)
			{
				if(divide)
					result /= base; // |base| >= 1
				else
					detail::multiply_power(result, base, representable);
			}
			n /= 2;
			if(n == 0)
				break;
			if(divide)
			{
				const auto raw = static_cast<I>(base.raw_value());
				bool too_large = raw > max_square_root;
				if constexpr(std::is_signed_v<B>)
					too_large = too_large || raw < -max_square_root;
				if(too_large) [[unlikely]]
				{
					base = Fixed(1) / base;
					divide = false;
				}
			}
			detail::multiply_power(base, base, representable);
		}
		return representable ? result : beyond;
	}

	namespace detail
	{
		/// 2^(z / 2^Z) for z with Z fraction bits, rounded to nearest. Results too large to represent saturate to the maximum,
		/// results too small to represent are 0. With `MinusOne` the result is 1 less, and it is the one that saturates:
		/// 2^z itself can be beyond the range of the type.
		template<typename B, typename I, uint32_t F, bool R, int32_t Z, bool MinusOne = false>
		[[nodiscard]] inline constexpr fixed<B, I, F, R> exp2_fixed_point(const signed_intermediate<I> z) noexcept
		{
			using Fixed = fixed<B, I, F, R>;
			using SI = signed_intermediate<I>;
			using S = std::make_signed_t<B>;
			using U = std::make_unsigned_t<B>;
			constexpr SI subtracted = MinusOne ? SI{1} << F : SI{0};

			// z = n + f, with integer n and f in [0, 1)
			const SI n = z >> Z; // arithmetic shift: rounds towards negative infinity
			if(n > std::numeric_limits<B>::digits - static_cast<int32_t>(F) - 1 + MinusOne) [[unlikely]]
				return std::numeric_limits<Fixed>::max(); // 2^z >= 2^n would not fit, and neither would 2^n - 1 with one more
			if(n < -static_cast<SI>(F) - 1) [[unlikely]]
				return Fixed::from_raw_value(static_cast<B>(-subtracted)); // 2^z < 2^-(F + 1) rounds to 0

			// 2^f = 1 + f Q(f) in [1, 2), with M fraction bits. Precise to the last of them: the largest results use them all.
			constexpr int32_t M = poly_bits<B, 1>;
			constexpr auto& coefficients = exp2_coefficients<B, M, M + 1>;
			const auto f = static_cast<S>(shift_by<M - Z>(static_cast<SI>(z - (n << Z))));
			// 2^f < 2, but its approximation can round up to 2: with the largest n that would not fit
			constexpr U below_two = static_cast<U>((U{1} << (M + 1)) - 1u);
			const auto mantissa = std::min<U>(below_two, static_cast<U>((U{1} << M) + static_cast<U>(poly_multiply<I, M, S>(f, estrin<I, M>(coefficients, f)))));

			// 2^n * 2^f is a shift of the mantissa, rounded to QF. n is within the range checked above.
			const int32_t shift = M - static_cast<int32_t>(F) - static_cast<int32_t>(n);
			if constexpr(MinusOne)
			{
				// 2^z can have one bit more than the base type, so this is done in the intermediate type
				const SI power = (shift <= 0)
					? static_cast<SI>(static_cast<SI>(mantissa) << -shift)
					: static_cast<SI>((static_cast<SI>(static_cast<U>(mantissa >> (shift - 1))) + SI{1}) >> 1);
				const SI result = power - subtracted;
				constexpr auto largest = static_cast<SI>(std::numeric_limits<B>::max());
				return Fixed::from_raw_value(static_cast<B>(result > largest ? largest : result));
			}
			else
			{
				// The mantissa is positive and below 2^(M+1), so this is done in the unsigned type of the base's width
				if(shift <= 0)
					return Fixed::from_raw_value(static_cast<B>(static_cast<U>(mantissa << -shift)));
				return Fixed::from_raw_value(static_cast<B>(static_cast<U>(static_cast<U>(mantissa >> (shift - 1)) + 1u) >> 1));
			}
		}

		/// log2(x) = e + p, with integer e and p in [-1/2, 1/2] with M fraction bits
		template<typename B>
		struct log2_parts
		{
			int32_t e;
			std::make_signed_t<B> p;
		};

		/// log2 of a positive raw value with F fraction bits, with a polynomial precise to `Bits` bits.
		/// With `PlusOne` it is log2 of 1 more than the value (which is above -1): that number can be beyond the range of the type.
		template<typename B, uint32_t F, typename I, int32_t Bits, bool PlusOne = false>
		[[nodiscard]] inline constexpr log2_parts<B> log2_split(const B raw) noexcept
		{
			using S = std::make_signed_t<B>;
			using U = std::make_unsigned_t<B>;
			constexpr int32_t M = poly_bits<B, 1>;
			constexpr int32_t top = std::numeric_limits<U>::digits - 1;

			// The number, as an unsigned value. 1 more than the largest value of a signed type fits in it as well;
			// for an unsigned type the sum can have one more bit, which is moved to the exponent.
			auto value = static_cast<U>(raw);
			int32_t carry = 0;
			if constexpr(PlusOne)
			{
				constexpr U one = U{1} << F;
				value = static_cast<U>(value + one);
				if constexpr(std::is_signed_v<B>)
					assert(raw > -static_cast<S>(one));
				else
				{
					carry = value < one;
					value = static_cast<U>(static_cast<U>(value >> carry) | static_cast<U>(static_cast<U>(carry) << top));
				}
			}
			else
				assert(raw > 0);

			// Normalize to m in [1, 2), in QM: move the highest bit to the top, then down to bit M
			const int32_t leading_zeros = std::countl_zero(value);
			const auto m = static_cast<U>(static_cast<U>(value << leading_zeros) >> (top - M));

			// Centered around 1, where the polynomial converges fastest: m / 2 for m >= sqrt(2), so t = m - 1 is in
			// [sqrt(2)/2 - 1, sqrt(2) - 1]. log2(1 + t) = t Q(t).
			constexpr auto sqrt2 = static_cast<U>(round_constant<S>(int64_t{6521908912666391106}, 62, M)); // sqrt(2) = 1.4142135623730950488
			const int32_t upper = m >= sqrt2;
			const auto t = static_cast<S>(static_cast<S>(m >> upper) - (S{1} << M));
			constexpr auto& coefficients = log2_coefficients<B, M, Bits>;
			return {
				top - leading_zeros - static_cast<int32_t>(F) + upper + carry,
				poly_multiply<I, M, S>(t, estrin<I, M>(coefficients, t))
			};
		}

		/// log2(x) * c for a constant 0 < c < 1 in Q63, rounded to nearest. With `PlusOne` it is log2(1 + x) * c.
		template<int64_t ConstantQ63, bool PlusOne, typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fixed<B, I, F, R> log2_times(const fixed<B, I, F, R> x) noexcept
		{
			using SI = signed_intermediate<I>;
			constexpr int32_t M = poly_bits<B, 1>;
			const auto [e, p] = log2_split<B, F, I, target_bits<F, M>, PlusOne>(x.raw_value());

			// e * c + p * c, both products with as many fraction bits as the signed intermediate type allows.
			// |e| <= 64, and |p| <= 2^(M-1) (as a raw value).
			constexpr int32_t E = std::min<int32_t>(63, value_bits<SI> - 7);
			constexpr int32_t P = std::min<int32_t>(63, value_bits<SI> - M + 1);
			constexpr int32_t Q = std::min<int32_t>(E, M + P); // the fraction bits of the sum
			const SI high = static_cast<SI>(e) * round_constant<SI>(ConstantQ63, 63, E);
			const SI low = static_cast<SI>(p) * round_constant<SI>(ConstantQ63, 63, P);
			const SI sum = shift_by<Q - E>(high) + shift_by<Q - M - P>(low);
			return fixed<B, I, F, R>::from_raw_value(saturated<B, log_saturates<B, F>>(round_shift<Q - static_cast<int32_t>(F)>(sum)));
		}
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> pow(fixed<B, I, F, R> base, fixed<B, I, F, R> exp) noexcept
	{
		using Fixed = fixed<B, I, F, R>;

		// Like std::pow: x^0 == 1 for any x, including 0
		if(exp == Fixed(0))
			return Fixed(1);

		if(base == Fixed(0))
		{
			assert(exp > Fixed(0));
			return Fixed(0);
		}

		const detail::floor_parts<B, F> parts(exp.raw_value());
		if(parts.fraction == 0)
		{
			// Non-fractional exponents are easier to calculate
			return pow(base, parts.floor);
		}

		// For negative bases we do not support fractional exponents.
		// Technically fractions with odd denominators could work,
		// but that's too much work to figure out.
		assert(base > Fixed(0));

		// base^exp = 2^(exp * log2(base)). The product's error is amplified by the result, so log2(base) = e + p is
		// calculated as precisely as the polynomial evaluation allows, and exp * e and exp * p are calculated exactly.
		using SI = detail::signed_intermediate<I>;
		constexpr int32_t M = detail::poly_bits<B, 1>;
		const auto [e, p] = detail::log2_split<B, F, I, M + 1>(base.raw_value());
		const auto y = static_cast<SI>(exp.raw_value());
		const SI high = y * e; // QF
		const SI low = y * p;  // Q(F+M)

		// Only exponents within +-limit matter: beyond, the result saturates or is 0.
		// |p| <= 1/2, so |exp * p| <= |exp * e| / 2 unless e == 0: if |high| > 2 * limit, the exponent is beyond the limit.
		constexpr int32_t digits = std::numeric_limits<B>::digits;
		constexpr SI limit = SI{digits + 2} << F;
		if(high > 2 * limit) [[unlikely]]
			return std::numeric_limits<Fixed>::max();
		if(high < -2 * limit) [[unlikely]]
			return Fixed(0);

		// The exponent in Q(F+K), with as many fraction bits as fit: up to 3 * limit
		constexpr int32_t K = std::min<int32_t>(M, detail::value_bits<SI> - static_cast<int32_t>(F) - static_cast<int32_t>(std::bit_width(static_cast<uint32_t>(3 * (digits + 2)))));
		const SI low_k = detail::shift_by<K - M>(low);
		if(low_k > (limit << K)) [[unlikely]]
			return std::numeric_limits<Fixed>::max(); // only when e == 0
		if(low_k < -(limit << K)) [[unlikely]]
			return Fixed(0);
		return detail::exp2_fixed_point<B, I, F, R, static_cast<int32_t>(F) + K>((high << K) + low_k);
	}

	namespace detail
	{
		/// e^x, or e^x - 1 with `MinusOne`: e^x itself can be beyond the range of the type then.
		/// Results too large to represent saturate to the maximum.
		template<bool MinusOne, typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fixed<B, I, F, R> exp_fixed_point(const fixed<B, I, F, R> x) noexcept
		{
			using Fixed = fixed<B, I, F, R>;
			using SI = signed_intermediate<I>;

			// e^x = 2^(x * log2(e)). e^x > 2^x does not fit for x >= digits - F (and e^x - 1 > 2^(x - 1) does not for one more),
			// and e^x < 2^-(F + 2) rounds to 0 for x <= -(F + 2).
			constexpr int32_t limit = std::numeric_limits<B>::digits - static_cast<int32_t>(F) + MinusOne;
			const auto raw = static_cast<SI>(x.raw_value());
			if(raw >= (SI{limit} << F)) [[unlikely]]
				return std::numeric_limits<Fixed>::max();
			if constexpr(std::is_signed_v<B>)
			{
				if(raw <= -(SI{static_cast<int32_t>(F) + 2} << F)) [[unlikely]]
					return Fixed::from_raw_value(static_cast<B>(MinusOne ? -(SI{1} << F) : SI{0}));
			}

			// Now |raw| < 2^(F + range): the product with log2(e) can have P fraction bits
			constexpr int32_t range = static_cast<int32_t>(std::bit_width(static_cast<uint32_t>(std::max<int32_t>(limit, static_cast<int32_t>(F) + 2))));
			constexpr int32_t XBits = static_cast<int32_t>(F) + range;
			constexpr int32_t P = std::min<int32_t>(62, value_bits<SI> - XBits - 1);
			return exp2_fixed_point<B, I, F, R, static_cast<int32_t>(F) + P, MinusOne>(multiply_by_long_constant<SI, log2_e, XBits, P>(raw));
		}
	}

	/// e^x. Results too large to represent saturate to the maximum.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> exp(fixed<B, I, F, R> x) noexcept
	{
		return detail::exp_fixed_point<false>(x);
	}

	/// 2^x. Results too large to represent saturate to the maximum.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> exp2(fixed<B, I, F, R> x) noexcept
	{
		return detail::exp2_fixed_point<B, I, F, R, static_cast<int32_t>(F)>(static_cast<detail::signed_intermediate<I>>(x.raw_value()));
	}

	/// e^x - 1, also where e^x is beyond the range of the type. Results too large to represent saturate to the maximum.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> expm1(fixed<B, I, F, R> x) noexcept
	{
		return detail::exp_fixed_point<true>(x);
	}

	/// The logarithms saturate to the lowest value where the type cannot represent the result: that is possible
	/// for unsigned types, and for types with few integral bits.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> log2(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;
		assert(x > Fixed(0));

		// e + p, rounded from QM to QF
		using SI = detail::signed_intermediate<I>;
		constexpr int32_t M = detail::poly_bits<B, 1>;
		const auto [e, p] = detail::log2_split<B, F, I, detail::target_bits<F, M>>(x.raw_value());
		const SI result = (static_cast<SI>(e) << F) + detail::round_shift<M - static_cast<int32_t>(F)>(static_cast<SI>(p));
		return Fixed::from_raw_value(detail::saturated<B, detail::log_saturates<B, F>>(result));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> log(fixed<B, I, F, R> x) noexcept
	{
		assert(x.raw_value() > 0);
		return detail::log2_times<int64_t{6393154322601327830}, false>(x); // ln(2) = 0.69314718055994530942
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> log10(fixed<B, I, F, R> x) noexcept
	{
		assert(x.raw_value() > 0);
		return detail::log2_times<int64_t{2776511644261678566}, false>(x); // log10(2) = 0.30102999566398119521
	}

	/// log(1 + x), also where 1 + x is beyond the range of the type
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> log1p(fixed<B, I, F, R> x) noexcept
	{
		return detail::log2_times<int64_t{6393154322601327830}, true>(x); // ln(2) = 0.69314718055994530942
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> cbrt(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;

		if(x == Fixed(0))
			return x;

		// The root of the magnitude, in the intermediate type: the one of the lowest value does not fit the base type
		const bool negative = detail::is_negative(x.raw_value());

		// The result's raw value is cbrt(X * 2^(2F)) for the raw value X, rounded to nearest.
		// Digit-by-digit cube root (base 2), consuming N = X * 2^(2F) three bits at a time from the top,
		// so that N itself never has to be stored. With root y and remainder r = N' - y^3 of the bits consumed
		// so far, r <= 3y^2 + 3y, which needs about 2 * (bits of the result) + 2 bits.
		// With a root below 2^R, (remainder << 3) < 2^(2R+5) is the largest value.
		// Calculated in the intermediate type W = I; the first steps in the base's width (see below).
		using W = I;
		constexpr int32_t total_bits_max = std::numeric_limits<B>::digits + 2 * static_cast<int32_t>(F);
		constexpr int32_t result_bits = (total_bits_max + 2) / 3;
		// If the root would have too many bits even for W, skip some of the trailing zero groups of N,
		// computing cbrt(N / 8^skip) * 2^skip instead: this loses `skip` bits (only for types with very few integral bits).
		constexpr int32_t skip = std::max<int32_t>(0, (2 * result_bits + 5 - std::numeric_limits<W>::digits + 1) / 2);

		// N = X * 2^(2F) = (X << a) * 8^z with a = 2F mod 3: first the 3-bit groups of X << a, then z zero groups
		constexpr int32_t a = (2 * static_cast<int32_t>(F)) % 3;
		constexpr int32_t zero_groups = (2 * static_cast<int32_t>(F)) / 3 - skip;
		static_assert(zero_groups >= 0);
		const W shifted = static_cast<W>(detail::magnitude<B, I>(x.raw_value()) << a);
		const int32_t data_groups = detail::find_highest_bit(shifted) / 3 + 1;
		const int32_t total_groups = data_groups + zero_groups;
		const auto bits_of = [&](const int32_t k) -> W // k-th 3-bit group of N from the top
		{
			return k < data_groups ? static_cast<W>((shifted >> (3 * (data_groups - 1 - k))) & 7) : W{0};
		};

		// One step: root = 2 * root (+ 1), with remainder = N' - root^3 of the bits consumed so far
		// Branch-free: whether the next bit is set is unpredictable, so a branch would often be mispredicted
		const auto step = [](auto& root, auto& root_squared, auto& remainder, const auto bits)
		{
			using T = std::remove_reference_t<decltype(remainder)>;
			remainder = static_cast<T>((remainder << 3) | bits);
			root = static_cast<T>(root << 1);
			root_squared = static_cast<T>(root_squared << 2);
			// (root + 1)^3 - root^3
			const auto next = static_cast<T>(3 * (root_squared + root) + 1);
			const auto mask = static_cast<T>(-static_cast<T>(remainder >= next)); // all ones if the bit is set
			remainder = static_cast<T>(remainder - (next & mask));
			root_squared = static_cast<T>(root_squared + ((2 * root + 1) & mask));
			root = static_cast<T>(root - mask);
		};

		W root = 0;
		W root_squared = 0;
		W remainder = 0;
		int32_t k = 0;

		// If the intermediate type is wider than the CPU's registers (e.g. 64-bit on 32-bit CPUs, 128-bit on 64-bit CPUs),
		// its arithmetic is emulated: then the first steps are done in the signed type of the base's width.
		// After k steps root < 2^k and remainder < 2^(2k+2), so they fit while (remainder << 3) does.
		using E = std::make_signed_t<B>;
		constexpr int32_t early_steps = (std::numeric_limits<E>::digits - 5) / 2;
		if constexpr(sizeof(W) > sizeof(std::uintptr_t) && sizeof(E) <= sizeof(std::uintptr_t) && early_steps >= 4)
		{
			E early_root = 0;
			E early_root_squared = 0;
			E early_remainder = 0;
			for(; k < total_groups && k < early_steps; ++k)
				step(early_root, early_root_squared, early_remainder, static_cast<E>(bits_of(k)));
			root = early_root;
			root_squared = early_root_squared;
			remainder = early_remainder;
		}
		for(; k < data_groups; ++k)
			step(root, root_squared, remainder, static_cast<W>((shifted >> (3 * (data_groups - 1 - k))) & 7));
		for(; k < total_groups; ++k)
			step(root, root_squared, remainder, W{0});

		// Round to nearest: N >= (root + 1/2)^3  <=>  8 * remainder > 12 root^2 + 6 root + 1 (never equal)
		if(8 * remainder > 12 * root_squared + 6 * root + 1)
			++root;

		const auto result = Fixed::from_raw_value(static_cast<B>(root << skip));
		return negative ? -result : result;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> sqrt(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;

		assert(x >= Fixed(0));
		if(x == Fixed(0))
			return x;

		// The raw value of the result is sqrt(X * 2^F) for the raw value X
		return Fixed::from_raw_value(static_cast<B>(detail::sqrt_rounded(static_cast<I>(static_cast<I>(x.raw_value()) << F))));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> hypot(fixed<B, I, F, R> x, fixed<B, I, F, R> y) noexcept
	{
		using Fixed = fixed<B, I, F, R>;

		// sqrt(x^2 + y^2) = sqrt(X^2 + Y^2) / 2^F for raw values X and Y: compute it exactly in the intermediate type.
		// |X|, |Y| <= 2^digits, so each square fits; only their sum can overflow, when the result would too.
		auto a = static_cast<I>(x.raw_value());
		auto b = static_cast<I>(y.raw_value());
		if constexpr(std::is_signed_v<B>)
		{
			a = a < 0 ? -a : a;
			b = b < 0 ? -b : b;
		}
		constexpr auto max = std::numeric_limits<Fixed>::max();
		const I a2 = a * a;
		const I b2 = b * b;
		if(b2 > std::numeric_limits<I>::max() - a2) [[unlikely]]
			return max;
		const I result = detail::sqrt_rounded(a2 + b2);
		return result > static_cast<I>(max.raw_value()) ? max : Fixed::from_raw_value(static_cast<B>(result));
	}

	#pragma endregion

	#pragma region Trigonometry functions

	namespace detail
	{
		/// π/2 (Q62), which is also π in Q61
		inline constexpr int64_t half_pi_q62 = int64_t{7244019458077122842}; // π/2 = 1.5707963267948966192

		/// x / (π/2) modulo 4: the angle in quarter turns, as the quadrant (0 to 3) and the position within it in [0, 1),
		/// with Z = quarter_turn_bits<B, I, F> fraction bits (more than M) in the signed intermediate type.
		/// A multiplication with a precise 2/π (up to 124 bits), so there is no division and the reduction stays accurate
		/// for large arguments. The modulo is a mask of the two's complement intermediate value.
		template<typename I>
		struct quarter_turns_result
		{
			int32_t quadrant;
			signed_intermediate<I> position;
		};

		/// The fraction bits of the product of a raw value with 2/π
		template<typename B, typename I, uint32_t F>
		inline constexpr int32_t quarter_turn_bits = static_cast<int32_t>(F) + std::min<int32_t>(62, value_bits<signed_intermediate<I>> - std::numeric_limits<std::make_unsigned_t<B>>::digits - 1);

		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr quarter_turns_result<I> quarter_turns(const fixed<B, I, F, R> x) noexcept
		{
			using SI = signed_intermediate<I>;
			constexpr int32_t XBits = std::numeric_limits<std::make_unsigned_t<B>>::digits;
			constexpr int32_t Z = quarter_turn_bits<B, I, F>;
			const SI turns = multiply_by_long_constant<SI, two_over_pi, XBits, Z - static_cast<int32_t>(F)>(static_cast<SI>(x.raw_value()));
			return {static_cast<int32_t>((turns >> Z) & 3), static_cast<SI>(turns & ((SI{1} << Z) - 1))};
		}

		/// sin(u * π/2) / u for u^2 in [0, 1] (QM), in QM, with `Bits` bits of precision
		template<typename B, typename I, int32_t Bits>
		[[nodiscard]] inline constexpr std::make_signed_t<B> sin_quotient(const std::make_signed_t<B> u_squared) noexcept
		{
			constexpr int32_t M = poly_bits<B, 1>;
			return estrin<I, M>(sin_coefficients<B, M, Bits>, u_squared);
		}

		/// sin(u * π/2) for u in [0, 1], in QM
		template<typename B, typename I, int32_t Bits>
		[[nodiscard]] inline constexpr std::make_signed_t<B> sin_first_quadrant(const std::make_signed_t<B> u) noexcept
		{
			using S = std::make_signed_t<B>;
			constexpr int32_t M = poly_bits<B, 1>;
			return poly_multiply<I, M, S>(u, sin_quotient<B, I, Bits>(poly_multiply<I, M, S>(u, u)));
		}

		/// sin of an angle in quarter turns, rounded to QF
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fixed<B, I, F, R> sin_quarter_turns(const int32_t quadrant, const signed_intermediate<I> position) noexcept
		{
			using S = std::make_signed_t<B>;
			constexpr int32_t M = poly_bits<B, 1>;

			// In the odd quadrants the sine decreases again: sin(1 - u), and in the second half it's negative
			const auto p = static_cast<S>(shift_by<M - quarter_turn_bits<B, I, F>>(position));
			const auto u = static_cast<S>((quadrant & 1) != 0 ? (S{1} << M) - p : p);
			const auto result = static_cast<B>(round_shift<M - static_cast<int32_t>(F)>(static_cast<I>(sin_first_quadrant<B, I, target_bits<F, M>>(u))));
			// (Negated via the raw value, so this compiles for unsigned base types as well)
			return fixed<B, I, F, R>::from_raw_value((quadrant & 2) != 0 ? static_cast<B>(B{0} - result) : result);
		}
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> sin(fixed<B, I, F, R> x) noexcept
	{
		const auto [quadrant, position] = detail::quarter_turns(x);
		return detail::sin_quarter_turns<B, I, F, R>(quadrant, position);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> cos(fixed<B, I, F, R> x) noexcept
	{
		// cos(x) = sin(x + π/2): one more quarter turn
		const auto [quadrant, position] = detail::quarter_turns(x);
		return detail::sin_quarter_turns<B, I, F, R>(quadrant + 1, position);
	}

	namespace detail
	{
		/// tan of an angle in quarter turns (not exactly an odd number of them), rounded to QF.
		/// Results too large to represent saturate to the maximum.
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fixed<B, I, F, R> tan_quarter_turns(const int32_t quadrant, const signed_intermediate<I> position) noexcept
		{
			using Fixed = fixed<B, I, F, R>;
			using S = std::make_signed_t<B>;
			using SI = signed_intermediate<I>;
			constexpr int32_t M = poly_bits<B, 1>;
			constexpr int32_t Z = quarter_turn_bits<B, I, F>;

			// tan = sin(u) / cos(u) = sin(u) / sin(1 - u) in quarter turns. In the odd quadrants, -cos(u) / sin(u).
			// The result has up to all the bits of the type, so the polynomials are as precise as the evaluation allows.
			const bool odd = (quadrant & 1) != 0;
			const SI numerator = odd ? (SI{1} << Z) - position : position;
			const SI denominator = (SI{1} << Z) - numerator;

			// Tangent goes to infinity at 90 and -90 degrees.
			// We can't represent that with fixed-point maths.
			assert(denominator > 0);

			const S sine = sin_first_quadrant<B, I, M + 1>(static_cast<S>(shift_by<M - Z>(numerator)));

			// Where the tangent is large, the cosine is small: calculated with k more fraction bits, as far as the division allows
			// (sine << (F + k) must fit) and as the reduced angle has them. sin(v) = v Q(v^2) for the denominator's angle v.
			constexpr int32_t k_max = std::min<int32_t>(value_bits<SI> - 1 - M - static_cast<int32_t>(F), Z - M);
			const int32_t k = std::clamp<int32_t>(Z - 1 - find_highest_bit(denominator), 0, k_max);
			const auto v = static_cast<S>(denominator >> (Z - M - k)); // Q(M+k), below 2^M
			const auto v_m = static_cast<S>(v >> k);
			const S cosine = poly_multiply<I, M, S>(v, sin_quotient<B, I, M + 1>(poly_multiply<I, M, S>(v_m, v_m)));

			// |tan| = sine / cosine in QF, rounded to nearest, saturated to the maximum.
			// A cosine of 0 is one below the precision of the evaluation: then the tangent is beyond the maximum.
			constexpr auto max = std::numeric_limits<Fixed>::max();
			if(cosine == 0) [[unlikely]]
				return Fixed::from_raw_value(odd ? static_cast<B>(B{0} - max.raw_value()) : max.raw_value());
			const SI magnitude = ((static_cast<SI>(sine) << (static_cast<int32_t>(F) + k)) + cosine / 2) / cosine;
			const B result = magnitude > static_cast<SI>(max.raw_value()) ? max.raw_value() : static_cast<B>(magnitude);
			// (Negated via the raw value, so this compiles for unsigned base types as well)
			return Fixed::from_raw_value(odd ? static_cast<B>(B{0} - result) : result);
		}
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> tan(fixed<B, I, F, R> x) noexcept
	{
		const auto [quadrant, position] = detail::quarter_turns(x);
		return detail::tan_quarter_turns<B, I, F, R>(quadrant, position);
	}

	namespace detail
	{
		/// atan(x) for x in [0, 1] in QM, in QM
		template<typename B, typename I, uint32_t F>
		[[nodiscard]] inline constexpr std::make_signed_t<B> atan_first_octant(const std::make_signed_t<B> x) noexcept
		{
			using S = std::make_signed_t<B>;
			constexpr int32_t M = poly_bits<B, 1>;
			constexpr auto& coefficients = atan_coefficients<B, M, target_bits<F, M>>;
			return poly_multiply<I, M, S>(x, estrin<I, M>(coefficients, poly_multiply<I, M, S>(x, x)));
		}

		/// atan(a / b) for a, b >= 0 (not both 0) with the same scale, in QM (in [0, π/2]).
		/// The argument of the polynomial is the smaller over the larger, calculated with M fraction bits.
		template<typename B, typename I, uint32_t F>
		[[nodiscard]] inline constexpr I atan_ratio(const I a, const I b) noexcept
		{
			using S = std::make_signed_t<B>;
			constexpr int32_t M = poly_bits<B, 1>;

			// For a > b: atan(a / b) = π/2 - atan(b / a). (A single polynomial evaluation, to keep the code small.)
			const bool swap = a > b;
			const I numerator = swap ? b : a;
			const I denominator = swap ? a : b;
			const I angle = atan_first_octant<B, I, F>(static_cast<S>(((numerator << M) + denominator / 2) / denominator));
			return swap ? static_cast<I>(round_constant<I>(half_pi_q62, 62, M) - angle) : angle;
		}

		/// An angle in QM (in the intermediate type), rounded to QF. It saturates for a type that cannot represent π.
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fixed<B, I, F, R> angle_to_fixed(const I angle) noexcept
		{
			return fixed<B, I, F, R>::from_raw_value(saturated<B, angle_saturates<B, F>>(round_shift<poly_bits<B, 1> - static_cast<int32_t>(F)>(angle)));
		}

		/// asin(x) in QM (in the intermediate type), for a result with `Precision` fraction bits (at least F)
		template<uint32_t Precision, typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr I asin_angle(const fixed<B, I, F, R> x) noexcept
		{
			static_assert(Precision >= F);
			constexpr int32_t M = poly_bits<B, 1>;

			// asin(x) = atan(x / sqrt(1 - x^2)). The raw value of 1 - x^2 in Q(2F) is exactly 2^(2F) - X^2 for the raw value X.
			// Its square root is rounded to K = Precision + 8 fraction bits: its error passes on to the angle about one to one,
			// so this adds at most 1/512 unit to the result. Fewer bits than M make the root faster: it takes K + 1 steps.
			constexpr int32_t K = std::min<int32_t>(static_cast<int32_t>(Precision) + 8, M);
			using SI = signed_intermediate<I>;
			const I a = magnitude<B, I>(x.raw_value());
			const auto root = static_cast<I>(sqrt_steps<K + 1>(shift_by<2 * (K - static_cast<int32_t>(F))>(static_cast<SI>((SI{1} << (2 * F)) - static_cast<SI>(a * a)))));
			const I angle = atan_ratio<B, I, Precision>(shift_by<K - static_cast<int32_t>(F)>(a), root);
			return is_negative(x.raw_value()) ? static_cast<I>(-angle) : angle;
		}
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> atan(fixed<B, I, F, R> x) noexcept
	{
		using S = std::make_signed_t<B>;
		constexpr int32_t M = detail::poly_bits<B, 1>;

		// atan(x) directly for |x| <= 1 (no division), as π/2 - atan(1 / x) beyond
		const I a = detail::magnitude<B, I>(x.raw_value());
		constexpr I one = I{1} << F;
		const bool large = a > one;
		const auto argument = large
			? static_cast<S>(((one << M) + a / 2) / a)
			: static_cast<S>(detail::shift_by<M - static_cast<int32_t>(F)>(a));
		const I result = detail::atan_first_octant<B, I, F>(argument);
		constexpr I half_pi = detail::round_constant<I>(detail::half_pi_q62, 62, M);
		const I angle = large ? static_cast<I>(half_pi - result) : result;
		return detail::angle_to_fixed<B, I, F, R>(detail::is_negative(x.raw_value()) ? static_cast<I>(-angle) : angle);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> asin(fixed<B, I, F, R> x) noexcept
	{
		using Fixed [[maybe_unused]] = fixed<B, I, F, R>;
		assert(x <= Fixed(+1));
		if constexpr(std::is_signed_v<B>)
			assert(x >= Fixed(-1));
		return detail::angle_to_fixed<B, I, F, R>(detail::asin_angle<F>(x));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> acos(fixed<B, I, F, R> x) noexcept
	{
		using Fixed [[maybe_unused]] = fixed<B, I, F, R>;
		assert(x <= Fixed(+1));
		if constexpr(std::is_signed_v<B>)
			assert(x >= Fixed(-1));

		// acos(x) = π/2 - asin(x): asin's absolute error is so small that this is precise even where acos(x) is small
		constexpr I half_pi = detail::round_constant<I>(detail::half_pi_q62, 62, detail::poly_bits<B, 1>);
		return detail::angle_to_fixed<B, I, F, R>(static_cast<I>(half_pi - detail::asin_angle<F>(x)));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> atan2(fixed<B, I, F, R> y, fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;
		if(x == Fixed(0))
		{
			// Like std::atan2, the angle of the zero vector is 0
			if(y == Fixed(0))
				return Fixed(0);
			return (y > Fixed(0)) ? Fixed::half_pi() : -Fixed::half_pi();
		}

		// atan(|y| / |x|) in [0, π/2], then mirrored into the quadrant of (x, y)
		I angle = detail::atan_ratio<B, I, F>(detail::magnitude<B, I>(y.raw_value()), detail::magnitude<B, I>(x.raw_value()));
		if constexpr(std::is_signed_v<B>)
		{
			constexpr I pi = detail::round_constant<I>(detail::half_pi_q62, 61, detail::poly_bits<B, 1>);
			if(x.raw_value() < 0)
				angle = pi - angle;
			if(y.raw_value() < 0)
				angle = -angle;
		}
		return detail::angle_to_fixed<B, I, F, R>(angle);
	}

#pragma endregion

}
