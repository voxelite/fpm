#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>

#include "fixed.hpp"

// Locale-independent, exact and `constexpr` conversions between fixed-point numbers and character sequences,
// mirroring `std::to_chars`, `std::from_chars` and (C++26) `std::to_string` for floating-point types.
//
// Everything in this header uses integer arithmetic only.
//
// Differences from the floating-point versions, because fixed-point numbers have no infinity or NaN:
// - `from_chars` recognises "inf", "infinity" and "nan" (case-insensitive) like the standard does,
//   but reports `std::errc::result_out_of_range` for them.
// - Ties are rounded to even (like `std::from_chars`) when the type has `EnableRounding`;
//   otherwise the value is truncated towards zero, like the type's other conversions.

namespace fpm
{
	namespace detail::charconv
	{
		// All calculations use the types of the fixed-point number: the unsigned type of the base's width for
		// magnitudes and digits, and the intermediate type where more bits are needed. Counts, digit positions
		// and exponents are 32-bit.

		/// Exponents and digit positions saturate here: anything beyond is far out of range for every type
		inline constexpr int32_t exponent_limit = 100'000'000;

		/// Decimal digits kept for a type: all integral digits, the F fractional digits of the grid points
		/// plus the one of the midpoints between them (for rounding), and some margin.
		template<typename B, uint32_t F>
		inline constexpr std::size_t max_digits = static_cast<std::size_t>(std::numeric_limits<std::make_unsigned_t<B>>::digits10) + 2 + F + 2;

		/// A decimal number: 0.d[0]d[1]...d[count-1] * 10^exponent (for hexadecimal digits: * 2^exponent).
		/// Normalised: `d[0] != 0` and no trailing zeros. `count == 0` represents zero.
		template<std::size_t Capacity>
		struct decimal
		{
			std::array<uint8_t, Capacity> digits{};
			int32_t count = 0;
			int32_t exponent = 0;
			bool negative = false;
			/// Non-zero digits follow beyond `digits` (while parsing: more digits than the capacity;
			/// for a partial expansion: digits that were not generated).
			bool sticky = false;

			constexpr void trim() noexcept
			{
				while(count > 0 && digits[count - 1] == 0)
					--count;
				if(count == 0 && !sticky)
					exponent = 0;
			}
		};

		template<typename B, uint32_t F>
		using decimal_for = decimal<max_digits<B, F>>;

		template<typename B>
		concept supported_base = std::is_integral_v<B> && std::numeric_limits<std::make_unsigned_t<B>>::digits <= 64;

		template<typename B>
		using magnitude_t = std::make_unsigned_t<B>;

		template<typename B>
		inline constexpr int32_t bits = std::numeric_limits<magnitude_t<B>>::digits;

		template<typename B>
		[[nodiscard]] constexpr magnitude_t<B> magnitude(const B raw) noexcept
		{
			using U = magnitude_t<B>;
			if constexpr(std::is_signed_v<B>)
			{
				// Well-defined for the minimum value as well
				return raw < 0 ? static_cast<U>(U{0} - static_cast<U>(raw)) : static_cast<U>(raw);
			}
			else
			{
				return raw;
			}
		}

		/// Largest magnitude a fixed-point number with base type `B` can hold for the given sign.
		template<typename B>
		[[nodiscard]] constexpr magnitude_t<B> max_magnitude(const bool negative) noexcept
		{
			if constexpr(std::is_signed_v<B>)
				return magnitude<B>(negative ? std::numeric_limits<B>::min() : std::numeric_limits<B>::max());
			else
				return negative ? magnitude_t<B>{0} : std::numeric_limits<B>::max();
		}

		/// The raw value of a magnitude with a sign
		template<typename B>
		[[nodiscard]] constexpr B to_raw(const magnitude_t<B> magnitude, const bool negative) noexcept
		{
			using U = magnitude_t<B>;
			return static_cast<B>(negative ? static_cast<U>(U{0} - magnitude) : magnitude);
		}

