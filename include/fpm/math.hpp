#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <utility>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "fixed.hpp"

// Code-layout hints for GCC, from measurements: some small functions give GCC better code in their callers
// when they are not inlined. This never changes results (all compilers compute exactly the same values).
#if defined(__GNUC__) && !defined(__clang__)
	#define FPM_DETAIL_GCC_NOINLINE [[gnu::noinline]]
#else
	#define FPM_DETAIL_GCC_NOINLINE
#endif

namespace fpm
{
	#pragma region Helper functions
	namespace detail
	{

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
					return {-static_cast<Q>(x), 0};
			}
			Q q = x / y;
			T r = x % y;

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

		/// Fraction bits for constants in precise calculations: as many as the intermediate type allows
		/// when multiplied with a raw value of the base type (at most 63).
		template<typename B, typename I>
		inline constexpr int32_t constant_precision = std::min<int32_t>(63, static_cast<int32_t>(sizeof(I) - sizeof(B)) * 8 - 1);

		/// A Q63 constant, rounded to `constant_precision<B, I>` fraction bits
		template<typename B, typename I, int64_t ConstantQ63>
		inline constexpr I precise_constant = static_cast<I>(
			(constant_precision<B, I> == 63) ? ConstantQ63 : ((ConstantQ63 >> (62 - constant_precision<B, I>)) + 1) >> 1
		);

		/// x * c for a constant 0 <= c < 1 given in Q63, with a single rounding
		/// (instead of rounding the constant to the precision of the type first)
		template<int64_t ConstantQ63, typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fixed<B, I, F, R> multiply_by_constant(const fixed<B, I, F, R> x) noexcept
		{
			constexpr int32_t P = constant_precision<B, I>;
			const I product = static_cast<I>(x.raw_value()) * precise_constant<B, I, ConstantQ63>;
			if constexpr(R)
				return fixed<B, I, F, R>::from_raw_value(static_cast<B>((product + (I{1} << (P - 1))) >> P));
			else
				return fixed<B, I, F, R>::from_raw_value(static_cast<B>(product / (I{1} << P))); // truncate towards zero
		}

		/// Polynomial approximations are evaluated with more fraction bits than the type has: in the signed type with
		/// the width of B, keeping `IntegralBits` integral bits for the coefficients and intermediate values.
		/// Products are calculated in the intermediate type I, so this costs no more than regular fixed-point products.
		/// (Never fewer than F: for types with fewer integral bits than the polynomial needs, the results are only
		/// meaningful where the intermediate values happen to fit, just as when evaluating in the type itself.)
		template<typename B, int32_t IntegralBits, uint32_t F>
		inline constexpr int32_t poly_bits = std::max(std::numeric_limits<std::make_signed_t<B>>::digits - IntegralBits, static_cast<int32_t>(F));

		/// Constant `value` in Q`q` (q >= M), rounded to QM
		template<typename B, int32_t M>
		[[nodiscard]] inline constexpr std::make_signed_t<B> poly_coefficient(const int64_t value, const int32_t q) noexcept
		{
			return static_cast<std::make_signed_t<B>>(q == M ? value : ((value >> (q - M - 1)) + 1) >> 1);
		}

		/// a * b in QM. Truncates: the few extra bits make rounding the intermediate steps unnecessary.
		template<typename I, int32_t M, typename S>
		[[nodiscard]] inline constexpr S poly_multiply(const S a, const S b) noexcept
		{
			return static_cast<S>((static_cast<I>(a) * b) >> M);
		}

