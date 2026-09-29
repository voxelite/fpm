#pragma once

#include <cassert>
#include <compare>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

// 128-bit integers: the intermediate type for 64-bit base types.
// Define FPM_INT128 to provide your own type, or FPM_NO_INT128 to not use 128-bit integers.
// Without a 128-bit integer type (e.g. on most 32-bit targets) the 64-bit base types are not available.
// The type may be a class: it is only assumed to behave like a signed integer, with a specialization
// of `std::numeric_limits` (type traits like `std::is_signed` do not recognize classes).
#if !defined(FPM_INT128) && !defined(FPM_NO_INT128)
	#if defined(_MSC_VER)
		// MSVC and clang-cl, for 64-bit Windows only (like most 32-bit targets, 32-bit Windows has no 128-bit integers).
		// clang-cl has `__int128` as well, but the MSVC runtime lacks the functions for its division.
		#if defined(_WIN64) && __has_include(<__msvc_int128.hpp>)
			#include <__msvc_int128.hpp>
			#define FPM_INT128 ::std::_Signed128
		#endif
	#elif defined(__SIZEOF_INT128__)
		#define FPM_INT128 __int128
	#endif
#endif

// Results that a type cannot represent are not defined, like the overflow of integers. Two options change that,
// each for all of a program (they change what the functions of this library do):
// - FPM_DEFINED_OVERFLOW defines them: the results of the arithmetic operators wrap around, for signed types as well,
//   and the ones of the mathematical functions saturate. This is the same for every platform, compiler and
//   optimization level. It costs some speed in the mathematical functions.
// - FPM_CHECK_OVERFLOW makes such a result of an operator an error, which `assert` reports (so in builds with
//   assertions, and in constant expressions).

namespace fpm
{
	namespace detail
	{
#ifdef FPM_DEFINED_OVERFLOW
		inline constexpr bool defined_overflow = true;
#else
		inline constexpr bool defined_overflow = false;
#endif
#ifdef FPM_CHECK_OVERFLOW
		inline constexpr bool checked_overflow = true;
#else
		inline constexpr bool checked_overflow = false;
#endif
	}

#ifdef FPM_INT128
	using int128_t = FPM_INT128;
	static_assert(sizeof(int128_t) > sizeof(int64_t));
	static_assert(std::numeric_limits<int128_t>::is_signed);
#endif

	namespace detail
	{
		/// value < 0 (without warnings for unsigned types)
		template<typename T>
		[[nodiscard]] inline constexpr bool is_negative(const T value) noexcept
		{
			if constexpr(std::numeric_limits<T>::is_signed)
				return value < 0;
			else
				return false;
		}

		/// 2^exponent, for an exponent of 0 at least
		template<std::floating_point T>
		[[nodiscard]] inline constexpr T power_of_two(const int32_t exponent) noexcept
		{
			T result{1};
			for(int32_t i = 0; i < exponent; ++i)
				result *= T{2};
			return result;
		}

		/// val modulo 2^bits: val - floor(val / 2^bits) * 2^bits, which is exact. In [0, 2^bits]: 2^bits only for
		/// a negative number that is too small to make a difference. 0 for a number that is not finite.
		template<std::floating_point T>
		[[nodiscard]] inline constexpr T modulo_power_of_two(const T val, const int32_t bits) noexcept
		{
			// From here on every number is an integer (as the difference between consecutive numbers is 1 at least)
			constexpr int32_t digits = std::numeric_limits<T>::digits;
			constexpr T integers = power_of_two<T>(digits - 1);
			const T scale = power_of_two<T>(bits);
			T quotient = val / scale;
			if(!(quotient > -integers && quotient < integers)) [[unlikely]]
				return T{0};

			if constexpr(digits > 64)
			{
				// Too large to convert to an integer: without the multiples of 2^62 first, which is exact
				constexpr T chunk = power_of_two<T>(62);
				if(quotient >= chunk || quotient <= -chunk) [[unlikely]]
					quotient -= static_cast<T>(static_cast<int64_t>(quotient / chunk)) * chunk;
			}
			T whole = static_cast<T>(static_cast<int64_t>(quotient)); // towards zero
			if(whole > quotient)
				whole -= T{1};
			return (quotient - whole) * scale;
		}

		/// value / 2^bits for bits >= 1: rounded to nearest (ties away from zero), or truncated towards zero.
		/// An arithmetic shift of the biased value instead of a division: the same result, but fast for 128-bit
		/// class types as well. The biased value must fit: |value| + 2^bits may not overflow.
		template<bool Round, typename T>
		[[nodiscard]] inline constexpr T shift_right(const T value, const uint32_t bits) noexcept
		{
			// The shift rounds towards negative infinity. Truncating a negative value towards zero takes a bias of
			// 2^bits - 1; rounding to nearest takes half a unit for positive values, which leaves half a unit minus one.
			const T half = static_cast<T>(T(1) << (bits - 1));
			T bias = Round ? half : T(0);
			if(is_negative(value))
				bias = static_cast<T>((half - 1) + (Round ? T(0) : half));
			return static_cast<T>((value + bias) >> bits);
		}

