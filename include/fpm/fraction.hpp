#pragma once

#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

#include "fixed.hpp"

// Define FPM_FRACTION_STRICT to check the numbers that are converted to a fraction from a floating-point number
// or from text: a number that is not in [0, 1) is an error then, instead of the fraction of it.
// (For all of a program: the option changes what the functions of this library do.)

namespace fpm
{
	//! A fraction in [0, 1): an unsigned number where all bits of the base type are fraction bits.
	//! It wraps around (modulo 1) instead of overflowing, like an angle stored as a part of a full turn.
	//!
	//! This type only stores such values, with the operations that are meaningful modulo 1.
	//! For any other calculation, convert it to a `fpm::fixed` type (which can represent 1).
	//!
	//! Conversions of a number to a fraction are modulo 1 as well: they take the fraction of the number (-0.25 and 1.75
	//! give 0.75), rounded to the nearest fraction. So a number that rounds up to 1 gives 0, and the error is
	//! half a unit at most.
	//! \tparam BaseType the unsigned integer type used to store the fraction
	template<std::unsigned_integral BaseType>
	struct fraction
	{
		using base_type = BaseType;
		static constexpr uint32_t fraction_bits = static_cast<uint32_t>(std::numeric_limits<BaseType>::digits);

	private:
		/// Unsigned type for calculations with the base type and T: small types are promoted to `int`,
		/// whose overflow is undefined
		template<typename T>
		using unsigned_with = std::make_unsigned_t<std::common_type_t<BaseType, T, unsigned int>>;

		struct raw_construct_tag{};
		inline constexpr fraction(const BaseType val, raw_construct_tag) noexcept : m_value(val) {}

	public:
		inline constexpr fraction() noexcept = default;

		/// The fraction of a (finite) floating-point number: val - floor(val), rounded to the nearest fraction.
		/// With FPM_FRACTION_STRICT the number must be in [0, 1).
		template<std::floating_point T>
		inline constexpr explicit fraction(const T val) noexcept
			: m_value(floating_to_raw(val))
		{}

		/// The fraction of a fixed-point number: val - floor(val), so -0.25 gives 0.75.
		/// Fraction bits that don't fit are rounded to the nearest fraction if the fixed-point type uses rounding,
		/// and dropped otherwise.
		template<typename B, typename I, uint32_t F, bool R>
		inline constexpr explicit fraction(const fixed<B, I, F, R> val) noexcept
			: m_value(fixed_to_raw<F, R>(val.raw_value()))
		{}

		/// Converts a fraction with another number of bits: exactly to more bits, and rounded to the nearest fraction
		/// to fewer bits
		template<std::unsigned_integral B>
			requires(!std::same_as<B, BaseType>)
		inline constexpr explicit fraction(const fraction<B> val) noexcept
			: m_value(fraction_to_raw(val.raw_value()))
		{}

		/// Explicit conversion to a floating-point type.
		/// The result is 1 if the fraction is closer to 1 than the floating-point type can represent.
		template<std::floating_point T>
		[[nodiscard]] inline constexpr explicit operator T() const noexcept
		{
			return static_cast<T>(m_value) / (half_scale<T>() * T{2});
		}

		/// Explicit conversion to a fixed-point type. Fraction bits that don't fit are rounded to nearest
		/// if the fixed-point type uses rounding (up to 1, which every fixed-point type can represent),
		/// and dropped otherwise.
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr explicit operator fixed<B, I, F, R>() const noexcept
		{
			if constexpr(F >= fraction_bits)
			{
				// The base type of the fixed-point type is wider, as it has an integral bit as well
				using U = std::make_unsigned_t<B>;
				return fixed<B, I, F, R>::from_raw_value(static_cast<B>(static_cast<U>(static_cast<U>(m_value) << (F - fraction_bits))));
			}
			else
			{
				// At most 2^F, which fits both the base type of the fraction and the one of the fixed-point type
				constexpr uint32_t shift = fraction_bits - F;
				auto raw = static_cast<BaseType>(m_value >> shift);
				if constexpr(R)
					raw = static_cast<BaseType>(raw + ((m_value >> (shift - 1)) & 1));
				return fixed<B, I, F, R>::from_raw_value(static_cast<B>(raw));
			}
		}