		/// Splits x into floor(x) and the fraction x - floor(x) in [0, 1)
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr std::pair<B, fixed<B, I, F, R>> split_floor(const fixed<B, I, F, R> x) noexcept
		{
			using U = std::make_unsigned_t<B>;
			const B raw = x.raw_value();
			return {
				static_cast<B>(raw >> F), // arithmetic shift: rounds towards negative infinity
				fixed<B, I, F, R>::from_raw_value(static_cast<B>(static_cast<U>(raw) & ((U{1} << F) - 1)))
			};
		}

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
		};
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> ceil(fixed<B, I, F, R> x) noexcept
	{
		const detail::floor_parts<B, F> parts(x.raw_value());
		return parts.template to_fixed<I, R>(static_cast<B>(parts.floor + (parts.fraction != 0 ? 1 : 0)));
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
		return parts.template to_fixed<I, R>(static_cast<B>(parts.floor + (up ? 1 : 0)));
	}

	/// Round to nearest, ties to even (rounding mode is assumed to be FE_TONEAREST)
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> nearbyint(fixed<B, I, F, R> x) noexcept
	{
		using Parts = detail::floor_parts<B, F>;
		const Parts parts(x.raw_value());
		const bool up = parts.fraction > Parts::half || (parts.fraction == Parts::half && (parts.floor & 1) != 0);
		return parts.template to_fixed<I, R>(static_cast<B>(parts.floor + (up ? 1 : 0)));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> rint(fixed<B, I, F, R> x) noexcept
	{
		// Rounding mode is assumed to be FE_TONEAREST
		return nearbyint(x);
	}

	#pragma endregion

	#pragma region Mathematical functions

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> abs(fixed<B, I, F, R> x) noexcept
	{
		if constexpr(std::is_signed_v<B>)
			return (x >= fixed<B, I, F, R>{0}) ? x : -x;
		else
			return x; // unsigned values are never negative
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
		return fixed<B, I, F, R>::from_raw_value(division.remainder);
	}

	/// Same result as `remainder`. Also stores the sign and the low 30 bits of the rounded quotient x / y in `*quo`.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> remquo(fixed<B, I, F, R> x, fixed<B, I, F, R> y, int* quo) noexcept
	{
		assert(quo != nullptr);
		const auto division = detail::divide_to_nearest<I>(x.raw_value(), y.raw_value());
		const I quotient = division.quotient;
		const auto low_bits = static_cast<int>((quotient < 0 ? -quotient : quotient) & I{0x3FFF'FFFF});
		*quo = quotient < 0 ? -low_bits : low_bits;
		return fixed<B, I, F, R>::from_raw_value(division.remainder);
	}

	#pragma endregion

	#pragma region Manipulation functions

	template<typename B, typename I, uint32_t F, bool R, typename C, typename J, uint32_t G, bool S>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> copysign(fixed<B, I, F, R> x, fixed<C, J, G, S> y) noexcept
	{
		x = abs(x);
		return (y >= fixed<C, J, G, S>{0}) ? x : -x;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> nextafter(fixed<B, I, F, R> from, fixed<B, I, F, R> to) noexcept
	{
		if(from == to)
			return to;
		else if(to > from)
			return fixed<B, I, F, R>::from_raw_value(from.raw_value() + 1);
		else
			return fixed<B, I, F, R>::from_raw_value(from.raw_value() - 1);
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
			base = Fixed(1) / base;
			divide = false;
		}

		// Largest raw magnitude whose square is representable
		constexpr I max_square_root = detail::sqrt_rounded(static_cast<I>(std::numeric_limits<B>::max()) << F) - 1;

		// Exponentiation by squaring
		Fixed result{1};
		for(;;)
		{
			if((n % 2) != 0)
				result = divide ? result / base : result * base;
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
			base *= base;
		}
		return result;
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

		constexpr auto FRAC = I{1} << F;
		if(exp.raw_value() % FRAC == 0)
		{
			// Non-fractional exponents are easier to calculate
			return pow(base, static_cast<B>(exp.raw_value() / FRAC));
		}

		// For negative bases we do not support fractional exponents.
		// Technically fractions with odd denominators could work,
		// but that's too much work to figure out.
		assert(base > Fixed(0));

		// exp2(log2(base) * exp). The product can exceed the range of the type while the result is representable
		// (e.g. tiny results), so it's calculated in the intermediate type and saturated.
		const I product = static_cast<I>(log2(base).raw_value()) * exp.raw_value();
		I exponent;
		if constexpr(R)
			exponent = (product + (I{1} << (F - 1))) >> F;
		else
			exponent = product / (I{1} << F); // truncate towards zero
		if(exponent > static_cast<I>(std::numeric_limits<B>::max()))
			return std::numeric_limits<Fixed>::max();
		if(exponent < static_cast<I>(std::numeric_limits<B>::lowest()))
			return Fixed(0);
		return exp2(Fixed::from_raw_value(static_cast<B>(exponent)));
	}

	/// e^x. Results too large to represent saturate to the maximum.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> exp(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;
		constexpr auto max = std::numeric_limits<Fixed>::max();

		// e^x > 2^integral_bits > max  <=>  x > integral_bits * ln(2)
		constexpr int32_t integral_bits = std::numeric_limits<B>::digits - static_cast<int32_t>(F);
		constexpr auto ln2 = Fixed::template from_fixed_point<63>(int64_t{6393154322601327830}); // 0.69314718055994530942
		constexpr auto overflow_threshold = ln2 * integral_bits;
		if(x >= overflow_threshold) [[unlikely]]
			return max;

		if constexpr(std::is_signed_v<B>)
		{
			// While e^-x is representable, 1 / e^-x is the most precise
			if(x < Fixed(0) && x > -overflow_threshold)
				return Fixed(1) / exp(-x);
		}

		// x = n + f, with integer n and f in [0, 1)
		const auto [n, f] = detail::split_floor(x);
		assert(f >= Fixed(0) && f < Fixed(1));

		// e^f in [1, e), with M fraction bits
		using S = std::make_signed_t<B>;
		constexpr int32_t M = detail::poly_bits<B, 2, F>;
		constexpr auto multiply = detail::poly_multiply<I, M, S>;
		constexpr auto coefficient = detail::poly_coefficient<B, M>;
		constexpr S fA = coefficient( int64_t{128239257017632854}, 63); // 1.3903728105644451e-2
		constexpr S fB = coefficient( int64_t{320978614890280666}, 63); // 3.4800571158543038e-2
		constexpr S fC = coefficient(int64_t{1571680799599592947}, 63); // 1.7040197373796334e-1
		constexpr S fD = coefficient(int64_t{4603349000587966862}, 63); // 4.9909609871464493e-1
		constexpr S fE = coefficient(int64_t{4612052447974689712}, 62); // 1.0000794567422495
		constexpr S fF = coefficient(int64_t{9223361618412247875}, 63); // 9.9999887043019773e-1
		const auto fm = static_cast<S>(static_cast<S>(f.raw_value()) << (M - static_cast<int32_t>(F)));
		// Estrin's scheme: shorter dependency chains than Horner's
		const S f2 = multiply(fm, fm);
		const S f4 = multiply(f2, f2);
		const S exp_f = multiply(multiply(fA, fm) + fB, f4) + multiply(multiply(fC, fm) + fD, f2) + (multiply(fE, fm) + fF);

		// e^n: for n < 0 (tiny results) as (1/e)^-n, whose powers shrink so they cannot overflow
		constexpr auto inv_e = Fixed::template from_fixed_point<63>(int64_t{3393088950634442637}); // 0.36787944117144232160
		const Fixed exp_n = (n >= 0) ? pow(Fixed::e(), n) : pow(inv_e, -n);

		// e^n * e^f, saturating in case the approximation error pushes the result just past the maximum
		const I product = static_cast<I>(exp_n.raw_value()) * exp_f;
		const I result = (product + (I{1} << (M - 1))) >> M;
		return result > static_cast<I>(max.raw_value()) ? max : Fixed::from_raw_value(static_cast<B>(result));
	}

	/// 2^x. Results too large to represent saturate to the maximum.
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> exp2(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;

		// x = n + f, with integer n and f in [0, 1)
		const auto [n, f] = detail::split_floor(x);
		assert(f >= Fixed(0) && f < Fixed(1));

		// 2^n * 2^f is a shift of 2^f
		if(n >= 0 && n > std::numeric_limits<B>::digits - static_cast<int32_t>(F) - 1) [[unlikely]]
			return std::numeric_limits<Fixed>::max(); // 2^f >= 1 would be shifted out of range

		// 2^f in [1, 2), with M fraction bits
		using S = std::make_signed_t<B>;
		constexpr int32_t M = detail::poly_bits<B, 1, F>;
		constexpr auto multiply = detail::poly_multiply<I, M, S>;
		constexpr auto coefficient = detail::poly_coefficient<B, M>;
		constexpr S fA = coefficient(  int64_t{17491766697771214}, 63); // 1.8964611454333148e-3
		constexpr S fB = coefficient(  int64_t{82483038782406547}, 63); // 8.9428289841091295e-3
		constexpr S fC = coefficient( int64_t{515275173969157690}, 63); // 5.5866246304520701e-2
		constexpr S fD = coefficient(int64_t{2214897896212987987}, 63); // 2.4013971109076949e-1
		constexpr S fE = coefficient(int64_t{6393224161192452326}, 63); // 6.9315475247516736e-1
		constexpr S fF = coefficient(int64_t{9223371050976163566}, 63); // 9.9999989311082668e-1
		const auto fm = static_cast<S>(static_cast<S>(f.raw_value()) << (M - static_cast<int32_t>(F)));
		// Estrin's scheme: shorter dependency chains than Horner's
		const S f2 = multiply(fm, fm);
		const S f4 = multiply(f2, f2);
		const S exp2_f = multiply(multiply(fA, fm) + fB, f4) + multiply(multiply(fC, fm) + fD, f2) + (multiply(fE, fm) + fF);

		// Result in QF: exp2_f * 2^n / 2^(M-F), rounded to nearest. exp2_f is positive and below 2^(M+1),
		// so this is done in the unsigned type of the base's width (no wider arithmetic needed).
		// n is below the range checked above, and results below 2^-(F+1) round to zero, so the shift fits in 32 bits.
		if(n < -static_cast<B>(F) - 1) [[unlikely]]
			return Fixed(0);
		using U = std::make_unsigned_t<B>;
		const auto mantissa = static_cast<U>(exp2_f);
		const int32_t shift = M - static_cast<int32_t>(F) - static_cast<int32_t>(n);
		if(shift <= 0)
			return Fixed::from_raw_value(static_cast<B>(static_cast<U>(mantissa << -shift)));
		return Fixed::from_raw_value(static_cast<B>(static_cast<U>(static_cast<U>(mantissa >> (shift - 1)) + 1u) >> 1));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> expm1(fixed<B, I, F, R> x) noexcept
	{
		return exp(x) - 1;
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> log2(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;
		assert(x > Fixed(0));

		// The polynomial is evaluated with M fraction bits (see detail::poly_bits). Its argument t = x - 1 is in [0, 1),
		// and all intermediate values are below 2, so it needs just one integral bit.
		using S = std::make_signed_t<B>;
		constexpr int32_t M = detail::poly_bits<B, 1, F>;
		constexpr auto multiply = detail::poly_multiply<I, M, S>;
		constexpr auto coefficient = detail::poly_coefficient<B, M>;

		// Normalize input to the [1:2) domain, in QM: move the highest bit to the top, then down to bit M
		using U = std::make_unsigned_t<B>;
		constexpr int32_t top = std::numeric_limits<U>::digits - 1;
		const auto value = static_cast<U>(x.raw_value());
		const int32_t leading_zeros = std::countl_zero(value);
		const int32_t highest = top - leading_zeros;
		const auto mantissa = static_cast<S>(static_cast<U>(static_cast<U>(value << leading_zeros) >> (top - M)));
		assert(mantissa >= (S{1} << M) && mantissa - (S{1} << M) < (S{1} << M));
		const auto t = static_cast<S>(mantissa - (S{1} << M));

		// Fifth-order polynomial approximation of log2(1 + t), evaluated with Estrin's scheme (shorter dependency chains than Horner's).
		// Mathematically identical to the original polynomial in x = 1 + t:
		//   4.4873610194131727e-2 x^5 - 4.1656368651734915e-1 x^4 + 1.6311487636297217 x^3
		//   - 3.5507929249026341 x^2 + 5.0917108110420042 x - 2.8003640347009253
		constexpr S c0 = coefficient(     int64_t{57824754770287}, 62); //  1.253874494907734e-05
		constexpr S c1 = coefficient( int64_t{6648596514624850418}, 62); //  1.441684557027163
		constexpr S c2 = coefficient(-int64_t{3265039810578835332}, 62); // -0.70799265117624666
		constexpr S c3 = coefficient( int64_t{1907532238906173757}, 62); //  0.41363011950164236
		constexpr S c4 = coefficient( -int64_t{886345925253438523}, 62); // -0.1921956355466905
		constexpr S c5 = coefficient(  int64_t{206943000728637990}, 62); //  0.044873610194131726
		const S t2 = multiply(t, t);
		const S t4 = multiply(t2, t2);
		const S fraction = multiply(multiply(c5, t) + c4, t4) + multiply(multiply(c3, t) + c2, t2) + (multiply(c1, t) + c0);

		// Integral part plus the fraction, rounded from QM to QF
		constexpr int32_t shift = M - static_cast<int32_t>(F);
		const I fraction_f = (shift == 0) ? I{fraction} : ((static_cast<I>(fraction) + (I{1} << (shift > 0 ? shift - 1 : 0))) >> shift);
		return Fixed::from_raw_value(static_cast<B>((static_cast<I>(highest - static_cast<int32_t>(F)) << F) + fraction_f));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> log(fixed<B, I, F, R> x) noexcept
	{
		// ln(x) = log2(x) * ln(2)
		return detail::multiply_by_constant<int64_t{6393154322601327830}>(log2(x)); // ln(2) = 0.69314718055994530942
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> log10(fixed<B, I, F, R> x) noexcept
	{
		// log10(x) = log2(x) * log10(2)
		return detail::multiply_by_constant<int64_t{2776511644261678566}>(log2(x)); // log10(2) = 0.30102999566398119521
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> log1p(fixed<B, I, F, R> x) noexcept
	{
		return log(1 + x);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> cbrt(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;

		if(x == Fixed(0))
			return x;
		if(x < Fixed(0))
			return -cbrt(-x);
		assert(x >= Fixed(0));

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
		const W shifted = static_cast<W>(static_cast<W>(x.raw_value()) << a);
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

		return Fixed::from_raw_value(static_cast<B>(root << skip));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> sqrt(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;

		assert(x >= Fixed(0));
		if(x == Fixed(0))
			return x;

		// Finding the square root of an integer in base-2, from:
		// https://en.wikipedia.org/wiki/Methods_of_computing_square_roots#Binary_numeral_system_.28base_2.29

		// Shift by F first because it's fixed-point.
		I num = I{x.raw_value()} << F;
		I res = 0;

		// "bit" starts at the greatest power of four that's less than the argument.
		for(
			I bit = I{1} << ((detail::find_highest_bit(x.raw_value()) + F) / 2 * 2);
			bit != 0;
			bit >>= 2
		)
		{
			const I val = res + bit;
			res >>= 1;
			if(num >= val)
			{
				num -= val;
				res += bit;
			}
		}

		// Round the last digit up if necessary
		if(num > res)
			res++;

		return Fixed::from_raw_value(static_cast<B>(res));
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
		/// x / (π/2) modulo 4: the angle in quarter turns, as a raw value with F fraction bits in [0, 4 * 2^F).
		/// A single multiplication with a precise 2/π, so there is no division and the reduction stays accurate
		/// for large arguments. The modulo is a mask of the two's complement intermediate value.
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr I quarter_turns(const fixed<B, I, F, R> x) noexcept
		{
			constexpr int32_t P = constant_precision<B, I>;
			constexpr I two_over_pi = precise_constant<B, I, int64_t{5871781006564002453}>; // 2/π = 0.63661977236758134308
			const I turns = (static_cast<I>(x.raw_value()) * two_over_pi + (I{1} << (P - 1))) >> P;
			return turns & ((I{4} << F) - 1);
		}

		/// sin(u * π/2) for a raw angle `u` in quarter turns in [0, 4 * 2^F)
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fixed<B, I, F, R> sin_quarter_turns(I u) noexcept
		{
			// This sine uses a fifth-order curve-fitting approximation originally
			// described by Jasper Vijn on coranac.com which has a worst-case
			// relative error of 0.07% (over [-pi:pi]).
			using Fixed = fixed<B, I, F, R>;
			constexpr I one = I{1} << F;

			bool negative = false;
			if(u > 2 * one)
			{
				// Reduce domain to [0..2].
				negative = true;
				u -= 2 * one;
			}

			if(u > one)
			{
				// Reduce domain to [0..1].
				u = 2 * one - u;
			}

			const auto x = Fixed::from_raw_value(static_cast<B>(u));
			const Fixed x2 = x*x;
			// Compile-time constants. Subtracted with modular arithmetic, so this is a valid constant expression
			// even for types that cannot represent them (e.g. a single integral bit), like at runtime.
			using U = std::make_unsigned_t<B>;
			constexpr auto subtract = [](const Fixed x, const Fixed y)
			{
				return Fixed::from_raw_value(static_cast<B>(static_cast<U>(static_cast<U>(x.raw_value()) - static_cast<U>(y.raw_value()))));
			};
			constexpr Fixed a = Fixed::pi();
			constexpr Fixed b = subtract(Fixed::two_pi(), Fixed(5));
			constexpr Fixed c = subtract(Fixed::pi(), Fixed(3));
			const Fixed result = x * (a - x2*(b - x2*c))/2;
			// (Negated via the raw value, so this compiles for unsigned base types as well)
			return negative ? Fixed::from_raw_value(static_cast<B>(B{0} - result.raw_value())) : result;
		}
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> sin(fixed<B, I, F, R> x) noexcept
	{
		return detail::sin_quarter_turns<B, I, F, R>(detail::quarter_turns(x));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> cos(fixed<B, I, F, R> x) noexcept
	{
		// cos(x) = sin(x + π/2): one more quarter turn
		constexpr I mask = (I{4} << F) - 1;
		return detail::sin_quarter_turns<B, I, F, R>((detail::quarter_turns(x) + (I{1} << F)) & mask);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> tan(fixed<B, I, F, R> x) noexcept
	{
		constexpr I mask = (I{4} << F) - 1;
		const I u = detail::quarter_turns(x);
		const auto cx = detail::sin_quarter_turns<B, I, F, R>((u + (I{1} << F)) & mask);

		// Tangent goes to infinity at 90 and -90 degrees.
		// We can't represent that with fixed-point maths.
		assert(abs(cx).raw_value() > 1);

		return detail::sin_quarter_turns<B, I, F, R>(u) / cx;
	}

	namespace detail
	{

		/// Calculates atan(x) assuming that x is in the range [0,1].
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] FPM_DETAIL_GCC_NOINLINE inline constexpr fixed<B, I, F, R> atan_sanitized(fixed<B, I, F, R> x) noexcept
		{
			using Fixed = fixed<B, I, F, R>;
			assert(x >= Fixed(0) && x <= Fixed(1));

			// Evaluated with extra fraction bits (see poly_bits): all values are within [-1, 1]
			using S = std::make_signed_t<B>;
			constexpr int32_t M = poly_bits<B, 1, F>;
			constexpr auto multiply = poly_multiply<I, M, S>;
			constexpr auto coefficient = poly_coefficient<B, M>;
			constexpr S fA = coefficient(  int64_t{716203666280654660}, 63); //  0.0776509570923569
			constexpr S fB = coefficient(-int64_t{2651115102768076601}, 63); // -0.287434475393028
			constexpr S fC = coefficient( int64_t{9178930894564541004}, 63); //  0.995181681698119  (PI/4 - A - B)

			const auto xm = static_cast<S>(static_cast<S>(x.raw_value()) << (M - static_cast<int32_t>(F)));
			const S xx = multiply(xm, xm);
			const S result = multiply(static_cast<S>(multiply(static_cast<S>(multiply(fA, xx) + fB), xx) + fC), xm);

			// Round from QM to QF, in S: the result is at most 1 (2^M), so adding half a unit cannot overflow
			constexpr int32_t shift = M - static_cast<int32_t>(F);
			if constexpr(shift == 0)
				return Fixed::from_raw_value(static_cast<B>(result));
			else
				return Fixed::from_raw_value(static_cast<B>(static_cast<S>(result + (S{1} << (shift - 1))) >> shift));
		}

		/// Calculate atan(y / x), assuming x != 0.
		///
		/// If x is very, very small, y/x can easily overflow the fixed-point range.
		/// If q = y/x and q > 1, atan(q) would calculate atan(1/q) as intermediate step
		/// anyway. We can shortcut that here and avoid the loss of information, thus
		/// improving the accuracy of atan(y/x) for very small x.
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr fixed<B, I, F, R> atan_div(fixed<B, I, F, R> y, fixed<B, I, F, R> x) noexcept
		{
			using Fixed = fixed<B, I, F, R>;
			assert(x != Fixed(0));

			// Make sure y and x are positive.
			// If y / x is negative (when y or x, but not both, are negative), negate the result to
			// keep the correct outcome.
			if(y < Fixed(0))
			{
				if(x < Fixed(0))
				{
					return atan_div(-y, -x);
				}
				return -atan_div(-y, x);
			}
			if(x < Fixed(0))
			{
				return -atan_div(y, -x);
			}
			assert(y >= Fixed(0));
			assert(x >  Fixed(0));

			if(y > x)
			{
				return Fixed::half_pi() - detail::atan_sanitized(x / y);
			}
			return detail::atan_sanitized(y / x);
		}

	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> atan(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;
		if(x < Fixed(0))
		{
			return -atan(-x);
		}

		if(x > Fixed(1))
		{
			return Fixed::half_pi() - detail::atan_sanitized(1 / x);
		}

		return detail::atan_sanitized(x);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> asin(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;
		assert(x <= Fixed(+1));
		if constexpr(std::is_signed_v<B>)
			assert(x >= Fixed(-1));

		const auto yy = 1 - x * x;
		if(yy == Fixed(0))
		{
			return copysign(Fixed::half_pi(), x);
		}
		return detail::atan_div(x, sqrt(yy));
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> acos(fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;
		assert(x <= Fixed(+1));
		if constexpr(std::is_signed_v<B>)
			assert(x >= Fixed(-1));

		if(std::is_signed_v<B> && x == -Fixed(1))
		{
			return Fixed::pi();
		}
		const auto yy = 1 - x * x;
		return 2*detail::atan_div(sqrt(yy), Fixed(1) + x);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> atan2(fixed<B, I, F, R> y, fixed<B, I, F, R> x) noexcept
	{
		using Fixed = fixed<B, I, F, R>;
		if(x == Fixed(0))
		{
			assert(y != Fixed(0));
			return (y > Fixed(0)) ? Fixed::half_pi() : -Fixed::half_pi();
		}

		auto ret = detail::atan_div(y, x);

		if(x < Fixed(0))
		{
			return (y >= Fixed(0)) ? ret + Fixed::pi() : ret - Fixed::pi();
		}
		return ret;
	}

#pragma endregion

}
