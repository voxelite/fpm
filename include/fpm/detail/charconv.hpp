#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

// Helpers for the conversions between numbers and text, shared by the types of this library.
// They only deal with digits and characters: the conversions of the values are part of each type.

namespace fpm
{
	namespace detail::charconv
	{
		// Counts, digit positions and exponents are 32-bit

		/// Exponents and digit positions saturate here: anything beyond is far out of range for every type
		inline constexpr int32_t exponent_limit = 100'000'000;

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

	namespace detail
	{
		/// The characters for a number that is converted to text: `Size` of them on the stack, which is what a type needs
		/// for all of its digits (see `text_size` of the type). More than that, for a larger precision, are allocated.
		template<std::size_t Size, typename CharT = char>
		class text_buffer
		{
		public:
			explicit text_buffer(const std::size_t needed)
			{
				if(needed > Size) [[unlikely]]
				{
					m_large = std::make_unique_for_overwrite<CharT[]>(needed);
					m_size = needed;
				}
			}

			[[nodiscard]] std::span<CharT> characters() noexcept
			{
				return {m_large ? m_large.get() : m_small.data(), m_size};
			}

		private:
			std::array<CharT, Size> m_small;
			std::unique_ptr<CharT[]> m_large;
			std::size_t m_size = Size;
		};

		/// Applies the alternate form ('#' in printf and std::format, std::showpoint for streams) to the output of
		/// `to_chars` for the given `type` ('a', 'e', 'f', 'g', or '\0' for the general format with a precision):
		/// the decimal point is always shown, and for 'g' trailing zeros are kept to show `precision` significant digits.
		/// In place, for the first `length` characters of `text` (which has room for more): returns the new length.
		[[nodiscard]] constexpr std::size_t apply_alternate_form(const std::span<char> text, std::size_t length, const char type, const int32_t precision) noexcept
		{
			const auto insert = [&](const std::size_t position, const std::size_t count, const char c)
			{
				// (The text has room for it: see `text_size` of the types)
				assert(position <= length && length + count <= text.size());
				if(length + count > text.size()) [[unlikely]]
					return;
				std::copy_backward(text.begin() + static_cast<std::ptrdiff_t>(position), text.begin() + static_cast<std::ptrdiff_t>(length), text.begin() + static_cast<std::ptrdiff_t>(length + count));
				std::fill_n(text.begin() + static_cast<std::ptrdiff_t>(position), count, c);
				length += count;
			};
			const auto find = [&](const char c)
			{
				const auto position = std::string_view(text.data(), length).find(c);
				return (position == std::string_view::npos) ? length : position;
			};

			// (The digits of the hexadecimal notation include 'e')
			if(find('.') == length)
				insert(find(type == 'a' ? 'p' : 'e'), 1, '.');

			if(type == 'g' || type == '\0')
			{
				const auto significant_wanted = static_cast<std::size_t>(precision == 0 ? 1 : (precision < 0 ? 6 : precision));
				const auto end = find('e');
				std::size_t significant = 0;
				bool leading = true;
				for(std::size_t i = 0; i < end; ++i)
				{
					const char c = text[i];
					if(c < '0' || c > '9' || (leading && c == '0'))
						continue;
					leading = false;
					++significant;
				}
				if(leading)
					significant = 1; // zero: "0" counts as one significant digit
				if(significant < significant_wanted)
					insert(end, significant_wanted - significant, '0');
			}
			return length;
		}
	}
}