		/// Like `shift_right`, but for any value: nothing can overflow, at the cost of a few more instructions
		template<bool Round, typename T>
		[[nodiscard]] inline constexpr T shift_right_any(const T value, const uint32_t bits) noexcept
		{
			// value = floor * 2^bits + rest, with 0 <= rest < 2^bits
			const T floor = static_cast<T>(value >> bits);
			const T rest = static_cast<T>(value & static_cast<T>((T(1) << bits) - 1));
			bool up = false;
			if constexpr(Round)
			{
				const T half = static_cast<T>(T(1) << (bits - 1));
				up = rest > half || (rest == half && !is_negative(value));
			}
			else
			{
				up = is_negative(value) && rest != 0;
			}
			return static_cast<T>(floor + (up ? 1 : 0));
		}

		/// The result of an operator must be one that the type can represent: see FPM_CHECK_OVERFLOW
		inline constexpr void check_overflow([[maybe_unused]] const bool fits) noexcept
		{
			assert(fits);
		}

		/// |value| in 64 bits, which is any magnitude of an integer
		template<std::integral T>
		[[nodiscard]] inline constexpr uint64_t magnitude_of(const T value) noexcept
		{
			return is_negative(value) ? uint64_t{0} - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
		}

		/// The largest magnitude of the type B, for a negative or a positive value
		template<std::integral B>
		[[nodiscard]] inline constexpr uint64_t largest_magnitude(const bool negative) noexcept
		{
			if(negative)
				return std::is_signed_v<B> ? magnitude_of(std::numeric_limits<B>::lowest()) : uint64_t{0};
			return static_cast<uint64_t>(std::numeric_limits<B>::max());
		}

		/// a + b. With FPM_DEFINED_OVERFLOW it wraps around: in the unsigned type, where that is defined.
		template<std::integral B>
		[[nodiscard]] inline constexpr B add(const B a, const B b) noexcept
		{
			if constexpr(defined_overflow || checked_overflow)
			{
				using U = std::make_unsigned_t<std::common_type_t<B, unsigned int>>;
				const auto result = static_cast<B>(static_cast<U>(a) + static_cast<U>(b));
				if constexpr(checked_overflow)
				{
					// (For signed types: the operands have the same sign, and the result has the other one)
					if constexpr(std::is_signed_v<B>)
						check_overflow(((a ^ result) & (b ^ result)) >= 0);
					else
						check_overflow(result >= a);
				}
				return result;
			}
			else
				return static_cast<B>(a + b);
		}

		/// a - b. With FPM_DEFINED_OVERFLOW it wraps around.
		template<std::integral B>
		[[nodiscard]] inline constexpr B subtract(const B a, const B b) noexcept
		{
			if constexpr(defined_overflow || checked_overflow)
			{
				using U = std::make_unsigned_t<std::common_type_t<B, unsigned int>>;
				const auto result = static_cast<B>(static_cast<U>(a) - static_cast<U>(b));
				if constexpr(checked_overflow)
				{
					// (For signed types: the operands have different signs, and the result has the sign of b)
					if constexpr(std::is_signed_v<B>)
						check_overflow(((a ^ b) & (a ^ result)) >= 0);
					else
						check_overflow(a >= b);
				}
				return result;
			}
			else
				return static_cast<B>(a - b);
		}

		/// raw * y. With FPM_DEFINED_OVERFLOW it wraps around, for any combination of signedness.
		template<std::integral B, std::integral T>
		[[nodiscard]] inline constexpr B multiply_by_integer(const B raw, const T y) noexcept
		{
			if constexpr(defined_overflow || checked_overflow)
			{
				using U = std::make_unsigned_t<std::common_type_t<B, T, unsigned int>>;
				if constexpr(checked_overflow)
				{
					const auto limit = largest_magnitude<B>(is_negative(raw) != is_negative(y));
					check_overflow(raw == 0 || y == 0 || magnitude_of(raw) <= limit / magnitude_of(y));
				}
				return static_cast<B>(static_cast<U>(raw) * static_cast<U>(y));
			}
			else
				return static_cast<B>(raw * y);
		}

		/// The value of a wider type in the type B: without the bits that do not fit
		template<std::integral B, typename I>
		[[nodiscard]] inline constexpr B narrow(const I value) noexcept
		{
			if constexpr(checked_overflow)
				check_overflow(value >= static_cast<I>(std::numeric_limits<B>::lowest()) && value <= static_cast<I>(std::numeric_limits<B>::max()));
			return static_cast<B>(value);
		}