		/// Returns the raw underlying value of this type: the fraction times 2^fraction_bits.
		[[nodiscard]] inline constexpr BaseType raw_value() const noexcept
		{
			return m_value;
		}

		/// Constructs a fraction from its raw underlying value: the fraction times 2^fraction_bits.
		[[nodiscard]] inline static constexpr fraction from_raw_value(const BaseType value) noexcept
		{
			return fraction(value, raw_construct_tag{});
		}

		inline constexpr fraction& operator+=(const fraction& y) noexcept
		{
			m_value = static_cast<BaseType>(static_cast<unsigned_with<BaseType>>(m_value) + y.m_value);
			return *this;
		}

		inline constexpr fraction& operator-=(const fraction& y) noexcept
		{
			m_value = static_cast<BaseType>(static_cast<unsigned_with<BaseType>>(m_value) - y.m_value);
			return *this;
		}

		/// The fraction of the product: 0.75 * 3 is 0.25, and 0.25 * -1 is 0.75
		template<std::integral T>
		inline constexpr fraction& operator*=(const T y) noexcept
		{
			using U = unsigned_with<T>;
			m_value = static_cast<BaseType>(static_cast<U>(m_value) * static_cast<U>(y));
			return *this;
		}

		/// The quotient of the value in [0, 1), truncated towards zero. The quotient by a negative integer
		/// wraps around: 0.5 / -2 is -0.25, which is 0.75.
		template<std::integral T>
		inline constexpr fraction& operator/=(const T y) noexcept
		{
			assert(y != 0);
			m_value = detail::divide_by_integer(m_value, y);
			return *this;
		}

		/// Compares the values in [0, 1)
		[[nodiscard]] friend inline constexpr bool operator==(const fraction& x, const fraction& y) noexcept
		{
			return x.m_value == y.m_value;
		}

		[[nodiscard]] friend inline constexpr std::strong_ordering operator<=>(const fraction& x, const fraction& y) noexcept
		{
			return x.m_value <=> y.m_value;
		}

	private:
		/// 2^(fraction_bits - 1): half the scale of the raw value, which fits in the base type
		template<std::floating_point T>
		[[nodiscard]] inline static constexpr T half_scale() noexcept
		{
			return static_cast<T>(static_cast<BaseType>(BaseType{1} << (fraction_bits - 1)));
		}

		/// 2^exponent
		template<std::floating_point T>
		[[nodiscard]] inline static constexpr T power_of_two(const int32_t exponent) noexcept
		{
			T result{1};
			for(int32_t i = 0; i < exponent; ++i)
				result *= T{2};
			return result;
		}

		/// val - floor(val) in [0, 1]: 1 for a negative number that is too small to make a difference
		template<std::floating_point T>
		[[nodiscard]] inline static constexpr T fraction_of(T val) noexcept
		{
			// From here on every number is an integer (as the difference between consecutive numbers is 1 at least)
			constexpr int32_t digits = std::numeric_limits<T>::digits;
			constexpr T integers = power_of_two<T>(digits - 1);
			if(!(val > -integers && val < integers)) [[unlikely]]
				return T{0};

			if constexpr(digits > 64)
			{
				// Too large to convert to an integer: without the multiples of 2^62 first, which is exact
				constexpr T chunk = power_of_two<T>(62);
				if(val >= chunk || val <= -chunk) [[unlikely]]
					val -= static_cast<T>(static_cast<int64_t>(val / chunk)) * chunk;
			}
			const T result = val - static_cast<T>(static_cast<int64_t>(val)); // in (-1, 1)
			return result < T{0} ? result + T{1} : result;
		}

