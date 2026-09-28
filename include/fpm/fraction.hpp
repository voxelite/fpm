#pragma once

#include <cassert>
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "fixed.hpp"

namespace fpm
{
	//! A fraction in [0, 1): an unsigned number where all bits of the base type are fraction bits.
	//! It wraps around (modulo 1) instead of overflowing, like an angle stored as a part of a full turn.
	//!
	//! This type only stores such values, with the operations that are meaningful modulo 1.
	//! For any other calculation, convert it to a `fpm::fixed` type (which can represent 1).
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

		/// Converts a floating-point number in [0, 1), rounded to nearest.
		/// A number that rounds up to 1 wraps around to 0.
		template<std::floating_point T>
		inline constexpr explicit fraction(const T val) noexcept
			: m_value(floating_to_raw(val))
		{}

		/// The fraction of a fixed-point number: val - floor(val), so -0.25 gives 0.75.
		/// Fraction bits that don't fit are rounded to nearest if the fixed-point type uses rounding (a number
		/// that rounds up to 1 wraps around to 0), and dropped otherwise.
		template<typename B, typename I, uint32_t F, bool R>
		inline constexpr explicit fraction(const fixed<B, I, F, R> val) noexcept
			: m_value(fixed_to_raw<F, R>(val.raw_value()))
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

		template<std::floating_point T>
		[[nodiscard]] inline static constexpr BaseType floating_to_raw(const T val) noexcept
		{
			assert(val >= T{0} && val < T{1});
			// val * 2^fraction_bits rounded to nearest, in two steps: neither the scale nor a result
			// that rounds up to it fits in the base type
			using U = unsigned_with<BaseType>;
			const T scaled = val * half_scale<T>();
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
				// The shift of the raw value itself rounds towards negative infinity for negative values
				if constexpr(R)
					return static_cast<BaseType>(static_cast<U>(static_cast<U>(raw >> (shift - 1)) + 1) >> 1);
				else
					return static_cast<BaseType>(static_cast<U>(raw >> shift));
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
}
