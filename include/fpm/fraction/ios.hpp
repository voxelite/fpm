#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "../detail/charconv.hpp"
#include "charconv.hpp"
#include "format.hpp"
#include "fraction.hpp"

// Stream operators for fractions. This header also provides the `std::format` support of <fpm/fraction/format.hpp>,
// which can be included on its own to avoid the dependency on streams and locales.

namespace fpm
{
	/// Prints like a floating-point number: supports the float field (fixed, scientific, hexfloat and default),
	/// precision, showpoint, showpos, uppercase, width, fill, adjustfield and the locale's decimal point and grouping.
	/// The digits are exactly rounded (ties to even).
	template<typename CharT, typename Traits, typename B>
	std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, fraction<B> x)
	{
		// A formatted output function: does nothing if the stream is not ready
		const typename std::basic_ostream<CharT, Traits>::sentry sentry(os);
		if(!sentry)
			return os;

		const auto flags = os.flags();
		const auto floatfield = flags & std::ios_base::floatfield;
		const bool uppercase = (flags & std::ios_base::uppercase) != 0;
		const bool hex = floatfield == (std::ios_base::fixed | std::ios_base::scientific);
		const auto precision = static_cast<int32_t>(std::min<std::streamsize>(os.precision() < 0 ? 6 : os.precision(), 1'000'000));

		// The number in the "C" locale: in a stack buffer, unless a large precision needs more
		std::array<char, 256> small_buffer;
		std::string large_buffer;
		std::span<char> buffer(small_buffer);
		if(precision > 64)
		{
			large_buffer.resize(static_cast<std::size_t>(precision) + 160);
			buffer = std::span<char>(large_buffer);
		}
		std::to_chars_result result;
		char type;
		if(floatfield == std::ios_base::fixed)
		{
			type = 'f';
			result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), x, std::chars_format::fixed, precision);
		}
		else if(floatfield == std::ios_base::scientific)
		{
			type = 'e';
			result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), x, std::chars_format::scientific, precision);
		}
		else if(hex)
		{
			// The precision is ignored for hexfloats: always exact
			type = 'a';
			result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), x, std::chars_format::hex);
		}
		else
		{
			type = 'g';
			result = fpm::to_chars(buffer.data(), buffer.data() + buffer.size(), x, std::chars_format::general, precision);
		}
		assert(result.ec == std::errc{});
		std::string_view number(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));

		std::string alternate;
		if((flags & std::ios_base::showpoint) != 0)
		{
			alternate.assign(number);
			detail::apply_alternate_form(alternate, type, precision);
			number = alternate;
		}

		const std::locale locale = os.getloc();
		const auto& ctype = std::use_facet<std::ctype<CharT>>(locale);
		const auto& numpunct = std::use_facet<std::numpunct<CharT>>(locale);
		const std::string grouping = numpunct.grouping();
		const bool grouped = !grouping.empty() && static_cast<unsigned char>(grouping[0]) != 0
			&& static_cast<unsigned char>(grouping[0]) < static_cast<unsigned char>(CHAR_MAX);
		const CharT decimal_point = numpunct.decimal_point();
		const bool negative = !number.empty() && number.front() == '-';

		// The characters to output, and the lengths of the sign and of the sign plus "0x" at their start
		const CharT* data = nullptr;
		std::size_t data_length = 0;
		std::size_t sign_length = 0;
		std::size_t prefix_length = 0;

		std::array<CharT, 512> small_body;
		std::basic_string<CharT> large_body;
		bool plain = false;
		if constexpr(std::is_same_v<CharT, char>)
		{
			// Common case: the "C" locale's formatting, so the output of `to_chars` is used as it is
			plain = !grouped && decimal_point == '.' && !uppercase && !hex && (flags & std::ios_base::showpos) == 0
				&& ctype.widen('0') == '0' && ctype.widen('-') == '-' && ctype.widen('e') == 'e';
			if(plain) [[likely]]
			{
				data = number.data();
				data_length = number.size();
				sign_length = negative ? 1 : 0;
				prefix_length = sign_length;
			}
		}
		if(!plain)
		{
			// The output: sign, "0x" prefix and the widened digits with the locale's decimal point and grouping
			std::span<CharT> body(small_body);
			if(number.size() * 2 + 4 > small_body.size())
			{
				large_body.resize(number.size() * 2 + 4);
				body = std::span<CharT>(large_body);
			}
			std::size_t body_length = 0;

			if(negative)
			{
				body[body_length++] = ctype.widen('-');
				number.remove_prefix(1);
			}
			else if((flags & std::ios_base::showpos) != 0)
			{
				body[body_length++] = ctype.widen('+');
			}
			sign_length = body_length;
			if(hex)
			{
				body[body_length++] = ctype.widen('0');
				body[body_length++] = ctype.widen(uppercase ? 'X' : 'x');
			}
			prefix_length = body_length;

			const auto integral_digits = std::min(number.find_first_of(".ep"), number.size());
			const CharT thousands_sep = numpunct.thousands_sep();
			for(std::size_t i = 0; i < number.size(); ++i)
			{
				char c = number[i];
				if(uppercase && c >= 'a' && c <= 'z')
					c = static_cast<char>(c - 'a' + 'A');
				body[body_length++] = (c == '.') ? decimal_point : ctype.widen(c);

				// Insert a separator after this digit if a group ends here (group sizes from the right)
				if(grouped && i + 1 < integral_digits)
				{
					std::size_t remaining = integral_digits - (i + 1); // digits to the right of the separator
					std::size_t group = 0;
					while(true)
					{
						const auto size = static_cast<unsigned char>(grouping[std::min(group, grouping.size() - 1)]);
						if(size == 0 || size >= static_cast<unsigned char>(CHAR_MAX) || remaining < size)
							break; // no (further) grouping
						remaining -= size;
						if(remaining == 0)
						{
							body[body_length++] = thousands_sep;
							break;
						}
						++group;
					}
				}
			}
			data = body.data();
			data_length = body_length;
		}

		// Output with padding
		auto* const buf = os.rdbuf();
		bool ok = true;
		const auto put = [&](const CharT* data, const std::size_t size)
		{
			if(size != 0 && buf->sputn(data, static_cast<std::streamsize>(size)) != static_cast<std::streamsize>(size))
				ok = false;
		};
		const auto put_fill = [&](std::size_t count)
		{
			std::array<CharT, 32> fill;
			fill.fill(os.fill());
			for(; count > 0; count -= std::min(count, fill.size()))
				put(fill.data(), std::min(count, fill.size()));
		};

		const auto width = os.width();
		os.width(0);
		const auto padding = static_cast<std::size_t>(std::max<std::streamsize>(0, width - static_cast<std::streamsize>(data_length)));
		if(padding == 0) [[likely]]
		{
			put(data, data_length);
		}
		else
		{
			const auto adjust = flags & std::ios_base::adjustfield;
			if(adjust == std::ios_base::left)
			{
				put(data, data_length);
				put_fill(padding);
			}
			else if(adjust == std::ios_base::internal)
			{
				// Padding after the sign if there is one (like the standard library does for floating-point types),
				// otherwise after the "0x" of hexfloats
				const auto internal = (sign_length > 0) ? sign_length : prefix_length;
				put(data, internal);
				put_fill(padding);
				put(data + internal, data_length - internal);
			}
			else
			{
				put_fill(padding);
				put(data, data_length);
			}
		}
		if(!ok)
			os.setstate(std::ios_base::badbit);
		return os;
	}

	/// Reads like a floating-point number. A number that is not in [0, 1) stores the nearest value (0 or the largest
	/// fraction) and fails, like a number that is out of range for the built-in types.
	template<typename CharT, class Traits, typename B>
	std::basic_istream<CharT, Traits>& operator>>(std::basic_istream<CharT, Traits>& is, fraction<B>& x)
	{
		typename std::basic_istream<CharT, Traits>::sentry sentry(is);
		if(!sentry)
			return is;

		const auto& ctype = std::use_facet<std::ctype<CharT>>(is.getloc());
		const auto& numpunct = std::use_facet<std::numpunct<CharT>>(is.getloc());

		bool thousands_separator_allowed = false;
		const bool supports_thousands_separators = !numpunct.grouping().empty();

		const auto& is_valid_character = [](char ch)
		{
			// Note: allowing ['p', 'i', 'n', 't', 'y'] is technically in violation of the spec (we are emulating std::num_get),
			// but otherwise we cannot parse hexfloats and "infinity". This is a known issue with the spec (LWG #2381).
			return std::isxdigit(static_cast<unsigned char>(ch)) ||
				ch == 'x' || ch == 'X' || ch == 'p' || ch == 'P' ||
				ch == 'i' || ch == 'I' || ch == 'n' || ch == 'N' ||
				ch == 't' || ch == 'T' || ch == 'y' || ch == 'Y' ||
				ch == '-' || ch == '+';
		};

		const auto& peek = [&]()
		{
			for(;;) {
				auto ch = is.rdbuf()->sgetc();
				if(ch == Traits::eof())
				{
					is.setstate(std::ios::eofbit);
					return '\0';
				}
				if(Traits::to_char_type(ch) == numpunct.decimal_point())
				{
					return '.';
				}
				if(Traits::to_char_type(ch) == numpunct.thousands_sep())
				{
					if(!supports_thousands_separators || !thousands_separator_allowed)
					{
						return '\0';
					}
					// Ignore valid thousands separators
					is.rdbuf()->sbumpc();
					continue;
				}
				auto res = ctype.narrow(Traits::to_char_type(ch), 0);
				if(!is_valid_character(res))
				{
					// Invalid character: end input
					return '\0';
				}
				return res;
			}
		};

		const auto& bump = [&]()
		{
			is.rdbuf()->sbumpc();
		};

		const auto& next = [&]()
		{
			bump();
			return peek();
		};

		bool negate = false;
		auto ch = peek();
		if(ch == '-')
		{
			negate = true;
			ch = next();
		}
		else if(ch == '+')
		{
			ch = next();
		}

		const char infinity[] = "infinity";
		// Must be "inf" or "infinity"
		int32_t i = 0;
		while(i < 8 && std::tolower(static_cast<unsigned char>(ch)) == infinity[i])
		{
			++i;
			ch = next();
		}

		if(i > 0)
		{
			// Infinity cannot be represented: like a value that is out of range
			if(i == 3 || i == 8)
				x = fraction<B>::from_raw_value(negate ? B{0} : std::numeric_limits<B>::max());
			is.setstate(std::ios::failbit);
			return is;
		}

		// Collect the digits and let the exact conversion of `fpm::from_chars` convert them (no allocations)
		detail::fraction_charconv::decimal_for<B> digits;
		digits.negative = negate;

		char exponent_char = 'e';
		bool hex = false;
		bool any_digit = false;
		if(ch == '0')
		{
			ch = next();
			if(ch == 'x' || ch == 'X')
			{
				// Hexfloat
				exponent_char = 'p';
				hex = true;
				ch = next();
			}
			else
			{
				any_digit = true; // a leading zero
			}
		}
		const int32_t base = hex ? 16 : 10;
		const int32_t digit_scale = hex ? 4 : 1;

		// Parse the significand
		thousands_separator_allowed = true;
		bool seen_point = false;
		for(;; ch = next())
		{
			if(ch == '.')
			{
				if(seen_point)
				{
					// Double decimal point. Stop parsing.
					break;
				}
				seen_point = true;
				thousands_separator_allowed = false;
			}
			else
			{
				const int32_t value = detail::charconv::digit_value(ch, base);
				if(value < 0)
					break;
				detail::charconv::add_digit(digits, value, seen_point, digit_scale);
				any_digit = true;
			}
		}
		if(!any_digit)
		{
			// We need a significand
			is.setstate(std::ios::failbit);
			return is;
		}
		thousands_separator_allowed = false;

		// Parse the exponent
		if(std::tolower(static_cast<unsigned char>(ch)) == exponent_char)
		{
			ch = next();
			bool exponent_negate = false;
			if(ch == '-' || ch == '+')
			{
				exponent_negate = ch == '-';
				ch = next();
			}

			bool parsed = false;
			int32_t exponent = 0;
			while(ch >= '0' && ch <= '9')
			{
				if(exponent < detail::charconv::exponent_limit) // saturate: huge exponents give 0 or overflow either way
					exponent = exponent * 10 + (ch - '0');
				parsed = true;
				ch = next();
			}
			if(!parsed)
			{
				// If the exponent character is given, the exponent value may not be empty
				is.setstate(std::ios::failbit);
				return is;
			}
			detail::charconv::add_exponent(digits, exponent_negate ? -exponent : exponent);
		}

		// We've parsed all we need. Construct the value.
		digits.trim();
		const auto converted = hex
			? detail::fraction_charconv::from_hex_digits<B>(digits)
			: detail::fraction_charconv::from_decimal<B>(digits);
		if(converted.out_of_range)
		{
			// Out of range: like for the built-in types, the nearest value is stored and the extraction fails
			x = fraction<B>::from_raw_value(negate ? B{0} : std::numeric_limits<B>::max());
			is.setstate(std::ios::failbit);
		}
		else
		{
			x = fraction<B>::from_raw_value(converted.value);
		}
		return is;
	}

}
