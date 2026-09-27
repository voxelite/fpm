#pragma once

#include <array>
#include <algorithm>
#include <cctype>
#include <climits>
#include <limits>
#include <ios>
#include <version>
#include <format>
#include <sstream>
#include <span>
#include <string_view>
#include <iomanip>

#include "charconv.hpp"
#include "fixed.hpp"
#include "math.hpp"

namespace fpm
{
	namespace detail
	{
		/// Applies the alternate form ('#' in printf and std::format, std::showpoint for streams) to the output of
		/// `to_chars` for the given `type` ('a', 'e', 'f', 'g', or '\0' for the general format with a precision):
		/// the decimal point is always shown, and for 'g' trailing zeros are kept to show `precision` significant digits.
		inline void apply_alternate_form(std::string& text, const char type, const int precision)
		{
			const auto exponent_pos = text.find_first_of(type == 'a' ? "p" : "e");
			const auto mantissa_end = (exponent_pos == std::string::npos) ? text.size() : exponent_pos;
			if(text.find('.') == std::string::npos)
				text.insert(mantissa_end, 1, '.');

			if(type == 'g' || type == '\0')
			{
				const auto significant_wanted = static_cast<std::size_t>(precision == 0 ? 1 : (precision < 0 ? 6 : precision));
				const auto end_pos = text.find_first_of("e");
				const auto end = (end_pos == std::string::npos) ? text.size() : end_pos;
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
					text.insert(end, significant_wanted - significant, '0');
			}
		}
	}

	/// Prints like a floating-point number: supports the float field (fixed, scientific, hexfloat and default),
	/// precision, showpoint, showpos, uppercase, width, fill, adjustfield and the locale's decimal point and grouping.
	/// The digits are exactly rounded (ties to even).
	template<typename CharT, typename Traits, typename B, typename I, uint32_t F, bool R>
	std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, fixed<B, I, F, R> x)
	{
		// A formatted output function: does nothing if the stream is not ready
		const typename std::basic_ostream<CharT, Traits>::sentry sentry(os);
		if(!sentry)
			return os;

		const auto flags = os.flags();
		const auto floatfield = flags & std::ios_base::floatfield;
		const bool uppercase = (flags & std::ios_base::uppercase) != 0;
		const bool hex = floatfield == (std::ios_base::fixed | std::ios_base::scientific);
		const auto precision = static_cast<int>(std::min<std::streamsize>(os.precision() < 0 ? 6 : os.precision(), 1'000'000));

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

		// The output: sign, "0x" prefix and the widened digits with the locale's decimal point and grouping
		std::array<CharT, 512> small_body;
		std::basic_string<CharT> large_body;
		std::span<CharT> body(small_body);
		if(number.size() * 2 + 4 > small_body.size())
		{
			large_body.resize(number.size() * 2 + 4);
			body = std::span<CharT>(large_body);
		}
		std::size_t body_length = 0;

		if(!number.empty() && number.front() == '-')
		{
			body[body_length++] = ctype.widen('-');
			number.remove_prefix(1);
		}
		else if((flags & std::ios_base::showpos) != 0)
		{
			body[body_length++] = ctype.widen('+');
		}
		const auto sign_length = body_length;
		if(hex)
		{
			body[body_length++] = ctype.widen('0');
			body[body_length++] = ctype.widen(uppercase ? 'X' : 'x');
		}
		const auto prefix_length = body_length;

		const auto integral_digits = std::min(number.find_first_of(".ep"), number.size());
		const std::string grouping = numpunct.grouping();
		const CharT thousands_sep = numpunct.thousands_sep();
		const CharT decimal_point = numpunct.decimal_point();
		const bool grouped = !grouping.empty() && static_cast<unsigned char>(grouping[0]) != 0
			&& static_cast<unsigned char>(grouping[0]) < static_cast<unsigned char>(CHAR_MAX);
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
		const auto padding = static_cast<std::size_t>(std::max<std::streamsize>(0, width - static_cast<std::streamsize>(body_length)));
		if(padding == 0)
		{
			put(body.data(), body_length);
		}
		else
		{
			const auto adjust = flags & std::ios_base::adjustfield;
			if(adjust == std::ios_base::left)
			{
				put(body.data(), body_length);
				put_fill(padding);
			}
			else if(adjust == std::ios_base::internal)
			{
				// Padding after the sign if there is one (like the standard library does for floating-point types),
				// otherwise after the "0x" of hexfloats
				const auto internal = (sign_length > 0) ? sign_length : prefix_length;
				put(body.data(), internal);
				put_fill(padding);
				put(body.data() + internal, body_length - internal);
			}
			else
			{
				put_fill(padding);
				put(body.data(), body_length);
			}
		}
		if(!ok)
			os.setstate(std::ios_base::badbit);
		return os;
	}

	template<typename CharT, class Traits, typename B, typename I, uint32_t F, bool R>
	std::basic_istream<CharT, Traits>& operator>>(std::basic_istream<CharT, Traits>& is, fixed<B, I, F, R>& x)
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
		int i = 0;
		while(i < 8 && std::tolower(static_cast<unsigned char>(ch)) == infinity[i])
		{
			++i;
			ch = next();
		}

		if(i > 0)
		{
			if(i == 3 || i == 8)
			{
				x = negate ? std::numeric_limits<fixed<B, I, F, R>>::min() : std::numeric_limits<fixed<B, I, F, R>>::max();
			}
			else
			{
				is.setstate(std::ios::failbit);
			}
			return is;
		}

		// Collect the digits and let the exact conversion of `fpm::from_chars` convert them (no allocations)
		detail::charconv::decimal digits;
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
		const int base = hex ? 16 : 10;
		const int digit_scale = hex ? 4 : 1;

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
				const int value = detail::charconv::digit_value(ch, base);
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
			long long exponent = 0;
			while(ch >= '0' && ch <= '9')
			{
				if(exponent < 1'000'000'000) // saturate: huge exponents give 0 or overflow either way
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
			digits.exponent += exponent_negate ? -exponent : exponent;
		}

		// We've parsed all we need. Construct the value.
		digits.trim();
		const auto converted = hex
			? detail::charconv::from_hex_digits<B, F, R>(digits)
			: detail::charconv::from_decimal<B, F, R>(digits);
		if(converted.out_of_range)
		{
			// Too large: saturate
			x = negate ? std::numeric_limits<fixed<B, I, F, R>>::lowest() : std::numeric_limits<fixed<B, I, F, R>>::max();
		}
		else
		{
			x = fixed<B, I, F, R>::from_raw_value(detail::charconv::to_raw<B>(converted.magnitude, negate));
		}
		return is;
	}

}