		/// Exact decimal expansion of `magnitude / 2^F`, or its first digits: generation stops after `max_significant`
		/// significant digits or at fractional position `max_fraction_position` (1-based), and `sticky` tells whether
		/// non-zero digits follow. That is enough to round to one digit less.
		template<typename B, uint32_t F>
		[[nodiscard]] constexpr decimal_for<B, F> to_decimal(
			const magnitude_t<B> magnitude,
			const int32_t max_significant = std::numeric_limits<int32_t>::max(),
			const int32_t max_fraction_position = std::numeric_limits<int32_t>::max()
		) noexcept
		{
			using U = magnitude_t<B>;
			constexpr int32_t N = bits<B>;
			static_assert(F >= 1 && static_cast<int32_t>(F) < N);
			decimal_for<B, F> d;

			// Integral part
			auto integral = static_cast<U>(magnitude >> F);
			std::array<uint8_t, std::numeric_limits<U>::digits10 + 1> reversed{};
			int32_t n = 0;
			while(integral != 0)
			{
				reversed[n++] = static_cast<uint8_t>(integral % 10);
				integral = static_cast<U>(integral / 10);
			}
			while(n > 0)
				d.digits[d.count++] = reversed[--n];
			d.exponent = d.count;

			// Fractional part: fraction = frac / 2^F. Each digit is the integral part of frac * 10.
			// Every multiplication by ten shifts one more zero bit in at the bottom, so this terminates after at most F digits.
			constexpr auto fraction_mask = static_cast<U>((U{1} << F) - 1);
			const auto add_digit = [&](const uint8_t digit)
			{
				if(d.count == 0 && digit == 0)
					--d.exponent; // leading zero of a value below one
				else
					d.digits[d.count++] = digit;
			};
			if constexpr(static_cast<int32_t>(F) + 4 <= N)
			{
				// frac * 10 fits in U: the digit is simply the part above F bits
				U frac = static_cast<U>(magnitude & fraction_mask);
				for(int32_t position = 1; frac != 0; ++position)
				{
					if(d.count >= max_significant || position > max_fraction_position)
					{
						d.sticky = true; // frac != 0: non-zero digits follow
						break;
					}
					frac = static_cast<U>(frac * 10u);
					add_digit(static_cast<uint8_t>(frac >> F));
					frac = static_cast<U>(frac & fraction_mask);
				}
			}
			else
			{
				// Fewer than 4 integral bits: scale so that fraction = frac / 2^N, and compute the part of frac * 10
				// above N bits in halves, so that no wider type is needed
				U frac = static_cast<U>(static_cast<U>(magnitude & fraction_mask) << (N - static_cast<int32_t>(F)));
				constexpr int32_t half = N / 2;
				constexpr U low_mask = static_cast<U>((U{1} << half) - 1);
				for(int32_t position = 1; frac != 0; ++position)
				{
					if(d.count >= max_significant || position > max_fraction_position)
					{
						d.sticky = true;
						break;
					}
					const U hi = static_cast<U>(frac >> half);
					const U lo = static_cast<U>(frac & low_mask);
					add_digit(static_cast<uint8_t>(static_cast<U>(hi * 10u + static_cast<U>(static_cast<U>(lo * 10u) >> half)) >> half));
					frac = static_cast<U>(frac * 10u); // the lower N bits
				}
			}
			d.trim();
			return d;
		}

		enum class rounding
		{
			nearest_even,     ///< round to nearest, ties to even
			away_from_zero,   ///< round up in magnitude
		};

		/// Round `d` to `keep` significant digits. If `d` is not the full expansion (`sticky`),
		/// it must have at least the digit after the rounding position (trailing zeros may have been trimmed).
		template<std::size_t Capacity>
		constexpr void round_to(decimal<Capacity>& d, const int32_t keep, const rounding mode) noexcept
		{
			if(keep >= d.count && !d.sticky)
				return;
			const bool more = d.sticky; // non-zero digits beyond the available ones
			d.sticky = false;

			bool up = true; // rounding::away_from_zero: the discarded digits are never all zero (no trailing zeros)
			if(mode == rounding::nearest_even)
			{
				if(keep < 0)
				{
					// The whole number is below half a unit of the rounding position
					up = false;
				}
				else
				{
					const auto next = keep < d.count ? d.digits[keep] : uint8_t{0}; // (trimmed zeros)
					if(next != 5)
						up = next > 5;
					else if(keep + 1 < d.count || more)
						up = true; // more than half: non-zero digits follow
					else
						up = keep > 0 && (d.digits[keep - 1] % 2) == 1; // exact tie: round to even
				}
			}

			if(!up)
			{
				d.count = keep < 0 ? 0 : keep;
				d.trim();
				return;
			}

			if(keep <= 0)
			{
				// Rounds up to a single unit at the rounding position, which lies above the first digit
				d.exponent += 1 - keep;
				d.digits[0] = 1;
				d.count = 1;
				return;
			}

			int32_t i = keep - 1;
			while(i >= 0 && d.digits[i] == 9)
				--i;
			if(i < 0)
			{
				// All nines: carry into a new leading digit
				d.digits[0] = 1;
				d.count = 1;
				++d.exponent;
			}
			else
			{
				++d.digits[i];
				d.count = i + 1;
			}
		}

		/// Halve-and-carry: multiplies the decimal fraction 0.f[0]f[1]...f[len-1] by two in place
		/// and returns the carry out of the integral position (the next binary digit).
		template<std::size_t N>
		[[nodiscard]] constexpr uint8_t double_fraction(std::array<uint8_t, N>& f, const int32_t len) noexcept
		{
			uint8_t carry = 0;
			for(int32_t i = len - 1; i >= 0; --i)
			{
				const auto v = static_cast<uint8_t>(f[i] * 2 + carry);
				f[i] = static_cast<uint8_t>(v % 10);
				carry = static_cast<uint8_t>(v / 10);
			}
			return carry;
		}

