#pragma once

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

#include "../detail/charconv.hpp"
#include "../fraction.hpp"

// Locale-independent, exact and `constexpr` conversions between fractions and character sequences,
// mirroring `std::to_chars`, `std::from_chars` and (C++26) `std::to_string` for floating-point types.
//
// Everything in this header uses integer arithmetic only.
//
// Differences from the floating-point versions, because a fraction is a number in [0, 1):
// - `from_chars` gives the fraction of any number, like the other conversions to a fraction: "1.75" and "-0.25"
//   give 0.75. With FPM_FRACTION_STRICT it reports `std::errc::result_out_of_range` for the numbers that are not
//   in [0, 1) instead.
// - Numbers are rounded to the nearest fraction, ties to even (like `std::from_chars`): a number that rounds up to 1
//   gives 0.
// - "inf", "infinity" and "nan" are not numbers: they give `std::errc::result_out_of_range`.

namespace fpm
{
	namespace detail::fraction_charconv
	{
		using namespace charconv;

		// All calculations use the base type of the fraction, and a wider type (if there is one) to convert
		// decimal digits with a single division.

		template<typename B>
		inline constexpr int32_t bits = std::numeric_limits<B>::digits;

		/// Decimal digits kept for a type: the fractional digits of the grid points (as many as the type has bits)
		/// plus the one of the midpoints between them (for rounding), and some margin.
		template<typename B>
		inline constexpr std::size_t max_digits = static_cast<std::size_t>(bits<B>) + 1 + 2;

		template<typename B>
		using decimal_for = decimal<max_digits<B>>;

		/// Exact decimal expansion of `raw / 2^bits`, or its first digits: generation stops after `max_significant`
		/// significant digits or at fractional position `max_fraction_position` (1-based), and `sticky` tells whether
		/// non-zero digits follow. That is enough to round to one digit less.
		template<typename B>
		[[nodiscard]] constexpr decimal_for<B> to_decimal(
			const B raw,
			const int32_t max_significant = std::numeric_limits<int32_t>::max(),
			const int32_t max_fraction_position = std::numeric_limits<int32_t>::max()
		) noexcept
		{
			constexpr int32_t N = bits<B>;
			decimal_for<B> d;

			// Each digit is the integral part of frac * 10: the part of the product above the bits of the type,
			// calculated in halves so that no wider type is needed.
			// Every multiplication by ten shifts one more zero bit in at the bottom, so this terminates after at most N digits.
			constexpr int32_t half = N / 2;
			constexpr auto low_mask = static_cast<B>((B{1} << half) - 1);
			B frac = raw;
			for(int32_t position = 1; frac != 0; ++position)
			{
				if(d.count >= max_significant || position > max_fraction_position)
				{
					d.sticky = true; // frac != 0: non-zero digits follow
					break;
				}
				const auto hi = static_cast<B>(frac >> half);
				const auto lo = static_cast<B>(frac & low_mask);
				const auto digit = static_cast<uint8_t>(static_cast<B>(hi * 10u + static_cast<B>(static_cast<B>(lo * 10u) >> half)) >> half);
				if(d.count == 0 && digit == 0)
					--d.exponent; // leading zero
				else
					d.digits[d.count++] = digit;
				frac = static_cast<B>(frac * 10u); // the lower N bits
			}
			d.trim();
			return d;
		}

		template<typename B>
		struct conversion
		{
			B value = 0;
			bool out_of_range = false;
		};

		/// The number of a text, after its sign: the digits with at most one decimal point, and the exponent
		/// (decimal, or binary for hexadecimal digits)
		struct number
		{
			const char* first;
			const char* last;
			int32_t exponent = 0;
			bool hexadecimal = false;
			bool negative = false;
		};