		/// raw / y truncated towards zero, for any combination of signedness: the usual arithmetic conversions
		/// would convert a negative value to a (large) unsigned value. Results that do not fit wrap.
		template<std::integral B, std::integral T>
		[[nodiscard]] inline constexpr B divide_by_integer(const B raw, const T y) noexcept
		{
			if constexpr(std::is_signed_v<B> == std::is_signed_v<T>)
			{
				if constexpr(std::is_signed_v<B> && (defined_overflow || checked_overflow))
				{
					// The one quotient that does not fit: of the lowest value, which is itself when it wraps around
					using U = std::make_unsigned_t<std::common_type_t<B, unsigned int>>;
					if(y == T{-1}) [[unlikely]]
						return static_cast<B>(U{0} - static_cast<U>(raw));
				}
				return static_cast<B>(raw / y);
			}
			else
			{
				// The quotient of the magnitudes, in an unsigned type that holds both
				using U = std::make_unsigned_t<std::common_type_t<B, T>>;
				const auto magnitude = [](const auto value)
				{
					return is_negative(value) ? static_cast<U>(U{0} - static_cast<U>(value)) : static_cast<U>(value);
				};
				const auto quotient = static_cast<U>(magnitude(raw) / magnitude(y));
				return static_cast<B>(is_negative(raw) != is_negative(y) ? static_cast<U>(U{0} - quotient) : quotient);
			}
		}

		/// Whether a raw value with `FractionBits` fraction bits equals an integer. Exact for any combination
		/// of signedness, and for integers beyond the range of the fixed-point type.
		template<uint32_t FractionBits, std::integral B, std::integral T>
		[[nodiscard]] inline constexpr bool equals_integer(const B raw, const T y) noexcept
		{
			// No fraction, and the same integer: with the same sign, and the same value in an unsigned type that holds both.
			// (Without short-circuit evaluation, so there are no branches.)
			using U = std::make_unsigned_t<std::common_type_t<B, T>>;
			const auto floor = static_cast<B>(raw >> FractionBits); // arithmetic shift: rounds towards negative infinity
			return (static_cast<B>(floor << FractionBits) == raw)
				& (is_negative(floor) == is_negative(y))
				& (static_cast<U>(floor) == static_cast<U>(y));
		}

		/// Compares a raw value with `FractionBits` fraction bits with an integer. Exact for any combination
		/// of signedness, and for integers beyond the range of the fixed-point type.
		template<uint32_t FractionBits, std::integral B, std::integral T>
		[[nodiscard]] inline constexpr std::strong_ordering compare_with_integer(const B raw, const T y) noexcept
		{
			// x = floor + fraction with 0 <= fraction < 1: the fraction only matters if floor == y
			const auto floor = static_cast<B>(raw >> FractionBits); // arithmetic shift: rounds towards negative infinity
			if(is_negative(floor) != is_negative(y))
				return is_negative(floor) ? std::strong_ordering::less : std::strong_ordering::greater;

			// With the same sign, the conversion to an unsigned type that holds both keeps their order
			using U = std::make_unsigned_t<std::common_type_t<B, T>>;
			const auto order = static_cast<U>(floor) <=> static_cast<U>(y);
			if(order != 0)
				return order;
			const bool has_fraction = static_cast<B>(floor << FractionBits) != raw;
			return has_fraction ? std::strong_ordering::greater : std::strong_ordering::equal;
		}
	}

	//! Fixed-point number type
	//! \tparam BaseType         the base integer type used to store the fixed-point number. This can be a signed or unsigned type.
	//! \tparam IntermediateType the integer type used to store intermediate results during calculations.
	//! \tparam FractionBits     the number of bits of the BaseType used to store the fraction. At least one bit
	//!                          (besides the sign) remains for the integral part, so every type can represent 1.
	//! \tparam EnableRounding   enable rounding of LSB for multiplication, division, and type conversion
	template<typename BaseType, typename IntermediateType, uint32_t FractionBits, bool EnableRounding = true>
	struct fixed
	{
		static_assert(std::is_integral_v<BaseType>, "BaseType must be an integral type");
		static_assert(FractionBits > 0, "FractionBits must be greater than zero");
		static_assert(FractionBits < static_cast<uint32_t>(std::numeric_limits<BaseType>::digits), "BaseType must have at least one integral bit (besides the sign bit) next to the fraction");
		static_assert(sizeof(IntermediateType) > sizeof(BaseType), "IntermediateType must be larger than BaseType");
		static_assert(std::numeric_limits<IntermediateType>::is_signed == std::numeric_limits<BaseType>::is_signed, "IntermediateType must have same signedness as BaseType");

		// For introspection using `decltype(fixed<...>)`
		using base_type = BaseType;
		using intermediate_type = IntermediateType;
		static constexpr decltype(FractionBits) fraction_bits = FractionBits;
		static constexpr decltype(FractionBits) integral_bits = (sizeof(BaseType) * 8) - FractionBits;
		static constexpr decltype(EnableRounding) enable_rounding = EnableRounding;

		/// 2^FractionBits in the intermediate type, so calculations with it are done in that type
		static constexpr IntermediateType FRACTION_MULT = IntermediateType(1) << FractionBits;

#pragma region Constructors