		template<typename B>
		struct conversion
		{
			magnitude_t<B> magnitude = 0;
			bool out_of_range = false;
		};

		/// Round a magnitude to the fixed-point grid.
		/// `half` is the bit right below the last representable bit, `rest` whether anything below it is non-zero.
		template<typename B, bool R>
		[[nodiscard]] constexpr conversion<B> finish(
			const magnitude_t<B> mag,
			const bool half,
			const bool rest,
			const bool negative
		) noexcept
		{
			const auto limit = max_magnitude<B>(negative);
			if(mag > limit)
				return {0, true};
			if constexpr(R)
			{
				if(half && (rest || (mag & 1) != 0))
				{
					if(mag == limit)
						return {0, true};
					return {static_cast<magnitude_t<B>>(mag + 1), false};
				}
			}
			return {mag, false};
		}

		/// Number of fractional digits D for which D * 2^F still fits in the intermediate type I:
		/// then the fraction is converted with one division (see from_decimal).
		template<typename I, uint32_t F>
		inline constexpr int32_t fast_digits = []
		{
			// The largest n with 10^n <= max >> F
			const I limit = std::numeric_limits<I>::max() >> F;
			int32_t n = 0;
			for(I p = 1; p <= limit / 10; p *= 10)
				++n;
			return n;
		}();

		/// 10^n in the intermediate type, for n <= fast_digits
		template<typename I, uint32_t F>
		inline constexpr auto powers_of_10 = []
		{
			std::array<I, fast_digits<I, F> + 1> p{};
			p[0] = 1;
			for(std::size_t i = 1; i < p.size(); ++i)
				p[i] = static_cast<I>(p[i - 1] * 10);
			return p;
		}();

		/// Exact conversion of a decimal to a fixed-point magnitude, rounded according to the type.
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] constexpr conversion<B> from_decimal(const decimal_for<B, F>& d) noexcept
		{
			using U = magnitude_t<B>;
			if(d.count == 0)
				return {0, false};

			const auto limit = max_magnitude<B>(d.negative);

			// Integral part
			U integral = 0;
			for(int32_t i = 0; i < d.exponent; ++i)
			{
				const auto digit = static_cast<U>(i < d.count ? d.digits[i] : 0);
				if(integral > static_cast<U>((std::numeric_limits<U>::max() - digit) / 10)) [[unlikely]]
					return {0, true};
				integral = static_cast<U>(integral * 10u + digit);
			}
			if(integral > static_cast<U>(limit >> F))
				return {0, true};
			const auto mag = static_cast<U>(integral << F);

			// Fractional part. Grid points need at most F fractional digits and the midpoints between
			// them F+1, so the first F+1 digits plus a "non-zero digits follow" flag decide the rounding exactly.
			constexpr int32_t frac_digits = static_cast<int32_t>(F) + 1;
			std::array<uint8_t, frac_digits> frac{};
			int32_t len = 0;
			bool rest = d.sticky;
			for(int32_t i = 0; i < d.count; ++i)
			{
				const int32_t position = i - d.exponent; // 0-based fractional position
				if(position < 0)
					continue;
				if(position >= frac_digits)
				{
					rest = true; // non-zero, as there are no trailing zeros
					break;
				}
				frac[position] = d.digits[i];
				len = position + 1;
			}

			// Fast path: with fraction = D / 10^L for the integer D of the first L digits, the bits are
			// D * 2^F / 10^L, exactly rounded using the remainder of a single division in the intermediate type.
			{
				constexpr int32_t max_length = fast_digits<I, F>;
				const int32_t length = len < max_length ? len : max_length;
				bool tail = rest; // non-zero digits after the first `length`
				for(int32_t i = length; i < len && !tail; ++i)
					tail = frac[i] != 0;

				I digits = 0;
				for(int32_t i = 0; i < length; ++i)
					digits = static_cast<I>(digits * 10 + frac[i]);
				const I pow10 = powers_of_10<I, F>[length];
				const I numerator = static_cast<I>(digits << F);
				const I quotient = numerator / pow10;
				const I remainder = numerator % pow10;

				// The fraction below the last bit is (remainder + t * 2^F) / 10^L, with t in [0, 1) the value of
				// the tail digits (t > 0 if `tail`): compare it with one half
				bool decided = true;
				bool half = remainder >= pow10 - remainder;
				bool rest_below = half ? (remainder != pow10 - remainder) : (remainder != 0);
				if(tail)
				{
					const I upper = static_cast<I>(remainder + (I{1} << F)); // exclusive bound with the tail
					if(half)
					{
						// Strictly above one half. When rounding, the result is the next grid point even if the tail
						// carries into the next bit (the fraction then stays below 1.5, as 2^F < 10^L / 2).
						rest_below = true;
						constexpr bool small_tail = (I{1} << (F + 1)) < powers_of_10<I, F>[max_length]; // 2^F < 10^L / 2
						if((!R || !small_tail) && upper > pow10)
							decided = false; // truncating (or a large tail): whether it carries matters
					}
					else if(upper <= pow10 - upper)
					{
						rest_below = true; // strictly below one half, and not zero
					}
					else
					{
						decided = false; // (rare) the tail may cross one half: use the exact method below
					}
				}
				if(decided) [[likely]]
				{
					const auto bits_value = static_cast<U>(quotient); // < 2^F
					if(bits_value > limit - mag)
						return {0, true};
					return finish<B, R>(static_cast<U>(mag + bits_value), half, rest_below, d.negative);
				}
			}

			// Binary digits of the fraction. Digits beyond `len` are zero and stay zero when doubling.
			U bits_value = 0;
			for(uint32_t i = 0; i < F; ++i)
				bits_value = static_cast<U>(static_cast<U>(bits_value << 1) | double_fraction(frac, len));
			const bool half = double_fraction(frac, len) != 0;
			for(int32_t i = 0; i < len && !rest; ++i)
				rest = frac[i] != 0;

			if(bits_value > limit - mag)
				return {0, true};
			return finish<B, R>(static_cast<U>(mag + bits_value), half, rest, d.negative);
		}