		template<std::floating_point T>
		[[nodiscard]] inline static constexpr BaseType floating_to_raw(const T val) noexcept
		{
#ifdef FPM_FRACTION_STRICT
			assert(val >= T{0} && val < T{1});
#endif
			// The fraction times 2^fraction_bits rounded to nearest, in two steps: neither the scale nor a result
			// that rounds up to it fits in the base type. The sum wraps around for 1.
			using U = unsigned_with<BaseType>;
			const T scaled = fraction_of(val) * half_scale<T>();
			const auto high = static_cast<BaseType>(scaled); // truncated
			const auto low = static_cast<BaseType>((scaled - static_cast<T>(high)) * T{2} + T{0.5}); // 0, 1 or 2
			return static_cast<BaseType>(static_cast<U>(high) * 2 + low);
		}

		template<uint32_t F, bool R, std::integral B>
		[[nodiscard]] inline static constexpr BaseType fixed_to_raw(const B raw) noexcept
		{
			// In an unsigned type that holds both types. The conversion to it and its arithmetic are modular,
			// so this keeps the bits of the fraction: also for negative values (two's complement).
			using U = unsigned_with<B>;
			if constexpr(F <= fraction_bits)
			{
				return static_cast<BaseType>(static_cast<U>(static_cast<U>(raw) << (fraction_bits - F)));
			}
			else
			{
				constexpr uint32_t shift = F - fraction_bits;
				// The shift of the raw value itself rounds towards negative infinity for negative values.
				// The sum wraps around for a fraction that rounds up to 1.
				if constexpr(R)
					return static_cast<BaseType>(static_cast<U>(static_cast<U>(raw >> (shift - 1)) + 1) >> 1);
				else
					return static_cast<BaseType>(static_cast<U>(raw >> shift));
			}
		}

		template<std::unsigned_integral B>
		[[nodiscard]] inline static constexpr BaseType fraction_to_raw(const B raw) noexcept
		{
			constexpr uint32_t bits = static_cast<uint32_t>(std::numeric_limits<B>::digits);
			if constexpr(bits < fraction_bits)
			{
				return static_cast<BaseType>(static_cast<BaseType>(raw) << (fraction_bits - bits));
			}
			else
			{
				// Rounded to nearest: the sum wraps around for a fraction that rounds up to 1
				using U = unsigned_with<B>;
				constexpr uint32_t shift = bits - fraction_bits;
				return static_cast<BaseType>(static_cast<B>(static_cast<U>(raw) + (U{1} << (shift - 1))) >> shift);
			}
		}

		BaseType m_value;
	};

	/// -x modulo 1: the fraction 1 - x, or 0 for 0
	template<typename B>
	[[nodiscard]] inline constexpr fraction<B> operator-(const fraction<B>& x) noexcept
	{
		return fraction<B>{} -= x;
	}

	template<typename B>
	[[nodiscard]] inline constexpr fraction<B> operator+(fraction<B> x, const fraction<B>& y) noexcept
	{
		return x += y;
	}

	template<typename B>
	[[nodiscard]] inline constexpr fraction<B> operator-(fraction<B> x, const fraction<B>& y) noexcept
	{
		return x -= y;
	}

	template<typename B, std::integral T>
	[[nodiscard]] inline constexpr fraction<B> operator*(fraction<B> x, const T y) noexcept
	{
		return x *= y;
	}

	template<typename B, std::integral T>
	[[nodiscard]] inline constexpr fraction<B> operator*(const T x, fraction<B> y) noexcept
	{
		return y *= x;
	}

	template<typename B, std::integral T>
	[[nodiscard]] inline constexpr fraction<B> operator/(fraction<B> x, const T y) noexcept
	{
		return x /= y;
	}

#pragma region Difference

	/// The direction of a rotation
	enum class direction
	{
		CounterClockwise = 0, ///< the angle increases
		Clockwise = 1,        ///< the angle decreases
	};

	/// A rotation: a distance of at most half a turn, in a direction
	template<std::unsigned_integral BaseType>
	struct rotation
	{
		fraction<BaseType> distance;
		fpm::direction direction;

		/// The rotation as a number in [-1/2, 1/2]: positive for counterclockwise, negative for clockwise
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] inline constexpr explicit operator fixed<B, I, F, R>() const noexcept
		{
			const auto value = static_cast<fixed<B, I, F, R>>(distance);
			return (direction == fpm::direction::Clockwise) ? -value : value;
		}