	private:
		/// 2^FractionBits in the base type: the raw value of 1
		static constexpr BaseType RAW_ONE = static_cast<BaseType>(BaseType(1) << FractionBits);

		struct raw_construct_tag{};
		inline constexpr fixed(const BaseType val, raw_construct_tag) noexcept : m_value(val) {}

	public:
		inline constexpr fixed() noexcept = default;

		/// Converts an integral number to the fixed-point type.
		/// Like static_cast, this truncates bits that don't fit.
		template<std::integral T>
		inline constexpr explicit fixed(const T val) noexcept
			: m_value(integral_to_raw(val))
		{}

		/// Converts a (finite) floating-point number to the fixed-point type.
		/// This truncates bits that don't fit, like the other conversions: a number beyond the range wraps around.
		template<std::floating_point T>
		inline constexpr explicit fixed(const T val) noexcept
			: m_value(floating_to_raw(val))
		{}

		/// Constructs from another fixed-point type with possibly different underlying representation.
		/// Like static_cast, this truncates bits that don't fit.
		template<typename B, typename I, uint32_t F, bool R>
		inline constexpr explicit fixed(const fixed<B,I,F,R> val) noexcept
			: m_value(from_fixed_point<F>(val.raw_value()).raw_value())
		{}

#pragma endregion

#pragma region Conversion Operators

		/// Explicit conversion to a floating-point type
		template<std::floating_point T>
		[[nodiscard]] inline constexpr explicit operator T() const noexcept
		{
			return static_cast<T>(m_value) / static_cast<T>(RAW_ONE);
		}

		/// Explicit conversion to an integral type. Like for floating-point numbers, the fraction is truncated.
		template<std::integral T>
		[[nodiscard]] inline constexpr explicit operator T() const noexcept
		{
			return static_cast<T>(m_value / RAW_ONE);
		}

#pragma endregion

#pragma region Raw Values

		/// Returns the raw underlying value of this type.
		/// Do not use this unless you know what you're doing.
		[[nodiscard]] inline constexpr BaseType raw_value() const noexcept
		{
			return m_value;
		}


		//! Constructs a fixed-point number from another fixed-point number.
		//! \tparam NumFractionBits the number of bits used by the fraction in \a value.
		//! \param value the integer fixed-point number
		template<uint32_t NumFractionBits, typename T>
			requires(NumFractionBits > FractionBits)
		[[nodiscard]] inline static constexpr fixed from_fixed_point(T value) noexcept
		{
			// Rounded (or truncated) in the type of the value: fewer fraction bits need no wider type.
			// For signed types, a value as wide as the intermediate type is signed as well (a hexadecimal literal
			// with the sign bit set is unsigned). Narrower unsigned values keep their value.
			using S = std::conditional_t<(std::is_signed_v<BaseType> && sizeof(T) >= sizeof(IntermediateType)), std::make_signed_t<T>, T>;
			return fixed(
				static_cast<BaseType>(detail::shift_right_any<EnableRounding>(static_cast<S>(value), NumFractionBits - FractionBits)),
				raw_construct_tag{}
			);
		}

		template<uint32_t NumFractionBits, typename T>
			requires(NumFractionBits <= FractionBits)
		[[nodiscard]] inline static constexpr fixed from_fixed_point(T value) noexcept
		{
			// In an unsigned type that holds both types: the shift is modular, so this truncates the bits
			// that don't fit (like the other conversions)
			using U = std::make_unsigned_t<std::common_type_t<T, BaseType>>;
			return fixed(
				static_cast<BaseType>(static_cast<U>(static_cast<U>(value) << (FractionBits - NumFractionBits))),
				raw_construct_tag{}
			);
		}

		/// Constructs a fixed-point number from its raw underlying value.
		/// Do not use this unless you know what you're doing.
		[[nodiscard]] inline static constexpr fixed from_raw_value(BaseType value) noexcept
		{
			return fixed(value, raw_construct_tag{});
		}

#pragma endregion

#pragma region Custom Fractions