namespace std
{
	/// `std::format` support, like for floating-point types: fill and align, sign, '#', '0', width and precision
	/// (also as nested arguments, e.g. "{:{}.{}f}"), and the types 'a', 'A', 'e', 'E', 'f', 'F', 'g' and 'G'.
	/// Locale-specific formatting ('L') is not supported.
	///
	/// Like for floating-point types, the output is locale-independent and based on `to_chars`:
	/// without type and precision it is the shortest representation that reads back to the same value.
	template<typename CharT, typename B, typename I, uint32_t F, bool R>
	struct formatter<fpm::fixed<B, I, F, R>, CharT>
	{
		static_assert(
			// `std::format` is implemented only for `char` and `wchar_t`
			std::is_same_v<CharT, char> || std::is_same_v<CharT, wchar_t>,
			"Formatter is implemented only for `char` and `wchar_t`"
		);

	private:
		enum class Alignment : char
		{
			Default = '\0', ///< right-aligned for numbers
			Left = '<',
			Right = '>',
			Center = '^', ///< padding on both sides, more on the right
		};

		enum class Sign : char
		{
			Minus = '-', ///< only show the sign of negative numbers
			Plus = '+',  ///< always show the sign
			Space = ' ', ///< space for non-negative numbers
		};

		/// Width or precision: a value, or a (nested) argument index
		struct Spec
		{
			std::size_t value = 0;
			std::size_t arg_id = 0;
			bool is_set = false;
			bool is_arg = false;
		};

		/// The fill character: a single code point, which may take several code units in UTF-8
		std::array<CharT, 4> fill{static_cast<CharT>(' ')};
		std::size_t fill_length = 1;
		Alignment alignment = Alignment::Default;
		Sign sign = Sign::Minus;
		bool alternate = false;   ///< '#'
		bool zero_padding = false; ///< '0'
		Spec width;
		Spec precision;
		char type = '\0';