		/// Round a value to the grid of the fraction: to nearest, ties to even.
		/// `half` is the bit right below the last bit, `rest` whether anything below it is non-zero.
		/// Then the fraction of a negative number: 1 - value, modulo 1.
		/// `not_zero`: the number (not only its fraction) is not zero.
		template<typename B>
		[[nodiscard]] constexpr conversion<B> finish(B value, const bool half, const bool rest, const bool negative, const bool integral, const bool not_zero) noexcept
		{
#ifdef FPM_FRACTION_STRICT
			// Not in [0, 1)
			if(integral || (negative && not_zero))
				return {0, true};
#else
			static_cast<void>(integral);
			static_cast<void>(not_zero);
#endif
			// The sum wraps around for a fraction that rounds up to 1. (With unsigned arithmetic: the small types
			// are promoted to `int`.)
			using U = std::make_unsigned_t<std::common_type_t<B, unsigned int>>;
			if(half && (rest || (value & 1) != 0))
				value = static_cast<B>(static_cast<U>(value) + 1u);
			if(negative)
				value = static_cast<B>(U{0} - static_cast<U>(value));
			return {value, false};
		}

		/// The wider type to convert decimal digits with a single division, if there is one: `void` otherwise
#ifdef FPM_INT128
		template<typename B>
		using wide_t = std::conditional_t<(bits<B> <= 32), uint64_t, FPM_INT128>;
#else
		template<typename B>
		using wide_t = std::conditional_t<(bits<B> <= 32), uint64_t, void>;
#endif

		/// Number of fractional digits D for which D * 2^N still fits in the wider type W
		template<typename W, int32_t N>
		inline constexpr int32_t fast_digits = []
		{
			// The largest n with 10^n <= max >> N
			const W limit = std::numeric_limits<W>::max() >> N;
			int32_t n = 0;
			for(W p = 1; p <= limit / 10; p *= 10)
				++n;
			return n;
		}();

		/// 10^n in the wider type, for n <= fast_digits
		template<typename W, int32_t N>
		inline constexpr auto powers_of_10 = []
		{
			std::array<W, fast_digits<W, N> + 1> p{};
			p[0] = 1;
			for(std::size_t i = 1; i < p.size(); ++i)
				p[i] = static_cast<W>(p[i - 1] * 10);
			return p;
		}();

		/// Exact conversion of the fraction of a decimal number, rounded to nearest (ties to even)
		template<typename B>
		[[nodiscard]] constexpr conversion<B> from_decimal(const number& text) noexcept
		{
			constexpr int32_t N = bits<B>;

			// The digits before the decimal point
			int64_t before = 0;
			bool seen_point = false;
			for(const char* p = text.first; p != text.last; ++p)
			{
				if(*p == '.')
					seen_point = true;
				else if(!seen_point)
					++before;
			}

			// The digits of the fraction: the ones after the decimal point of the number, with its exponent.
			// Grid points need at most N fractional digits and the midpoints between them N+1,
			// so the first N+1 digits plus a "non-zero digits follow" flag decide the rounding exactly.
			constexpr int32_t frac_digits = N + 1;
			std::array<uint8_t, frac_digits> frac{};
			int32_t len = 0;
			bool rest = false;
			bool integral = false; // the number is 1 at least
			bool not_zero = false;
			int64_t position = -(before + text.exponent); // 0 for the first digit after the decimal point
			for(const char* p = text.first; p != text.last; ++p)
			{
				if(*p == '.')
					continue;
				const auto digit = static_cast<uint8_t>(*p - '0');
				if(digit != 0)
				{
					not_zero = true;
					if(position < 0)
						integral = true;
					else if(position >= frac_digits)
						rest = true;
				}
				if(position >= 0 && position < frac_digits)
				{
					frac[static_cast<std::size_t>(position)] = digit;
					if(digit != 0)
						len = static_cast<int32_t>(position) + 1;
				}
				++position;
			}

			// Fast path: with fraction = D / 10^L for the integer D of the first L digits, the bits are
			// D * 2^N / 10^L, exactly rounded using the remainder of a single division in the wider type.
			using W = wide_t<B>;
			if constexpr(!std::is_void_v<W>)
			{
				constexpr int32_t max_length = fast_digits<W, N>;
				const int32_t length = len < max_length ? len : max_length;
				bool tail = rest; // non-zero digits after the first `length`
				for(int32_t i = length; i < len && !tail; ++i)
					tail = frac[static_cast<std::size_t>(i)] != 0;

				W digits = 0;
				for(int32_t i = 0; i < length; ++i)
					digits = static_cast<W>(digits * 10 + frac[static_cast<std::size_t>(i)]);
				const W pow10 = powers_of_10<W, N>[static_cast<std::size_t>(length)];
				const W numerator = static_cast<W>(digits << N);
				const W quotient = numerator / pow10;
				const W remainder = numerator % pow10;

				// The fraction below the last bit is (remainder + t * 2^N) / 10^L, with t in [0, 1) the value of
				// the tail digits (t > 0 if `tail`): compare it with one half
				bool decided = true;
				const bool half = remainder >= pow10 - remainder;
				bool rest_below = half ? (remainder != pow10 - remainder) : (remainder != 0);
				if(tail)
				{
					const W upper = static_cast<W>(remainder + (W{1} << N)); // exclusive bound with the tail
					if(half)
					{
						// Strictly above one half. The result is the next grid point even if the tail carries
						// into the next bit, if the fraction then stays below 1.5: 2^N < 10^L / 2.
						rest_below = true;
						constexpr bool small_tail = (W{1} << (N + 1)) < powers_of_10<W, N>[static_cast<std::size_t>(max_length)];
						if(!small_tail && upper > pow10)
							decided = false; // a large tail: whether it carries matters
					}
					else if(upper <= pow10 && upper <= pow10 - upper)
					{
						// Strictly below one half, and not zero. (The bound can exceed 10^L: no negative difference,
						// which would be a large value in an unsigned type.)
						rest_below = true;
					}
					else
					{
						decided = false; // (rare) the tail may cross one half: use the exact method below
					}
				}
				if(decided) [[likely]]
					return finish<B>(static_cast<B>(quotient), half, rest_below, text.negative, integral, not_zero); // the quotient is below 2^N
			}

			// Binary digits of the fraction. Digits beyond `len` are zero and stay zero when doubling.
			B value = 0;
			for(int32_t i = 0; i < N; ++i)
				value = static_cast<B>(static_cast<B>(value << 1) | double_fraction(frac, len));
			const bool half = double_fraction(frac, len) != 0;
			for(int32_t i = 0; i < len && !rest; ++i)
				rest = frac[static_cast<std::size_t>(i)] != 0;
			return finish<B>(value, half, rest, text.negative, integral, not_zero);
		}

