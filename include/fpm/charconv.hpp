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
		/// Largest number of significant digits kept while parsing.
		/// Enough for 20 integral digits (2^64) and 65 fractional digits (2^-64 needs 64, the rounding digit one more).
		inline constexpr int max_digits = 96;

		/// A decimal number: 0.d[0]d[1]...d[count-1] * 10^exponent.
		/// Normalised: `d[0] != 0` and no trailing zeros. `count == 0` represents zero.
		struct decimal
		{
			std::array<std::uint8_t, max_digits> digits{};
			int count = 0;
			long long exponent = 0;
			bool negative = false;
			/// Parsing only: there were non-zero digits beyond `digits`.
			bool sticky = false;

			constexpr void trim() noexcept
			{
				while(count > 0 && digits[count - 1] == 0)
					--count;
				if(count == 0 && !sticky)
					exponent = 0;
			}
		};

		template<typename B>
		concept supported_base = std::is_integral_v<B> && sizeof(B) <= sizeof(std::uint64_t);

		template<typename B>
		[[nodiscard]] constexpr std::uint64_t magnitude(const B raw) noexcept
		{
			if constexpr(std::is_signed_v<B>)
			{
				// Well-defined for the minimum value as well
				return raw < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(raw) : static_cast<std::uint64_t>(raw);
			}
			else
			{
				return static_cast<std::uint64_t>(raw);
			}
		}

		/// Largest magnitude a fixed-point number with base type `B` can hold for the given sign.
		template<typename B>
		[[nodiscard]] constexpr std::uint64_t max_magnitude(const bool negative) noexcept
		{
			if constexpr(std::is_signed_v<B>)
				return magnitude<B>(negative ? std::numeric_limits<B>::min() : std::numeric_limits<B>::max());
			else
				return negative ? 0 : static_cast<std::uint64_t>(std::numeric_limits<B>::max());
		}

		/// Exact decimal expansion of `magnitude / 2^F`, or its first digits: generation stops after `max_significant`
		/// significant digits or at fractional position `max_fraction_position` (1-based), and `sticky` tells whether
		/// non-zero digits follow. That is enough to round to one digit less.
		template<std::uint32_t F>
		[[nodiscard]] constexpr decimal to_decimal(
			const std::uint64_t magnitude,
			const long long max_significant = std::numeric_limits<long long>::max(),
			const long long max_fraction_position = std::numeric_limits<long long>::max()
		) noexcept
		{
			static_assert(F >= 1 && F <= 64);
			decimal d;

			// Integral part
			std::uint64_t integral = (F == 64) ? 0 : (magnitude >> (F % 64));
			std::array<std::uint8_t, 20> reversed{};
			int n = 0;
			while(integral != 0)
			{
				reversed[n++] = static_cast<std::uint8_t>(integral % 10);
				integral /= 10;
			}
			while(n > 0)
				d.digits[d.count++] = reversed[--n];
			d.exponent = d.count;

			// Fractional part, scaled so that fraction = frac / 2^64.
			// Every multiplication by ten shifts one more zero bit in at the bottom, so this terminates after at most F digits.
			std::uint64_t frac = magnitude << ((64 - F) % 64);
			if constexpr(F < 64)
				frac = (magnitude & ((std::uint64_t{1} << F) - 1)) << (64 - F);
			for(long long position = 1; frac != 0; ++position)
			{
				if(d.count >= max_significant || position > max_fraction_position)
				{
					d.sticky = true; // frac != 0: non-zero digits follow
					break;
				}
				// Upper 64 bits of the 68-bit product frac * 10, computed without a wider type
				const std::uint64_t hi = frac >> 32;
				const std::uint64_t lo = frac & 0xFFFF'FFFFu;
				const auto digit = static_cast<std::uint8_t>((hi * 10 + ((lo * 10) >> 32)) >> 32);
				frac *= 10; // lower 64 bits
				if(d.count == 0 && digit == 0)
					--d.exponent; // leading zero of a value below one
				else
					d.digits[d.count++] = digit;
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
		constexpr void round_to(decimal& d, const long long keep, const rounding mode) noexcept
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
					const auto next = keep < d.count ? d.digits[keep] : std::uint8_t{0}; // (trimmed zeros)
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
				d.count = static_cast<int>(keep < 0 ? 0 : keep);
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

			auto i = static_cast<int>(keep) - 1;
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
		[[nodiscard]] constexpr std::uint8_t double_fraction(std::array<std::uint8_t, N>& f, const int len) noexcept
		{
			std::uint8_t carry = 0;
			for(int i = len - 1; i >= 0; --i)
			{
				const auto v = static_cast<std::uint8_t>(f[i] * 2 + carry);
				f[i] = v % 10;
				carry = v / 10;
			}
			return carry;
		}

		struct conversion
		{
			std::uint64_t magnitude = 0;
			bool out_of_range = false;
		};

		/// Round the magnitude `integral + fraction` to the fixed-point grid.
		/// `half` is the bit right below the last representable bit, `rest` whether anything below it is non-zero.
		template<typename B, bool R>
		[[nodiscard]] constexpr conversion finish(
			const std::uint64_t mag,
			const bool half,
			const bool rest,
			const bool negative
		) noexcept
		{
			const auto limit = max_magnitude<B>(negative);
			if(mag > limit)
				return {0, true};
			if(R && half && (rest || (mag & 1) != 0))
			{
				if(mag == limit)
					return {0, true};
				return {mag + 1, false};
			}
			return {mag, false};
		}

		inline constexpr std::array<std::uint64_t, 20> powers_of_10 = []
		{
			std::array<std::uint64_t, 20> p{};
			p[0] = 1;
			for(std::size_t i = 1; i < p.size(); ++i)
				p[i] = p[i - 1] * 10;
			return p;
		}();

		/// Exact conversion of a decimal to a fixed-point magnitude, rounded according to the type.
		template<typename B, std::uint32_t F, bool R>
		[[nodiscard]] constexpr conversion from_decimal(const decimal& d) noexcept
		{
			if(d.count == 0)
				return {0, false};

			const auto limit = max_magnitude<B>(d.negative);

			// Integral part
			std::uint64_t integral = 0;
			for(long long i = 0; i < d.exponent; ++i)
			{
				const std::uint64_t digit = i < d.count ? d.digits[i] : 0;
				if(integral > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
					return {0, true};
				integral = integral * 10 + digit;
			}
			if constexpr(F == 64)
			{
				if(integral != 0)
					return {0, true};
			}
			else if(integral > (limit >> F))
			{
				return {0, true};
			}
			std::uint64_t mag = (F == 64) ? 0 : (integral << (F % 64));

			// Fractional part. Grid points need at most F fractional digits and the midpoints between
			// them F+1, so the first F+1 digits plus a "non-zero digits follow" flag decide the rounding exactly.
			constexpr int frac_digits = static_cast<int>(F) + 1;
			std::array<std::uint8_t, frac_digits> frac{};
			int len = 0;
			bool rest = d.sticky;
			for(int i = 0; i < d.count; ++i)
			{
				const long long position = i - d.exponent; // 0-based fractional position
				if(position < 0)
					continue;
				if(position >= frac_digits)
				{
					rest = true; // non-zero, as there are no trailing zeros
					break;
				}
				frac[position] = d.digits[i];
				len = static_cast<int>(position) + 1;
			}

			// Fast path: with fraction = D / 10^len for the integer D of the `len` digits, the bits are
			// D * 2^F / 10^len, exactly rounded using the remainder of a single integer division.
			if(!rest && len <= 19)
			{
				std::uint64_t digits = 0;
				for(int i = 0; i < len; ++i)
					digits = digits * 10 + frac[i];
				const std::uint64_t pow10 = powers_of_10[len];

				bool fast = false;
				std::uint64_t quotient = 0;
				std::uint64_t remainder = 0;
				if(F < 64 && digits <= (std::numeric_limits<std::uint64_t>::max() >> (F % 64)))
				{
					const std::uint64_t numerator = digits << (F % 64);
					quotient = numerator / pow10;
					remainder = numerator % pow10;
					fast = true;
				}
#ifdef FPM_INT128
				else if constexpr(F < 64)
				{
					const auto numerator = static_cast<FPM_INT128>(digits) << F; // < 2^(64+63)
					quotient = static_cast<std::uint64_t>(numerator / static_cast<FPM_INT128>(pow10));
					remainder = static_cast<std::uint64_t>(numerator % static_cast<FPM_INT128>(pow10));
					fast = true;
				}
#endif
				if(fast)
				{
					// remainder / 10^len in [0, 1): compare with one half
					const bool half = remainder >= pow10 - remainder;
					const bool below_half = half ? (remainder != pow10 - remainder) : (remainder != 0);
					if(quotient > limit - mag)
						return {0, true};
					return finish<B, R>(mag + quotient, half, below_half, d.negative);
				}
			}

			// Binary digits of the fraction. Digits beyond `len` are zero and stay zero when doubling.
			std::uint64_t bits = 0;
			for(std::uint32_t i = 0; i < F; ++i)
				bits = (bits << 1) | double_fraction(frac, len);
			const bool half = double_fraction(frac, len) != 0;
			for(int i = 0; i < len && !rest; ++i)
				rest = frac[i] != 0;

			if(bits > limit - mag)
				return {0, true};
			return finish<B, R>(mag + bits, half, rest, d.negative);
		}

		/// Adds a digit of the significand; leading zeros are dropped, and the exponent tracks the position of the point.
		/// `scale` is the exponent change per digit: 1 for decimal, 4 for hexadecimal (binary exponent).
		constexpr void add_digit(decimal& d, const int value, const bool after_point, const int scale) noexcept
		{
			if(d.count == 0 && value == 0)
			{
				if(after_point)
					d.exponent -= scale;
				return;
			}
			if(d.count < max_digits)
				d.digits[d.count++] = static_cast<std::uint8_t>(value);
			else if(value != 0)
				d.sticky = true;
			if(!after_point)
				d.exponent += scale;
		}

		/// Exact conversion of hexadecimal digits (value = 0.h[0]h[1]... * 2^exponent) to a fixed-point magnitude
		template<typename B, std::uint32_t F, bool R>
		[[nodiscard]] constexpr conversion from_hex_digits(const decimal& d) noexcept
		{
			// Every bit has a known position relative to the fixed-point grid
			const auto limit = max_magnitude<B>(d.negative);
			std::uint64_t mag = 0;
			bool half = false;
			bool rest = d.sticky;
			for(int i = 0; i < d.count; ++i)
			{
				for(int b = 0; b < 4; ++b)
				{
					if(((d.digits[i] >> b) & 1) == 0)
						continue;
					const long long position = d.exponent - 4 * (i + 1) + b + static_cast<long long>(F);
					if(position >= 64)
						return {0, true};
					if(position >= 0)
					{
						const auto bit = std::uint64_t{1} << position;
						if(bit > limit - mag)
							return {0, true};
						mag += bit;
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

		/// The raw value of a converted magnitude
		template<typename B>
		[[nodiscard]] constexpr B to_raw(const std::uint64_t magnitude, const bool negative) noexcept
		{
			return static_cast<B>(negative ? std::uint64_t{0} - magnitude : magnitude);
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

		[[nodiscard]] constexpr int digit_value(const char c, const int base) noexcept
		{
			int v = base;
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
		[[nodiscard]] constexpr const char* parse_exponent(const char* p, const char* last, long long& exponent) noexcept
		{
			bool negative = false;
			if(p != last && (*p == '+' || *p == '-'))
			{
				negative = *p == '-';
				++p;
			}
			if(p == last || digit_value(*p, 10) < 0)
				return nullptr;

			constexpr long long saturation = 1'000'000'000;
			exponent = 0;
			for(; p != last && digit_value(*p, 10) >= 0; ++p)
			{
				if(exponent < saturation)
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

			constexpr void put(const char c, long long count) noexcept
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

		constexpr void write_exponent(writer& w, long long exponent, const int min_digits) noexcept
		{
			w.put(exponent < 0 ? '-' : '+');
			auto e = static_cast<unsigned long long>(exponent < 0 ? -exponent : exponent);
			std::array<char, 24> reversed{};
			int n = 0;
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
		constexpr void write_fixed(writer& w, const decimal& d, const long long precision) noexcept
		{
			if(d.negative)
				w.put('-');
			if(d.count == 0 || d.exponent <= 0)
			{
				w.put('0');
			}
			else
			{
				for(long long i = 0; i < d.exponent && !w.overflow; ++i)
					w.put(i < d.count ? static_cast<char>('0' + d.digits[i]) : '0');
			}
			if(precision > 0)
			{
				w.put('.');
				for(long long i = 0; i < precision && !w.overflow; ++i)
				{
					const long long index = d.exponent + i;
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
		constexpr void write_scientific(writer& w, const decimal& d, const long long precision) noexcept
		{
			if(d.negative)
				w.put('-');
			w.put(d.count == 0 ? '0' : static_cast<char>('0' + d.digits[0]));
			if(precision > 0)
			{
				w.put('.');
				for(long long i = 1; i <= precision && !w.overflow; ++i)
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
		[[nodiscard]] constexpr long long fixed_fraction_digits(const decimal& d) noexcept
		{
			return d.count == 0 ? 0 : std::max<long long>(0, d.count - d.exponent);
		}

		/// "%a" (without "0x") of `magnitude / 2^F`; `precision < 0` means exact.
		template<std::uint32_t F>
		constexpr void write_hex(writer& w, const bool negative, const std::uint64_t magnitude, const long long precision) noexcept
		{
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
			const int top = std::bit_width(magnitude) - 1;
			const auto nibble = [&](const long long index) -> unsigned // 1-based hex digit after the point
			{
				const long long shift = top - 4 * index;
				if(shift >= 0)
					return static_cast<unsigned>((magnitude >> shift) & 0xF);
				if(shift > -4)
					return static_cast<unsigned>((magnitude << -shift) & 0xF);
				return 0;
			};

			long long exact_digits = (top + 3) / 4;
			while(exact_digits > 0 && nibble(exact_digits) == 0)
				--exact_digits;

			unsigned leading = 1;
			std::uint64_t kept = 0; // the kept digits, when rounding
			const bool rounds = precision >= 0 && precision < exact_digits;
			if(rounds)
			{
				// Here 4 * precision < top, so all shifts are in range
				const int shift = top - 4 * static_cast<int>(precision);
				const std::uint64_t below = magnitude & ((std::uint64_t{1} << shift) - 1);
				const std::uint64_t half = std::uint64_t{1} << (shift - 1);
				kept = magnitude >> shift; // includes the leading one
				if(below > half || (below == half && (kept & 1) != 0))
					++kept;
				leading = static_cast<unsigned>(kept >> (4 * precision)); // 1, or 2 after a carry
			}

			w.put(hex_digits[leading]);
			const long long digits = precision < 0 ? exact_digits : precision;
			if(digits > 0)
			{
				w.put('.');
				for(long long i = 1; i <= digits && !w.overflow; ++i)
				{
					if(rounds)
						w.put(hex_digits[(kept >> (4 * (digits - i))) & 0xF]);
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
			write_exponent(w, top - static_cast<long long>(F), 1);
		}

		/// Does parsing `d` give back `magnitude`?
		template<typename B, std::uint32_t F, bool R>
		[[nodiscard]] constexpr bool round_trips(const decimal& d, const std::uint64_t magnitude) noexcept
		{
			const auto c = from_decimal<B, F, R>(d);
			return !c.out_of_range && c.magnitude == magnitude;
		}

		/// The decimal with the fewest significant digits that parses back to the same value.
		template<typename B, std::uint32_t F, bool R>
		[[nodiscard]] constexpr decimal shortest(const B raw) noexcept
		{
			const auto mag = magnitude(raw);
			auto d = to_decimal<F>(mag);
			d.negative = raw < 0;
			if(d.count == 0)
				return d;

			// Parsing rounds to nearest with rounding enabled and truncates without it,
			// so candidates must be rounded to nearest or away from zero, respectively.
			constexpr auto mode = R ? rounding::nearest_even : rounding::away_from_zero;

			// Round-tripping is monotonic in the number of digits (more digits are never further away),
			// and the exact expansion always round-trips: binary search the smallest count.
			int lo = 1;
			int hi = d.count;
			while(lo < hi)
			{
				const int mid = lo + (hi - lo) / 2;
				auto candidate = d;
				round_to(candidate, mid, mode);
				if(round_trips<B, F, R>(candidate, mag))
					hi = mid;
				else
					lo = mid + 1;
			}
			round_to(d, lo, mode);
			return d;
		}

		[[nodiscard]] constexpr long long scientific_length(const decimal& d) noexcept
		{
			const long long e = d.count == 0 ? 0 : d.exponent - 1;
			const long long abs_e = e < 0 ? -e : e;
			const long long exponent_digits = abs_e >= 100 ? (abs_e >= 1000 ? 4 : 3) : 2;
			return (d.negative ? 1 : 0) + 1 + (d.count > 1 ? d.count : 0) + 2 + exponent_digits;
		}

		[[nodiscard]] constexpr long long fixed_length(const decimal& d) noexcept
		{
			const auto fraction = fixed_fraction_digits(d);
			return (d.negative ? 1 : 0) + (d.count == 0 || d.exponent <= 0 ? 1 : d.exponent) + (fraction > 0 ? fraction + 1 : 0);
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
		const auto d = shortest<B, F, R>(value.raw_value());
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
			write_hex<F>(w, value.raw_value() < 0, magnitude(value.raw_value()), -1);
			return w.result();
		}

		const auto d = shortest<B, F, R>(value.raw_value());
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
			const long long x = d.count == 0 ? 0 : d.exponent - 1;
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
			write_hex<F>(w, value.raw_value() < 0, magnitude(value.raw_value()), precision);
			return w.result();
		}

		// Only the digits up to and including the rounding digit are generated
		const long long p = precision < 0 ? 6 : precision;
		const auto mag = magnitude(value.raw_value());

		if(fmt == std::chars_format::fixed)
		{
			auto d = to_decimal<F>(mag, std::numeric_limits<long long>::max(), p + 1);
			d.negative = value.raw_value() < 0;
			round_to(d, d.exponent + p, rounding::nearest_even);
			write_fixed(w, d, p);
		}
		else if(fmt == std::chars_format::scientific)
		{
			auto d = to_decimal<F>(mag, p + 2);
			d.negative = value.raw_value() < 0;
			round_to(d, p + 1, rounding::nearest_even);
			write_scientific(w, d, p);
		}
		else
		{
			// "%g": the precision is the number of significant digits, and trailing zeros are removed
			const long long significant = (p == 0) ? 1 : p;
			auto d = to_decimal<F>(mag, significant + 1);
			d.negative = value.raw_value() < 0;
			round_to(d, significant, rounding::nearest_even);
			const long long x = d.count == 0 ? 0 : d.exponent - 1;
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
		decimal d;
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
		const int base = is_hex ? 16 : 10;
		const int digit_scale = is_hex ? 4 : 1; // exponent change per digit (binary for hex, decimal otherwise)

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
			const int v = digit_value(*p, base);
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
			long long exponent = 0;
			if(const char* end = parse_exponent(p + 1, last, exponent))
			{
				has_exponent = true;
				p = end;
				d.exponent += exponent;
			}
		}
		if(exponent_required && !has_exponent)
			return {first, std::errc::invalid_argument};

		d.trim();
		const conversion c = is_hex ? from_hex_digits<B, F, R>(d) : from_decimal<B, F, R>(d);
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
		// Sign, 20 integral digits, point and 64 fractional digits at most
		std::array<char, 128> buffer{};
		const auto result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
		return std::string(buffer.data(), result.ptr);
	}
}

// Overloads in `std` for backward compatibility with `std::to_chars(...)` / `std::from_chars(...)` calls.
// Prefer the `fpm::` versions (also found by argument-dependent lookup): the standard does not allow adding
// overloads to namespace `std`. These are unconstrained so that the constrained `fpm::` versions win overload
// resolution when both are visible (e.g. after `using std::to_chars;`).
namespace std
{
	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] constexpr std::to_chars_result to_chars(char* first, char* last, const fpm::fixed<B, I, F, R> value) noexcept
	{
		return fpm::to_chars(first, last, value);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] constexpr std::to_chars_result to_chars(char* first, char* last, const fpm::fixed<B, I, F, R> value, const std::chars_format fmt) noexcept
	{
		return fpm::to_chars(first, last, value, fmt);
	}

	template<typename B, typename I, uint32_t F, bool R>
	[[nodiscard]] constexpr std::to_chars_result to_chars(char* first, char* last, const fpm::fixed<B, I, F, R> value, const std::chars_format fmt, const int precision) noexcept
	{
		return fpm::to_chars(first, last, value, fmt, precision);
	}

	template<typename B, typename I, uint32_t F, bool R>
	constexpr std::from_chars_result from_chars(const char* first, const char* last, fpm::fixed<B, I, F, R>& value, const std::chars_format fmt = std::chars_format::general) noexcept
	{
		return fpm::from_chars(first, last, value, fmt);
	}
}