		[[nodiscard]] friend inline constexpr bool operator==(const rotation&, const rotation&) noexcept = default;
	};

	/// The shortest rotation from y to x: the difference x - y of the angles, as a distance of at most half a turn
	/// and its direction. For angles that are half a turn apart the direction is counterclockwise.
	template<typename B>
	[[nodiscard]] inline constexpr rotation<B> difference(const fraction<B> x, const fraction<B> y) noexcept
	{
		constexpr auto half = static_cast<B>(B{1} << (std::numeric_limits<B>::digits - 1));
		const fraction<B> forward = x - y;
		if(forward.raw_value() <= half)
			return {forward, direction::CounterClockwise};
		return {-forward, direction::Clockwise};
	}

#pragma endregion

#pragma region Type testing

	template<typename T>
	struct is_fraction : std::false_type {};

	template<typename BaseType>
	struct is_fraction<fraction<BaseType>> : std::true_type {};

	template<typename T>
	inline constexpr bool is_fraction_v = is_fraction<T>::value;

	template<typename T>
	concept Fraction = is_fraction_v<T>;

#pragma endregion
}

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

	template<typename B>
	struct hash<fpm::fraction<B>>
	{
		[[nodiscard]] std::size_t operator()(const fpm::fraction<B> arg) const noexcept(noexcept(std::declval<std::hash<B>>()(arg.raw_value())))
		{
			return std::hash<B>{}(arg.raw_value());
		}
	};

	template<typename B>
	struct numeric_limits<fpm::fraction<B>>
	{
		static constexpr bool is_specialized = true;
		static constexpr bool is_signed = false;
		static constexpr bool is_integer = false;
		static constexpr bool is_exact = true;
		static constexpr bool has_infinity = false;
		static constexpr bool has_quiet_NaN = false;
		static constexpr bool has_signaling_NaN = false;
		static constexpr std::float_denorm_style has_denorm = std::denorm_absent;
		static constexpr bool has_denorm_loss = false;
		static constexpr std::float_round_style round_style = std::round_to_nearest;
		static constexpr bool is_iec559 = false;
		static constexpr bool is_bounded = true;

		/// A fraction wraps around
		static constexpr bool is_modulo = true;
		static constexpr int digits = std::numeric_limits<B>::digits;

		// The decimal places that a fraction keeps: any number in [0, 1) with `digits10` digits after the decimal point
		// is convertible from text and back without change. (Not the significant digits, like for floating-point types:
		// a fraction has an absolute precision, so small numbers have few of those.)
		static constexpr int digits10 = fpm::detail::digits10(std::numeric_limits<B>::digits);
		static constexpr int max_digits10 = fpm::detail::max_digits10(std::numeric_limits<B>::digits);

		static constexpr int radix = 2;
		static constexpr int min_exponent = 1 - std::numeric_limits<B>::digits;
		static constexpr int min_exponent10 = -fpm::detail::digits10(std::numeric_limits<B>::digits);
		static constexpr int max_exponent = 0;
		static constexpr int max_exponent10 = 0;
		static constexpr bool traps = true;
		static constexpr bool tinyness_before = false;

		static constexpr fpm::fraction<B> lowest() noexcept
		{
			return fpm::fraction<B>::from_raw_value(0);
		}

		/// The smallest positive value, like for floating-point types (not the lowest value, like for integers)
		static constexpr fpm::fraction<B> min() noexcept
		{
			return fpm::fraction<B>::from_raw_value(1);
		}

		static constexpr fpm::fraction<B> max() noexcept
		{
			return fpm::fraction<B>::from_raw_value(std::numeric_limits<B>::max());
		}

		/// The difference between two consecutive fractions
		static constexpr fpm::fraction<B> epsilon() noexcept
		{
			return fpm::fraction<B>::from_raw_value(1);
		}

		static constexpr fpm::fraction<B> round_error() noexcept
		{
			// 0.5
			return fpm::fraction<B>::from_raw_value(static_cast<B>(B{1} << (std::numeric_limits<B>::digits - 1)));
		}

		static constexpr fpm::fraction<B> denorm_min() noexcept
		{
			return min();
		}
	};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
}