		static constexpr bool is_alignment(const CharT c) noexcept
		{
			return c == CharT('<') || c == CharT('>') || c == CharT('^');
		}

		/// Number of code units of the code point starting with `c`
		static constexpr std::size_t code_point_length(const CharT c) noexcept
		{
			if constexpr(sizeof(CharT) == 1)
			{
				const auto u = static_cast<unsigned char>(c);
				return (u >= 0xF0) ? 4 : (u >= 0xE0) ? 3 : (u >= 0xC0) ? 2 : 1;
			}
			else
			{
				return 1;
			}
		}

		template<typename ParseContext>
		static constexpr typename ParseContext::iterator parse_spec(typename ParseContext::iterator it, ParseContext& ctx, Spec& spec)
		{
			const auto end = ctx.end();
			if(it != end && *it == CharT('{'))
			{
				// Nested argument: "{}" or "{n}"
				++it;
				if(it != end && *it >= CharT('0') && *it <= CharT('9'))
				{
					std::size_t id = 0;
					while(it != end && *it >= CharT('0') && *it <= CharT('9'))
						id = id * 10 + static_cast<std::size_t>(*it++ - CharT('0'));
					ctx.check_arg_id(id);
					spec.arg_id = id;
				}
				else
				{
					spec.arg_id = ctx.next_arg_id();
				}
				if(it == end || *it != CharT('}'))
					throw std::format_error("Invalid nested width or precision");
#if defined(__cpp_lib_format) && __cpp_lib_format >= 202305L
				ctx.check_dynamic_spec_integral(spec.arg_id); // compile-time check of the argument type
#endif
				spec.is_arg = true;
				spec.is_set = true;
				return ++it;
			}

			if(it != end && *it >= CharT('0') && *it <= CharT('9'))
			{
				spec.value = 0;
				while(it != end && *it >= CharT('0') && *it <= CharT('9'))
					spec.value = spec.value * 10 + static_cast<std::size_t>(*it++ - CharT('0'));
				spec.is_set = true;
			}
			return it;
		}

		template<typename FormatContext>
		static std::size_t resolve(const Spec& spec, FormatContext& ctx)
		{
			if(!spec.is_arg)
				return spec.value;

			const auto get = [](const auto value) -> std::size_t
			{
				using T = std::remove_cvref_t<decltype(value)>;
				if constexpr(std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, CharT>)
				{
					if(value < 0)
						throw std::format_error("Negative width or precision");
					return static_cast<std::size_t>(value);
				}
				else
				{
					throw std::format_error("Width or precision is not an integer");
				}
			};
			const auto arg = ctx.arg(spec.arg_id);
			if constexpr(requires { arg.visit(get); })
				return arg.visit(get); // C++26 (std::visit_format_arg is deprecated)
			else
				return std::visit_format_arg(get, arg);
		}

	public:
		template<typename ParseContext>
		constexpr typename ParseContext::iterator parse(ParseContext& ctx)
		{
			auto it = ctx.begin();
			const auto end = ctx.end();

			// Fill and align
			if(it != end && *it != CharT('}'))
			{
				const auto length = code_point_length(*it);
				auto next = it;
				for(std::size_t i = 0; i < length && next != end; ++i)
					++next;
				if(next != end && is_alignment(*next) && *it != CharT('{') && *it != CharT('}'))
				{
					fill_length = length;
					for(std::size_t i = 0; i < length; ++i)
						fill[i] = *it++;
					alignment = static_cast<Alignment>(*it++);
				}
				else if(is_alignment(*it))
				{
					alignment = static_cast<Alignment>(*it++);
				}
			}

			// Sign
			if(it != end && (*it == CharT('+') || *it == CharT('-') || *it == CharT(' ')))
				sign = static_cast<Sign>(*it++);

			// Alternate form
			if(it != end && *it == CharT('#'))
			{
				alternate = true;
				++it;
			}

			// Zero padding
			if(it != end && *it == CharT('0'))
			{
				zero_padding = true;
				++it;
			}

			it = parse_spec(it, ctx, width);
			if(width.is_set && !width.is_arg && width.value == 0)
				throw std::format_error("Width must be positive");

			// Precision
			if(it != end && *it == CharT('.'))
			{
				++it;
				if(it == end || !((*it >= CharT('0') && *it <= CharT('9')) || *it == CharT('{')))
					throw std::format_error("Missing precision");
				it = parse_spec(it, ctx, precision);
			}

			if(it != end && *it == CharT('L'))
				throw std::format_error("Locale-specific formatting of fixed-point numbers is not supported");

			// Type
			if(it != end)
			{
				switch(*it)
				{
					case CharT('a'): case CharT('A'):
					case CharT('e'): case CharT('E'):
					case CharT('f'): case CharT('F'):
					case CharT('g'): case CharT('G'):
						type = static_cast<char>(*it++);
						break;
					default:
						break;
				}
			}

			if(it != end && *it != CharT('}'))
				throw std::format_error("Invalid format specification for a fixed-point number");
			return it;
		}