		//! Constructs a fixed-point number from integer part and fraction part.
		//! \tparam NumFraction the fraction in \a fraction_value.
		//! \param integer_value integer part
		//! \param fraction_value fraction value
		//!
		//! Creating a fixed-point number from decimal parts:
		//! \code
		//! // Create a fixed-point number representing 3.141592 (π approximation)
		//! // Using millionths as the fraction unit (denominator = 1,000,000)
		//! auto pi = fpm::fixed_16_16::from_custom_fraction<1000000>(3, 141592);
		//! // pi now represents 3.141592
		//! \endcode
		template<uint64_t NumFraction, std::integral T>
		[[nodiscard]] inline static constexpr fixed from_custom_fraction(T integer_value, T fraction_value) noexcept
		{
			static_assert(NumFraction > 0, "The fraction's denominator must be positive");

			// fraction_value * 2^(FractionBits+1) / NumFraction, truncated towards zero: one more bit than needed,
			// to correctly round the last bit of the result.
			// Calculated in the intermediate type, or in a 128-bit type if the denominator is too large for it
			// (a fraction value up to the denominator, times 2^(FractionBits+1), must fit), or else by long division.
			constexpr int32_t spare_bits = std::numeric_limits<IntermediateType>::digits - static_cast<int32_t>(FractionBits + 1);
			constexpr bool fits_intermediate = spare_bits >= 64 || (NumFraction >> spare_bits) == 0;
			IntermediateType two_frac_part;
			if constexpr(fits_intermediate)
			{
				two_frac_part = static_cast<IntermediateType>(
					(static_cast<IntermediateType>(fraction_value) << (FractionBits + 1)) / static_cast<IntermediateType>(NumFraction)
				);
			}
			else
			{
#ifdef FPM_INT128
				two_frac_part = static_cast<IntermediateType>(
					(static_cast<FPM_INT128>(fraction_value) << (FractionBits + 1)) / static_cast<FPM_INT128>(NumFraction)
				);
#else
				// Binary long division of the magnitude, in unsigned 64-bit arithmetic
				const bool negative = detail::is_negative(fraction_value);
				const auto magnitude = negative ? uint64_t{0} - static_cast<uint64_t>(fraction_value) : static_cast<uint64_t>(fraction_value);
				uint64_t remainder = magnitude % NumFraction;
				uint64_t quotient = magnitude / NumFraction;
				for(uint32_t i = 0; i < FractionBits + 1; ++i)
				{
					const bool bit = remainder >= NumFraction - remainder; // 2 * remainder >= NumFraction
					remainder = bit ? remainder - (NumFraction - remainder) : remainder * 2;
					quotient = (quotient << 1) | (bit ? 1 : 0);
				}
				two_frac_part = static_cast<IntermediateType>(negative ? uint64_t{0} - quotient : quotient);
#endif
			}

			const auto int_part = static_cast<IntermediateType>(static_cast<IntermediateType>(integer_value) << FractionBits);
			const IntermediateType frac_part = detail::shift_right<EnableRounding>(two_frac_part, 1);
			return fixed(
				static_cast<BaseType>(int_part + frac_part),
				raw_construct_tag{}
			);
		}
#pragma endregion

#pragma region Constants

		[[nodiscard]] inline static constexpr fixed e()       noexcept { return from_fixed_point<61>(int64_t{6267931151224907085}); }
		[[nodiscard]] inline static constexpr fixed pi()      noexcept { return from_fixed_point<61>(int64_t{7244019458077122842}); }
		[[nodiscard]] inline static constexpr fixed half_pi() noexcept { return from_fixed_point<62>(int64_t{7244019458077122842}); }
		[[nodiscard]] inline static constexpr fixed two_pi()  noexcept { return from_fixed_point<60>(int64_t{7244019458077122842}); }

#pragma endregion

		[[nodiscard]] inline constexpr explicit operator bool() const noexcept
		{
			return m_value != 0;
		}

#pragma region Arithmetic member operators

#pragma region Addition

		inline constexpr fixed& operator+=(const fixed& y) noexcept
		{
			m_value = detail::add(m_value, y.m_value);
			return *this;
		}

		template<std::integral I>
		inline constexpr fixed& operator+=(I y) noexcept
		{
			m_value = detail::add(m_value, integral_to_raw_checked(y));
			return *this;
		}

#pragma endregion

#pragma region Subtraction

		inline constexpr fixed& operator-=(const fixed& y) noexcept
		{
			m_value = detail::subtract(m_value, y.m_value);
			return *this;
		}

		template<std::integral I>
		inline constexpr fixed& operator-=(I y) noexcept
		{
			m_value = detail::subtract(m_value, integral_to_raw_checked(y));
			return *this;
		}

#pragma endregion

#pragma region Multiplication

		inline constexpr fixed& operator*=(const fixed& y) noexcept
		{
			// x * y / 2^FractionBits, with the product in the intermediate type
			const auto product = static_cast<IntermediateType>(static_cast<IntermediateType>(m_value) * static_cast<IntermediateType>(y.m_value));
			m_value = detail::narrow<BaseType>(detail::shift_right<EnableRounding>(product, FractionBits));
			return *this;
		}

		template<std::integral I>
		inline constexpr fixed& operator*=(I y) noexcept
		{
			m_value = detail::multiply_by_integer(m_value, y);
			return *this;
		}

#pragma endregion

#pragma region Division

		inline constexpr fixed& operator/=(const fixed& y) noexcept
		{
			assert(y.m_value != 0);
			// x * 2^FractionBits / y, with the numerator in the intermediate type
			if constexpr(EnableRounding)
			{
				// One more bit in the quotient, to correctly round the last bit of the result
				const auto quotient = static_cast<IntermediateType>((static_cast<IntermediateType>(m_value) << (FractionBits + 1)) / y.m_value);
				m_value = detail::narrow<BaseType>(detail::shift_right<true>(quotient, 1));
			}
			else
			{
				m_value = detail::narrow<BaseType>(static_cast<IntermediateType>((static_cast<IntermediateType>(m_value) << FractionBits) / y.m_value));
			}
			return *this;
		}