		/// Adds a digit of the significand; leading zeros are dropped, and the exponent tracks the position of the point.
		/// `scale` is the exponent change per digit: 1 for decimal, 4 for hexadecimal (binary exponent).
		template<std::size_t Capacity>
		constexpr void add_digit(decimal<Capacity>& d, const int32_t value, const bool after_point, const int32_t scale) noexcept
		{
			if(d.count == 0 && value == 0)
			{
				if(after_point && d.exponent > -exponent_limit)
					d.exponent -= scale;
				return;
			}
			if(static_cast<std::size_t>(d.count) < Capacity)
				d.digits[d.count++] = static_cast<uint8_t>(value);
			else if(value != 0)
				d.sticky = true;
			if(!after_point && d.exponent < exponent_limit)
				d.exponent += scale;
		}

		/// Adds a (saturated) exponent
		template<std::size_t Capacity>
		constexpr void add_exponent(decimal<Capacity>& d, const int32_t exponent) noexcept
		{
			d.exponent = std::clamp(d.exponent + exponent, -2 * exponent_limit, 2 * exponent_limit);
		}

		/// Exact conversion of hexadecimal digits (value = 0.h[0]h[1]... * 2^exponent) to a fixed-point magnitude
		template<typename B, uint32_t F, bool R>
		[[nodiscard]] constexpr conversion<B> from_hex_digits(const decimal_for<B, F>& d) noexcept
		{
			using U = magnitude_t<B>;
			// Every bit has a known position relative to the fixed-point grid
			const auto limit = max_magnitude<B>(d.negative);
			U mag = 0;
			bool half = false;
			bool rest = d.sticky;
			for(int32_t i = 0; i < d.count; ++i)
			{
				for(int32_t b = 0; b < 4; ++b)
				{
					if(((d.digits[i] >> b) & 1) == 0)
						continue;
					const int32_t position = d.exponent - 4 * (i + 1) + b + static_cast<int32_t>(F);
					if(position >= bits<B>)
						return {0, true};
					if(position >= 0)
					{
						const auto bit = static_cast<U>(U{1} << position);
						if(bit > limit - mag)
							return {0, true};
						mag = static_cast<U>(mag + bit);
					}
					else if(position == -1)
					{
						half = true;
					}
					else
					{
						rest = true;
					}
				}
			}
			return finish<B, R>(mag, half, rest, d.negative);
		}

		[[nodiscard]] constexpr char to_lower(const char c) noexcept
		{
			return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
		}

		/// Case-insensitive match of `word` at `p`. Returns the end of the match, or `nullptr`.
		[[nodiscard]] constexpr const char* match(const char* p, const char* last, const char* word) noexcept
		{
			for(; *word != '\0'; ++p, ++word)
			{
				if(p == last || to_lower(*p) != *word)
					return nullptr;
			}
			return p;
		}

		[[nodiscard]] constexpr int32_t digit_value(const char c, const int32_t base) noexcept
		{
			int32_t v = base;
			if(c >= '0' && c <= '9')
				v = c - '0';
			else if(c >= 'a' && c <= 'f')
				v = c - 'a' + 10;
			else if(c >= 'A' && c <= 'F')
				v = c - 'A' + 10;
			return v < base ? v : -1;
		}