		/// Exact conversion of the fraction of a number with hexadecimal digits, rounded to nearest (ties to even)
		template<typename B>
		[[nodiscard]] constexpr conversion<B> from_hex_digits(const number& text) noexcept
		{
			constexpr int32_t N = bits<B>;

			// The digits before the point
			int64_t before = 0;
			bool seen_point = false;
			for(const char* p = text.first; p != text.last; ++p)
			{
				if(*p == '.')
					seen_point = true;
				else if(!seen_point)
					++before;
			}

			// Every bit has a known position relative to the grid of the fraction
			B value = 0;
			bool half = false;
			bool rest = false;
			bool integral = false;
			bool not_zero = false;
			int64_t weight = 4 * before + text.exponent; // the first digit has the bits 2^(weight - 4) to 2^(weight - 1)
			for(const char* p = text.first; p != text.last; ++p)
			{
				if(*p == '.')
					continue;
				weight -= 4;
				const int32_t digit = digit_value(*p, 16);
				if(digit == 0)
					continue;
				not_zero = true;
				for(int32_t b = 0; b < 4; ++b)
				{
					if(((digit >> b) & 1) == 0)
						continue;
					const int64_t position = weight + b + N; // in the raw value
					if(position >= N)
						integral = true;
					else if(position >= 0)
						value = static_cast<B>(value | static_cast<B>(B{1} << position));
					else if(position == -1)
						half = true;
					else
						rest = true;
				}
			}
			return finish<B>(value, half, rest, text.negative, integral, not_zero);
		}

