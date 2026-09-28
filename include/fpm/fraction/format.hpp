#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <version>

#include "../detail/charconv.hpp"
#include "charconv.hpp"
#include "../fraction.hpp"

// `std::format` support for fractions. Independent of streams and locales: see <fpm/fraction/ios.hpp> for those.

namespace std
{
	/// `std::format` support, like for floating-point types: fill and align, sign, '#', '0', width and precision
	/// (also as nested arguments, e.g. "{:{}.{}f}"), and the types 'a', 'A', 'e', 'E', 'f', 'F', 'g' and 'G'.
	/// Locale-specific formatting ('L') is not supported.
	///
	/// Like for floating-point types, the output is locale-independent and based on `to_chars`:
	/// without type and precision it is the shortest representation that reads back to the same value.
	template<typename CharT, typename B>
	struct formatter<fpm::fraction<B>, CharT>
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
				throw std::format_error("Locale-specific formatting of fractions is not supported");

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
				throw std::format_error("Invalid format specification for a fraction");
			return it;
		}

		template<typename FormatContext>
		typename FormatContext::iterator format(const fpm::fraction<B>& value, FormatContext& ctx) const
		{
			const std::size_t w = resolve(width, ctx);
			const bool has_precision = precision.is_set;
			const int32_t p = has_precision ? static_cast<int32_t>(std::min<std::size_t>(resolve(precision, ctx), 1'000'000)) : -1;

			// Convert, with room for a sign, a large precision and '#' additions
			const auto lower = static_cast<char>(type | 0x20);
			std::string text(static_cast<std::size_t>(std::max<int32_t>(p, 0)) + 160, '\0');
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
				throw std::format_error("Fraction could not be formatted");
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