		template<typename FormatContext>
		typename FormatContext::iterator format(const fpm::fixed<B, I, F, R>& value, FormatContext& ctx) const
		{
			const std::size_t w = resolve(width, ctx);
			const bool has_precision = precision.is_set;
			const int p = has_precision ? static_cast<int>(std::min<std::size_t>(resolve(precision, ctx), 1'000'000)) : -1;

			// Convert, with room for a sign, a large precision and '#' additions
			const auto lower = static_cast<char>(type | 0x20);
			std::string text(static_cast<std::size_t>(std::max(p, 0)) + 160, '\0');
			std::to_chars_result result;
			char* const first = text.data();
			char* const last = first + text.size();
			switch(lower)
			{
				case 'a': result = fpm::to_chars(first, last, value, std::chars_format::hex, p); break;
				case 'e': result = fpm::to_chars(first, last, value, std::chars_format::scientific, has_precision ? p : 6); break;
				case 'f': result = fpm::to_chars(first, last, value, std::chars_format::fixed, has_precision ? p : 6); break;
				case 'g': result = fpm::to_chars(first, last, value, std::chars_format::general, has_precision ? p : 6); break;
				default:
					result = has_precision
						? fpm::to_chars(first, last, value, std::chars_format::general, p)
						: fpm::to_chars(first, last, value);
					break;
			}
			if(result.ec != std::errc{})
				throw std::format_error("Fixed-point value could not be formatted");
			text.resize(static_cast<std::size_t>(result.ptr - first));

			if(alternate)
				fpm::detail::apply_alternate_form(text, lower == '\0' ? (has_precision ? '\0' : 'x') : lower, p);

			if(type == 'A' || type == 'E' || type == 'G' || type == 'F')
			{
				for(auto& c : text)
					if(c >= 'a' && c <= 'z')
						c = static_cast<char>(c - 'a' + 'A');
			}

			// Sign
			const bool negative = !text.empty() && text[0] == '-';
			const std::size_t sign_length = (negative || sign != Sign::Minus) ? 1 : 0;
			const char sign_char = negative ? '-' : static_cast<char>(sign);
			const std::string_view digits = std::string_view(text).substr(negative ? 1 : 0);
			const std::size_t content = sign_length + digits.size();

			auto out = ctx.out();
			const auto put = [&](const std::string_view str)
			{
				for(const char c : str)
					*out++ = static_cast<CharT>(c);
			};
			const auto put_fill = [&](std::size_t count)
			{
				for(; count > 0; --count)
					for(std::size_t i = 0; i < fill_length; ++i)
						*out++ = fill[i];
			};

			const std::size_t padding = (w > content) ? w - content : 0;
			if(alignment == Alignment::Default && zero_padding)
			{
				// Zeros between the sign and the digits
				if(sign_length != 0)
					*out++ = static_cast<CharT>(sign_char);
				for(std::size_t i = 0; i < padding; ++i)
					*out++ = static_cast<CharT>('0');
				put(digits);
				return out;
			}

			std::size_t before = padding;
			if(alignment == Alignment::Left)
				before = 0;
			else if(alignment == Alignment::Center)
				before = padding / 2;
			put_fill(before);
			if(sign_length != 0)
				*out++ = static_cast<CharT>(sign_char);
			put(digits);
			put_fill(padding - before);
			return out;
		}
	};
}