		/// "%a" (without "0x") of `raw / 2^bits`; `precision < 0` means exact.
		template<typename B>
		constexpr void write_hex(writer& w, const B raw, const int32_t precision) noexcept
		{
			constexpr int32_t N = bits<B>;
			constexpr char hex_digits[] = "0123456789abcdef";

			if(raw == 0)
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
			const int32_t top = static_cast<int32_t>(std::bit_width(raw)) - 1;
			const auto nibble = [&](const int32_t index) -> uint32_t // 1-based hex digit after the point
			{
				const int32_t shift = top - 4 * index;
				if(shift >= 0)
					return static_cast<uint32_t>((raw >> shift) & 0xFu);
				if(shift > -4)
					return static_cast<uint32_t>(static_cast<B>(raw << -shift) & 0xFu);
				return 0;
			};

			int32_t exact_digits = (top + 3) / 4;
			while(exact_digits > 0 && nibble(exact_digits) == 0)
				--exact_digits;

			uint32_t leading = 1;
			B kept = 0; // the kept digits, when rounding
			const bool rounds = precision >= 0 && precision < exact_digits;
			if(rounds)
			{
				// Here 4 * precision < top, so all shifts are in range
				const int32_t shift = top - 4 * precision;
				const auto below = static_cast<B>(raw & static_cast<B>((B{1} << shift) - 1));
				const auto half = static_cast<B>(B{1} << (shift - 1));
				kept = static_cast<B>(raw >> shift); // includes the leading one
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
			write_exponent(w, top - N, 1);
		}

		/// The decimal with the fewest significant digits that parses back to the same value.
		///
		/// For a value X / 2^N, rounding to k fractional digits is off by r / (2^N * 10^k) (down) or
		/// (2^N - r) / (2^N * 10^k) (up), where r = X * 10^k mod 2^N. Parsing the candidate gives back X if it is
		/// within half a unit (2^-(N+1), ties to even X). So the smallest k can be found with integers of the base's
		/// width: r is updated like the fraction digits, by one multiplication.
		template<typename B>
		[[nodiscard]] constexpr decimal_for<B> shortest(const B raw) noexcept
		{
			auto d = to_decimal<B>(raw);
			if(raw == 0)
				return d;

			constexpr B mask = std::numeric_limits<B>::max(); // 2^N - 1
			B r = raw;
			B power = 1;        // 10^k while it is at most 2^N - 1
			bool large = false; // 10^k >= 2^N: every candidate is within half a unit
			int32_t k = 0;
			for(;;)
			{
				++k;
				if(!large)
				{
					if(power > mask / 10)
						large = true;
					else
						power = static_cast<B>(power * 10u);
				}
				r = static_cast<B>(r * 10u); // X * 10^k mod 2^N
				if(r == 0 || large)
					break;

				// Nearest candidate within half a unit: 2 * distance < 10^k, or equal for an even X
				const auto up = static_cast<B>(mask - r + 1); // 2^N - r (r != 0)
				const B distance = r < up ? r : up;
				if(distance < power && (distance < power - distance || (distance == power - distance && (raw & 1) == 0)))
					break;
			}
			round_to(d, d.exponent + k, rounding::nearest_even);
			return d;
		}
	}

#pragma region to_chars