		template<std::integral I>
		inline constexpr fixed& operator/=(I y) noexcept
		{
			assert(y != 0);
			if constexpr(detail::checked_overflow)
			{
				// (The quotients that do not fit: a negative one for an unsigned type, and the one of the lowest value by -1)
				const auto quotient = detail::magnitude_of(m_value) / detail::magnitude_of(y);
				detail::check_overflow(quotient == 0 || quotient <= detail::largest_magnitude<BaseType>(detail::is_negative(m_value) != detail::is_negative(y)));
			}
			m_value = detail::divide_by_integer(m_value, y);
			return *this;
		}

#pragma endregion

#pragma endregion

	private:
		/// Raw value of an integer that is an operand of an operator: see FPM_CHECK_OVERFLOW
		template<std::integral T>
		[[nodiscard]] inline static constexpr BaseType integral_to_raw_checked(const T val) noexcept
		{
			const BaseType raw = integral_to_raw(val);
			if constexpr(detail::checked_overflow)
				detail::check_overflow(detail::equals_integer<FractionBits>(raw, val));
			return raw;
		}

		/// Raw value of an integer. Like static_cast, this truncates bits that don't fit (modular arithmetic, no overflow).
		template<std::integral T>
		[[nodiscard]] inline static constexpr BaseType integral_to_raw(const T val) noexcept
		{
			using U = std::make_unsigned_t<BaseType>;
			return static_cast<BaseType>(static_cast<U>(static_cast<U>(val) << FractionBits));
		}

		/// Raw value of a floating-point number: rounded to nearest (ties away from zero), or truncated.
		template<std::floating_point T>
		[[nodiscard]] inline static constexpr BaseType floating_to_raw(const T val) noexcept
		{
			// The conversion of a number beyond the range of the base type is undefined (and not the same for every platform).
			// The range is checked before the scaling, which is exact but could overflow (and is then not a constant expression).
			constexpr T limit = detail::power_of_two<T>(std::numeric_limits<BaseType>::digits - static_cast<int32_t>(FractionBits));
			constexpr T lowest = std::is_signed_v<BaseType> ? -limit : T{-1} / static_cast<T>(RAW_ONE);
			if(val < limit && (std::is_signed_v<BaseType> ? val >= lowest : val > lowest)) [[likely]]
			{
				const T scaled = val * static_cast<T>(RAW_ONE);
				const auto whole = static_cast<BaseType>(scaled); // truncated
				if constexpr(EnableRounding)
				{
					// By what is left, which is exact: the sum of a large number and a half is rounded itself.
					// (In the unsigned type, as the ends of the range wrap around.)
					using U = std::make_unsigned_t<std::common_type_t<BaseType, unsigned int>>;
					const T rest = scaled - static_cast<T>(whole);
					if(rest >= T{0.5})
						return static_cast<BaseType>(static_cast<U>(whole) + 1u);
					if(rest <= T{-0.5})
						return static_cast<BaseType>(static_cast<U>(whole) - 1u);
				}
				return whole;
			}
			return floating_to_raw_wrapped(val);
		}

		/// Raw value of a floating-point number beyond the range: modulo the range, like for an integer that does not fit
		template<std::floating_point T>
		[[nodiscard]] inline static constexpr BaseType floating_to_raw_wrapped(const T val) noexcept
		{
			assert(val - val == T{0}); // finite

			// The magnitude without the multiples of the range, which is exact. Then rounded like any other number,
			// in the unsigned type: up to the range itself, which is 0.
			using U = std::make_unsigned_t<std::common_type_t<BaseType, unsigned int>>;
			constexpr int32_t bits = std::numeric_limits<std::make_unsigned_t<BaseType>>::digits;
			const bool negative = val < T{0};
			const T scaled = detail::modulo_power_of_two(negative ? -val : val, bits - static_cast<int32_t>(FractionBits)) * static_cast<T>(RAW_ONE);
			auto result = static_cast<U>(scaled); // truncated
			if constexpr(EnableRounding)
			{
				if(scaled - static_cast<T>(result) >= T{0.5})
					++result;
			}
			return static_cast<BaseType>(negative ? U{0} - result : result);
		}

		BaseType m_value;
	};

#pragma region Convenience typedefs

#pragma region 8-bit Base
	using fixed_4_4 = fixed<int8_t, int16_t, 4>;
#pragma endregion

#pragma region 16-bit Base
	using fixed_8_8 = fixed<int16_t, int32_t, 8>;
#pragma endregion

#pragma region 32-bit Base
	using fixed_8_24  = fixed<int32_t, int64_t, 24>;
	using fixed_16_16 = fixed<int32_t, int64_t, 16>;
	using fixed_24_8  = fixed<int32_t, int64_t, 8>;
#pragma endregion

#pragma region 64-bit Base
#ifdef FPM_INT128