		/// Parses an optionally signed decimal exponent ("+12", "-3", "7").
		/// Returns the end of the exponent, or `nullptr` if there are no digits. Saturates huge values.
		[[nodiscard]] constexpr const char* parse_exponent(const char* p, const char* last, int32_t& exponent) noexcept
		{
			bool negative = false;
			if(p != last && (*p == '+' || *p == '-'))
			{
				negative = *p == '-';
				++p;
			}
			if(p == last || digit_value(*p, 10) < 0)
				return nullptr;

			exponent = 0;
			for(; p != last && digit_value(*p, 10) >= 0; ++p)
			{
				if(exponent < exponent_limit)
					exponent = exponent * 10 + digit_value(*p, 10);
			}
			if(negative)
				exponent = -exponent;
			return p;
		}

		/// Writes characters into [p, last), remembering whether it ran out of space.
		struct writer
		{
			char* p;
			char* last;
			bool overflow = false;

			constexpr void put(const char c) noexcept
			{
				if(p == last)
					overflow = true;
				else
					*p++ = c;
			}

			constexpr void put(const char c, int32_t count) noexcept
			{
				for(; count > 0 && !overflow; --count)
					put(c);
			}

			[[nodiscard]] constexpr std::to_chars_result result() const noexcept
			{
				if(overflow)
					return {last, std::errc::value_too_large};
				return {p, std::errc{}};
			}
		};

		constexpr void write_exponent(writer& w, const int32_t exponent, const int32_t min_digits) noexcept
		{
			w.put(exponent < 0 ? '-' : '+');
			auto e = static_cast<uint32_t>(exponent < 0 ? -exponent : exponent);
			std::array<char, 12> reversed{};
			int32_t n = 0;
			do
			{
				reversed[n++] = static_cast<char>('0' + e % 10);
				e /= 10;
			} while(e != 0);
			while(n < min_digits)
				reversed[n++] = '0';
			while(n > 0)
				w.put(reversed[--n]);
		}

		/// "%.{precision}f" of an already rounded decimal
		template<std::size_t Capacity>
		constexpr void write_fixed(writer& w, const decimal<Capacity>& d, const int32_t precision) noexcept
		{
			if(d.negative)
				w.put('-');
			if(d.count == 0 || d.exponent <= 0)
			{
				w.put('0');
			}
			else
			{
				for(int32_t i = 0; i < d.exponent && !w.overflow; ++i)
					w.put(i < d.count ? static_cast<char>('0' + d.digits[i]) : '0');
			}
			if(precision > 0)
			{
				w.put('.');
				for(int32_t i = 0; i < precision && !w.overflow; ++i)
				{
					const int32_t index = d.exponent + i;
					if(d.count != 0 && index >= 0 && index < d.count)
					{
						w.put(static_cast<char>('0' + d.digits[index]));
					}
					else if(index >= d.count)
					{
						// Only zeros remain
						w.put('0', precision - i);
						break;
					}
					else
					{
						w.put('0');
					}
				}
			}
		}

		/// "%.{precision}e" of an already rounded decimal
		template<std::size_t Capacity>
		constexpr void write_scientific(writer& w, const decimal<Capacity>& d, const int32_t precision) noexcept
		{
			if(d.negative)
				w.put('-');
			w.put(d.count == 0 ? '0' : static_cast<char>('0' + d.digits[0]));
			if(precision > 0)
			{
				w.put('.');
				for(int32_t i = 1; i <= precision && !w.overflow; ++i)
				{
					if(i >= d.count)
					{
						w.put('0', precision - i + 1);
						break;
					}
					w.put(static_cast<char>('0' + d.digits[i]));
				}
			}
			w.put('e');
			write_exponent(w, d.count == 0 ? 0 : d.exponent - 1, 2);
		}

		/// Number of fractional digits needed to show all digits of `d` in fixed notation
		template<std::size_t Capacity>
		[[nodiscard]] constexpr int32_t fixed_fraction_digits(const decimal<Capacity>& d) noexcept
		{
			return d.count == 0 ? 0 : std::max<int32_t>(0, d.count - d.exponent);
		}