	/// Shortest representation that `fpm::from_chars` converts back to the same value, in fixed or scientific notation,
	/// whichever is shorter (fixed on ties). Like `std::to_chars(first, last, double)`.
	template<typename B>
	[[nodiscard]] constexpr std::to_chars_result to_chars(char* first, char* last, const fraction<B> value) noexcept
	{
		using namespace detail::charconv;
		using namespace detail::fraction_charconv;
		const auto d = shortest<B>(value.raw_value());
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
	template<typename B>
	[[nodiscard]] constexpr std::to_chars_result to_chars(
		char* first,
		char* last,
		const fraction<B> value,
		const std::chars_format fmt
	) noexcept
	{
		using namespace detail::charconv;
		using namespace detail::fraction_charconv;
		writer w{first, last};
		if(fmt == std::chars_format::hex)
		{
			write_hex<B>(w, value.raw_value(), -1);
			return w.result();
		}

		const auto d = shortest<B>(value.raw_value());
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
	/// The digits are rounded, so a fraction close to 1 can give "1" (which `from_chars` reads as 0).
	template<typename B>
	[[nodiscard]] constexpr std::to_chars_result to_chars(
		char* first,
		char* last,
		const fraction<B> value,
		const std::chars_format fmt,
		const int precision
	) noexcept
	{
		using namespace detail::charconv;
		using namespace detail::fraction_charconv;
		writer w{first, last};
		if(fmt == std::chars_format::hex)
		{
			write_hex<B>(w, value.raw_value(), clamp_precision(precision));
			return w.result();
		}

		// Only the digits up to and including the rounding digit are generated
		const int32_t p = precision < 0 ? 6 : clamp_precision(precision);
		const B raw = value.raw_value();

		if(fmt == std::chars_format::fixed)
		{
			auto d = to_decimal<B>(raw, std::numeric_limits<int32_t>::max(), p + 1);
			round_to(d, d.exponent + p, rounding::nearest_even);
			write_fixed(w, d, p);
		}
		else if(fmt == std::chars_format::scientific)
		{
			auto d = to_decimal<B>(raw, p + 2);
			round_to(d, p + 1, rounding::nearest_even);
			write_scientific(w, d, p);
		}
		else
		{
			// "%g": the precision is the number of significant digits, and trailing zeros are removed
			const int32_t significant = (p == 0) ? 1 : p;
			auto d = to_decimal<B>(raw, significant + 1);
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

	/// Parses a fraction like `std::from_chars(first, last, double&, fmt)`:
	/// no leading whitespace or '+', an optional '-', and no "0x" prefix for `hex`.
	/// The exponent is forbidden for `fixed`, optional for `general` and `hex`, and required for `scientific`.
	///
	/// The result is the fraction of the number (modulo 1: "1.75" and "-0.25" give 0.75), exactly rounded to the
	/// nearest fraction (ties to even). With FPM_FRACTION_STRICT, numbers that are not in [0, 1) give
	/// `std::errc::result_out_of_range` instead. So do infinity and NaN, always.
	/// On any error, `value` is left unmodified.
	template<typename B>
	constexpr std::from_chars_result from_chars(
		const char* const first,
		const char* const last,
		fraction<B>& value,
		const std::chars_format fmt = std::chars_format::general
	) noexcept
	{
		using namespace detail::charconv;
		using namespace detail::fraction_charconv;

		const char* p = first;
		number text{};
		if(p != last && *p == '-')
		{
			text.negative = true;
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

		text.hexadecimal = fmt == std::chars_format::hex;
		const int32_t base = text.hexadecimal ? 16 : 10;

		// Significand
		text.first = p;
		bool any_digit = false;
		bool seen_point = false;
		for(; p != last; ++p)
		{
			if(*p == '.' && !seen_point)
			{
				seen_point = true;
				continue;
			}
			if(digit_value(*p, base) < 0)
				break;
			any_digit = true;
		}
		text.last = p;
		if(!any_digit)
			return {first, std::errc::invalid_argument};

		// Exponent
		const bool exponent_allowed = text.hexadecimal || (fmt & std::chars_format::scientific) == std::chars_format::scientific;
		const bool exponent_required = !text.hexadecimal && (fmt & std::chars_format::fixed) != std::chars_format::fixed;
		bool has_exponent = false;
		if(exponent_allowed && p != last && to_lower(*p) == (text.hexadecimal ? 'p' : 'e'))
		{
			if(const char* end = parse_exponent(p + 1, last, text.exponent))
			{
				has_exponent = true;
				p = end;
			}
			else
			{
				text.exponent = 0;
			}
		}
		if(exponent_required && !has_exponent)
			return {first, std::errc::invalid_argument};

		const auto c = text.hexadecimal ? from_hex_digits<B>(text) : from_decimal<B>(text);
		if(c.out_of_range)
			return {p, std::errc::result_out_of_range};

		value = fraction<B>::from_raw_value(c.value);
		return {p, std::errc{}};
	}

#pragma endregion

	/// Shortest round-trip representation, like C++26 `std::to_string(double)` (which is `std::format("{}", value)`).
	template<typename B>
	[[nodiscard]] constexpr std::string to_string(const fraction<B> value)
	{
		// "0.", and as many fractional digits as the type has bits at most (or the scientific notation, which is shorter)
		std::array<char, std::numeric_limits<B>::digits + 8> buffer{};
		const auto result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
		return std::string(buffer.data(), result.ptr);
	}
}