	using fixed_56_8  = fixed<int64_t, FPM_INT128, 8>;
	using fixed_48_16 = fixed<int64_t, FPM_INT128, 16>;
	using fixed_40_24 = fixed<int64_t, FPM_INT128, 24>;
	using fixed_32_32 = fixed<int64_t, FPM_INT128, 32>;
	using fixed_24_40 = fixed<int64_t, FPM_INT128, 40>;
	using fixed_16_48 = fixed<int64_t, FPM_INT128, 48>;
	using fixed_8_56  = fixed<int64_t, FPM_INT128, 56>;
#endif
#pragma endregion

#pragma endregion

	/// Negation. For unsigned base types the result wraps around, like negating an unsigned integer.
	/// With FPM_DEFINED_OVERFLOW it does for the lowest value of a signed type as well (which is itself).
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator-(const fixed<B, I, F, R>& x) noexcept
	{
		return fixed<B, I, F, R>::from_raw_value(detail::subtract(B{0}, x.raw_value()));
	}

#pragma region Arithmetic operators

#pragma region Addition

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator+(fixed<B, I, F, R> x, const fixed<B, I, F, R>& y) noexcept
	{
		return x += y;
	}

	template<typename B, typename I, uint32_t F, bool R, std::integral T>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator+(fixed<B, I, F, R> x, T y) noexcept
	{
		return x += y;
	}

	template<typename B, typename I, uint32_t F, bool R, std::integral T>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator+(T x, fixed<B, I, F, R> y) noexcept
	{
		return y += x;
	}

#pragma endregion

#pragma region Subtraction

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator-(fixed<B, I, F, R> x, const fixed<B, I, F, R>& y) noexcept
	{
		return x -= y;
	}

	template<typename B, typename I, uint32_t F, bool R, std::integral T>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator-(fixed<B, I, F, R> x, T y) noexcept
	{
		return x -= y;
	}

	template<typename B, typename I, uint32_t F, bool R, std::integral T>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator-(T x, const fixed<B, I, F, R>& y) noexcept
	{
		fixed<B, I, F, R> result{};
		result += x;
		return result -= y;
	}

#pragma endregion

#pragma region Multiplication

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator*(fixed<B, I, F, R> x, const fixed<B, I, F, R>& y) noexcept
	{
		return x *= y;
	}

	template<typename B, typename I, uint32_t F, bool R, std::integral T>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator*(fixed<B, I, F, R> x, T y) noexcept
	{
		return x *= y;
	}

	template<typename B, typename I, uint32_t F, bool R, std::integral T>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator*(T x, fixed<B, I, F, R> y) noexcept
	{
		return y *= x;
	}
#pragma endregion

#pragma region Division

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator/(fixed<B, I, F, R> x, const fixed<B, I, F, R>& y) noexcept
	{
		return x /= y;
	}

	template<typename B, typename I, uint32_t F, std::integral T, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator/(fixed<B, I, F, R> x, T y) noexcept
	{
		return x /= y;
	}

	template<typename B, typename I, uint32_t F, std::integral T, bool R>
	[[nodiscard]] inline constexpr fixed<B, I, F, R> operator/(T x, const fixed<B, I, F, R>& y) noexcept
	{
		fixed<B, I, F, R> result{};
		result += x;
		return result /= y;
	}

#pragma endregion

#pragma endregion

#pragma region Comparison operators

	// The other operators (`!=`, `<`, `>=`, ..., and the ones with the integer first) are derived from these

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr bool operator==(const fixed<B, I, F, R>& x, const fixed<B, I, F, R>& y) noexcept
	{
		return x.raw_value() == y.raw_value();
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] inline constexpr std::strong_ordering operator<=>(const fixed<B, I, F, R>& x, const fixed<B, I, F, R>& y) noexcept
	{
		return x.raw_value() <=> y.raw_value();
	}

	/// Comparisons with integers are exact, also for integers that the fixed-point type cannot represent
	template<typename B, typename I, uint32_t F, std::integral T, bool R>
	[[nodiscard]] inline constexpr bool operator==(const fixed<B, I, F, R>& x, const T y) noexcept
	{
		return detail::equals_integer<F>(x.raw_value(), y);
	}

	template<typename B, typename I, uint32_t F, std::integral T, bool R>
	[[nodiscard]] inline constexpr std::strong_ordering operator<=>(const fixed<B, I, F, R>& x, const T y) noexcept
	{
		return detail::compare_with_integer<F>(x.raw_value(), y);
	}

#pragma endregion

	namespace detail
	{
		/// Number of base-10 digits required to fully represent a number of bits.
		[[nodiscard]] inline constexpr int max_digits10(const int bits) noexcept
		{
			// 8.24 fixed-point equivalent of (int)ceil(bits * std::log10(2));
			using T = int64_t;
			return static_cast<int>((T{bits} * 5050445 + (T{1} << 24) - 1) >> 24);
		}