		/// "%a" (without "0x") of `magnitude / 2^F`; `precision < 0` means exact.
		template<typename B, uint32_t F>
		constexpr void write_hex(writer& w, const bool negative, const magnitude_t<B> magnitude, const int32_t precision) noexcept
		{
			using U = magnitude_t<B>;
			constexpr char hex_digits[] = "0123456789abcdef";
			if(negative)
				w.put('-');

			if(magnitude == 0)
			{
				w.put('0');
				if(precision > 0)
				{
					w.put('.');
					w.put('0', precision);
				}
				w.put('p');
				write_exponent(w, 0, 1);
				return;
			}

			// Normalise to 1.xxx: `top` is the leading bit, the `top` bits below it form the hex digits
			const int32_t top = static_cast<int32_t>(std::bit_width(magnitude)) - 1;
			const auto nibble = [&](const int32_t index) -> uint32_t // 1-based hex digit after the point
			{
				const int32_t shift = top - 4 * index;
				if(shift >= 0)
					return static_cast<uint32_t>((magnitude >> shift) & 0xFu);
				if(shift > -4)
					return static_cast<uint32_t>(static_cast<U>(magnitude << -shift) & 0xFu);
				return 0;
			};

			int32_t exact_digits = (top + 3) / 4;
			while(exact_digits > 0 && nibble(exact_digits) == 0)
				--exact_digits;

			uint32_t leading = 1;
			U kept = 0; // the kept digits, when rounding
			const bool rounds = precision >= 0 && precision < exact_digits;
			if(rounds)
			{
				// Here 4 * precision < top, so all shifts are in range
				const int32_t shift = top - 4 * precision;
				const auto below = static_cast<U>(magnitude & static_cast<U>((U{1} << shift) - 1));
				const auto half = static_cast<U>(U{1} << (shift - 1));
				kept = static_cast<U>(magnitude >> shift); // includes the leading one
				if(below > half || (below == half && (kept & 1) != 0))
					++kept;
				leading = static_cast<uint32_t>(kept >> (4 * precision)); // 1, or 2 after a carry
			}

			w.put(hex_digits[leading]);
			const int32_t digits = precision < 0 ? exact_digits : precision;
			if(digits > 0)
			{
				w.put('.');
				for(int32_t i = 1; i <= digits && !w.overflow; ++i)
				{
					if(rounds)
						w.put(hex_digits[static_cast<uint32_t>(kept >> (4 * (digits - i))) & 0xFu]);
					else if(i <= exact_digits)
						w.put(hex_digits[nibble(i)]);
					else
					{
						w.put('0', digits - i + 1);
						break;
					}
				}
			}
			w.put('p');
			write_exponent(w, top - static_cast<int32_t>(F), 1);
		}

		/// The decimal with the fewest significant digits that parses back to the same value.
		///
		/// For a value X / 2^F with a fraction, rounding to k fractional digits is off by r / (2^F * 10^k) (down) or
		/// (2^F - r) / (2^F * 10^k) (up), where r = X * 10^k mod 2^F. Parsing the candidate gives back X if it is within half
		/// a unit (2^-(F+1), ties to even X) when rounding, or at most a unit above X when truncating. So the smallest k
		/// can be found with integers of the base's width: r is updated like the fraction digits, by one multiplication.
		template<typename B, typename I, uint32_t F, bool R>
		[[nodiscard]] constexpr decimal_for<B, F> shortest(const B raw) noexcept
		{
			using U = magnitude_t<B>;
			const auto mag = magnitude(raw);
			auto d = to_decimal<B, F>(mag);
			d.negative = raw < 0;

			constexpr auto mask = static_cast<U>((U{1} << F) - 1);
			U r = static_cast<U>(mag & mask);
			if(r == 0)
				return d; // an integer: the expansion without trailing zeros is the shortest

			U power = 1;        // 10^k while it is at most 2^F - 1
			bool large = false; // 10^k >= 2^F: every candidate is within half a unit
			int32_t k = 0;
			for(;;)
			{
				++k;
				if(!large)
				{
					if(power > mask / 10)
						large = true;
					else
						power = static_cast<U>(power * 10u);
				}
				r = static_cast<U>(static_cast<U>(r * 10u) & mask); // X * 10^k mod 2^F
				if(r == 0 || large)
					break;

				const auto up = static_cast<U>(mask - r + 1); // 2^F - r (r != 0)
				if constexpr(R)
				{
					// Nearest candidate within half a unit: 2 * distance < 10^k, or equal for an even X
					const U distance = r < up ? r : up;
					if(distance < power && (distance < power - distance || (distance == power - distance && (mag & 1) == 0)))
						break;
				}
				else
				{
					// The candidate rounded away from zero must be less than a unit above X
					if(up < power)
						break;
				}
			}
			constexpr auto mode = R ? rounding::nearest_even : rounding::away_from_zero;
			round_to(d, d.exponent + k, mode);
			return d;
		}

		template<std::size_t Capacity>
		[[nodiscard]] constexpr int32_t scientific_length(const decimal<Capacity>& d) noexcept
		{
			const int32_t e = d.count == 0 ? 0 : d.exponent - 1;
			const int32_t abs_e = e < 0 ? -e : e;
			const int32_t exponent_digits = abs_e >= 100 ? (abs_e >= 1000 ? 4 : 3) : 2;
			return (d.negative ? 1 : 0) + 1 + (d.count > 1 ? d.count : 0) + 2 + exponent_digits;
		}

		template<std::size_t Capacity>
		[[nodiscard]] constexpr int32_t fixed_length(const decimal<Capacity>& d) noexcept
		{
			const auto fraction = fixed_fraction_digits(d);
			return (d.negative ? 1 : 0) + (d.count == 0 || d.exponent <= 0 ? 1 : d.exponent) + (fraction > 0 ? fraction + 1 : 0);
		}

		/// Precisions are clamped, so calculations with them cannot overflow (the output would not fit anyway)
		[[nodiscard]] constexpr int32_t clamp_precision(const int precision) noexcept
		{
			constexpr int32_t limit = 1'000'000'000;
			return precision > limit ? limit : static_cast<int32_t>(precision);
		}
	}

#pragma region to_chars

	/// Shortest representation that `fpm::from_chars` converts back to the same value, in fixed or scientific notation,
	/// whichever is shorter (fixed on ties). Like `std::to_chars(first, last, double)`.
	template<typename B, typename I, uint32_t F, bool R>
		requires detail::charconv::supported_base<B>
	[[nodiscard]] constexpr std::to_chars_result to_chars(char* first, char* last, const fixed<B, I, F, R> value) noexcept
	{
		using namespace detail::charconv;
		const auto d = shortest<B, I, F, R>(value.raw_value());
		writer w{first, last};
		if(fixed_length(d) <= scientific_length(d))
			write_fixed(w, d, fixed_fraction_digits(d));
		else
			write_scientific(w, d, d.count - 1);
		return w.result();
	}

	/// Shortest round-trip representation in the given format. Like `std::to_chars(first, last, double, fmt)`:
	/// - `fixed`: "%f" style, `scientific`: "%e" style, `hex`: "%a" style without the "0x" prefix;
	/// - `general`: fixed notation if the decimal exponent is in [-4, 6), otherwise scientific.
	template<typename B, typename I, uint32_t F, bool R>
		requires detail::charconv::supported_base<B>
	[[nodiscard]] constexpr std::to_chars_result to_chars(
		char* first,
		char* last,
		const fixed<B, I, F, R> value,
		const std::chars_format fmt
	) noexcept
	{
		using namespace detail::charconv;
		writer w{first, last};
		if(fmt == std::chars_format::hex)
		{
			write_hex<B, F>(w, value.raw_value() < 0, magnitude(value.raw_value()), -1);
			return w.result();
		}

		const auto d = shortest<B, I, F, R>(value.raw_value());
		if(fmt == std::chars_format::fixed)
		{
			write_fixed(w, d, fixed_fraction_digits(d));
		}
		else if(fmt == std::chars_format::scientific)
		{
			write_scientific(w, d, d.count == 0 ? 0 : d.count - 1);
		}
		else
		{
			const int32_t x = d.count == 0 ? 0 : d.exponent - 1;
			if(x >= -4 && x < 6)
				write_fixed(w, d, fixed_fraction_digits(d));
			else
				write_scientific(w, d, d.count - 1);
		}
		return w.result();
	}

	/// Representation with the given precision, like `printf` in the "C" locale, and exactly rounded (ties to even).
	/// Like `std::to_chars(first, last, double, fmt, precision)`:
	/// - `fixed`: "%.{precision}f", `scientific`: "%.{precision}e",
	/// - `general`: "%.{precision}g", `hex`: "%.{precision}a" without the "0x" prefix.
	/// A negative precision behaves like the default of `printf`: 6, or the exact value for `hex`.
	template<typename B, typename I, uint32_t F, bool R>
		requires detail::charconv::supported_base<B>
	[[nodiscard]] constexpr std::to_chars_result to_chars(
		char* first,
		char* last,
		const fixed<B, I, F, R> value,
		const std::chars_format fmt,
		const int precision
	) noexcept
	{
		using namespace detail::charconv;
		writer w{first, last};
		if(fmt == std::chars_format::hex)
		{
			write_hex<B, F>(w, value.raw_value() < 0, magnitude(value.raw_value()), clamp_precision(precision));
			return w.result();
		}

		// Only the digits up to and including the rounding digit are generated
		const int32_t p = precision < 0 ? 6 : clamp_precision(precision);
		const auto mag = magnitude(value.raw_value());

		if(fmt == std::chars_format::fixed)
		{
			auto d = to_decimal<B, F>(mag, std::numeric_limits<int32_t>::max(), p + 1);
			d.negative = value.raw_value() < 0;
			round_to(d, d.exponent + p, rounding::nearest_even);
			write_fixed(w, d, p);
		}
		else if(fmt == std::chars_format::scientific)
		{
			auto d = to_decimal<B, F>(mag, p + 2);
			d.negative = value.raw_value() < 0;
			round_to(d, p + 1, rounding::nearest_even);
			write_scientific(w, d, p);
		}
		else
		{
			// "%g": the precision is the number of significant digits, and trailing zeros are removed
			const int32_t significant = (p == 0) ? 1 : p;
			auto d = to_decimal<B, F>(mag, significant + 1);
			d.negative = value.raw_value() < 0;
			round_to(d, significant, rounding::nearest_even);
			const int32_t x = d.count == 0 ? 0 : d.exponent - 1;
			if(significant > x && x >= -4)
				write_fixed(w, d, fixed_fraction_digits(d));
			else
				write_scientific(w, d, d.count == 0 ? 0 : d.count - 1);
		}
		return w.result();
	}