		/// Number of base-10 digits that can be fully represented by a number of bits.
		[[nodiscard]] inline constexpr int digits10(const int bits) noexcept
		{
			// 8.24 fixed-point equivalent of (int)(bits * std::log10(2));
			using T = int64_t;
			return static_cast<int>((T{bits} * 5050445) >> 24);
		}

	} // namespace detail
} // namespace fpm

// Specializations for customization points
namespace std
{
	// `has_denorm` and `has_denorm_loss` are deprecated since C++23, but remain members of std::numeric_limits
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

	template<typename B, typename I, uint32_t F, bool R>
	struct hash<fpm::fixed<B,I,F,R>>
	{
		using argument_type = fpm::fixed<B, I, F, R>;
		using result_type = std::size_t;

		[[nodiscard]] result_type operator()(argument_type arg) const noexcept(noexcept(std::declval<std::hash<B>>()(arg.raw_value())))
		{
			return std::hash<B>{}(arg.raw_value());
		}
	};

#pragma region numeric_limits

	template<typename B, typename I, uint32_t F, bool R>
	struct numeric_limits<fpm::fixed<B,I,F,R>>
	{
		static constexpr bool is_specialized = true;
		static constexpr bool is_signed = std::numeric_limits<B>::is_signed;
		static constexpr bool is_integer = false;
		static constexpr bool is_exact = true;
		static constexpr bool has_infinity = false;
		static constexpr bool has_quiet_NaN = false;
		static constexpr bool has_signaling_NaN = false;
		static constexpr std::float_denorm_style has_denorm = std::denorm_absent;
		static constexpr bool has_denorm_loss = false;
		static constexpr std::float_round_style round_style = R ? std::round_to_nearest : std::round_toward_zero;
		static constexpr bool is_iec559 = false;
		static constexpr bool is_bounded = true;

		/// With FPM_DEFINED_OVERFLOW the results of the arithmetic operators wrap around for signed types as well
		static constexpr bool is_modulo = fpm::detail::defined_overflow || std::numeric_limits<B>::is_modulo;
		static constexpr int digits = std::numeric_limits<B>::digits;

		// Any number with `digits10` significant base-10 digits is convertible from text and back without change.
		// That is none: the precision is absolute, so a small number has fewer significant digits than a large one.
		// 0.00001 is 0.00002 with 16 fraction bits, for instance. (The decimal places that are kept are -min_exponent10.)
		static constexpr int digits10 = 0;

		// This is equal to max_digits10 for the integer and fractional part together.
		static constexpr int max_digits10 = fpm::detail::max_digits10(std::numeric_limits<B>::digits - F) + fpm::detail::max_digits10(F);

		static constexpr int radix = 2;
		static constexpr int min_exponent = 1 - F;
		static constexpr int min_exponent10 = -fpm::detail::digits10(F);
		static constexpr int max_exponent = std::numeric_limits<B>::digits - F;
		static constexpr int max_exponent10 = fpm::detail::digits10(std::numeric_limits<B>::digits - F);
		static constexpr bool traps = true;
		static constexpr bool tinyness_before = false;

		static constexpr fpm::fixed<B,I,F,R> lowest() noexcept
		{
			return fpm::fixed<B,I,F,R>::from_raw_value(std::numeric_limits<B>::lowest());
		};

		/// The smallest positive value, like for floating-point types (not the lowest value, like for integers)
		static constexpr fpm::fixed<B,I,F,R> min() noexcept
		{
			return fpm::fixed<B,I,F,R>::from_raw_value(1);
		}

		static constexpr fpm::fixed<B,I,F,R> max() noexcept
		{
			return fpm::fixed<B,I,F,R>::from_raw_value(std::numeric_limits<B>::max());
		};

		static constexpr fpm::fixed<B,I,F,R> epsilon() noexcept
		{
			return fpm::fixed<B,I,F,R>::from_raw_value(1);
		};

		static constexpr fpm::fixed<B,I,F,R> round_error() noexcept
		{
			// 0.5
			return fpm::fixed<B,I,F,R>::from_raw_value(static_cast<B>(B{1} << (F - 1)));
		};

		static constexpr fpm::fixed<B,I,F,R> denorm_min() noexcept
		{
			return min();
		}
	};

#pragma endregion

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
}

#pragma region Type testing
namespace fpm
{
	template<typename T>
	struct is_fixed : std::false_type {};

	template<typename BaseType, typename IntermediateType, uint32_t FractionBits, bool EnableRounding>
	struct is_fixed<fixed<BaseType, IntermediateType, FractionBits, EnableRounding>> : std::true_type {};

	template<typename T>
	inline constexpr bool is_fixed_v = is_fixed<T>::value;

	template<typename T>
	concept Fixed = is_fixed_v<T>;
}
#pragma endregion