#pragma endregion

#pragma region from_chars

	/// Parses a fixed-point number like `std::from_chars(first, last, double&, fmt)`:
	/// no leading whitespace or '+', an optional '-', and no "0x" prefix for `hex`.
	/// The exponent is forbidden for `fixed`, optional for `general` and `hex`, and required for `scientific`.
	///
	/// The result is exact, rounded to nearest (ties to even) or truncated if the type does not use rounding.
	/// Values that do not fit, as well as infinity and NaN, give `std::errc::result_out_of_range`.
	/// On any error, `value` is left unmodified.
	template<typename B, typename I, uint32_t F, bool R>
		requires detail::charconv::supported_base<B>
	constexpr std::from_chars_result from_chars(
		const char* const first,
		const char* const last,
		fixed<B, I, F, R>& value,
		const std::chars_format fmt = std::chars_format::general
	) noexcept
	{
		using namespace detail::charconv;

		const char* p = first;
		decimal_for<B, F> d;
		if(p != last && *p == '-')
		{
			d.negative = true;
			++p;
		}

		// Infinity and NaN parse, but cannot be represented
		if(const char* end = match(p, last, "inf"))
		{
			if(const char* end_long = match(end, last, "inity"))
				end = end_long;
			return {end, std::errc::result_out_of_range};
		}
		if(const char* end = match(p, last, "nan"))
		{
			if(end != last && *end == '(')
			{
				const char* q = end + 1;
				while(q != last && (digit_value(*q, 10) >= 0 || (to_lower(*q) >= 'a' && to_lower(*q) <= 'z') || *q == '_'))
					++q;
				if(q != last && *q == ')')
					end = q + 1;
			}
			return {end, std::errc::result_out_of_range};
		}

		const bool is_hex = fmt == std::chars_format::hex;
		const int32_t base = is_hex ? 16 : 10;
		const int32_t digit_scale = is_hex ? 4 : 1; // exponent change per digit (binary for hex, decimal otherwise)

		// Significand
		bool any_digit = false;
		bool seen_point = false;
		for(; p != last; ++p)
		{
			if(*p == '.' && !seen_point)
			{
				seen_point = true;
				continue;
			}
			const int32_t v = digit_value(*p, base);
			if(v < 0)
				break;
			any_digit = true;
			add_digit(d, v, seen_point, digit_scale);
		}
		if(!any_digit)
			return {first, std::errc::invalid_argument};

		// Exponent
		const bool exponent_allowed = is_hex || (fmt & std::chars_format::scientific) == std::chars_format::scientific;
		const bool exponent_required = !is_hex && (fmt & std::chars_format::fixed) != std::chars_format::fixed;
		bool has_exponent = false;
		if(exponent_allowed && p != last && to_lower(*p) == (is_hex ? 'p' : 'e'))
		{
			int32_t exponent = 0;
			if(const char* end = parse_exponent(p + 1, last, exponent))
			{
				has_exponent = true;
				p = end;
				add_exponent(d, exponent);
			}
		}
		if(exponent_required && !has_exponent)
			return {first, std::errc::invalid_argument};

		d.trim();
		const auto c = is_hex ? from_hex_digits<B, F, R>(d) : from_decimal<B, I, F, R>(d);
		if(c.out_of_range)
			return {p, std::errc::result_out_of_range};

		value = fixed<B, I, F, R>::from_raw_value(to_raw<B>(c.magnitude, d.negative));
		return {p, std::errc{}};
	}

#pragma endregion

	/// Shortest round-trip representation, like C++26 `std::to_string(double)` (which is `std::format("{}", value)`).
	template<typename B, typename I, uint32_t F, bool R>
		requires detail::charconv::supported_base<B>
	[[nodiscard]] constexpr std::string to_string(const fixed<B, I, F, R> value)
	{
		// Sign, integral digits, point and F fractional digits at most (or the scientific notation, which is shorter)
		std::array<char, std::numeric_limits<std::make_unsigned_t<B>>::digits10 + F + 8> buffer{};
		const auto result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
		return std::string(buffer.data(), result.ptr);
	}
}
