#include <fpm/fixed.hpp>
#include <fpm/fixed/math.hpp>
#include <fpm/fraction.hpp>
#include <fpm/fraction/math.hpp>
#include <cmath>
#include <fstream>
#include <string>

// The accuracy of the trigonometry for angles that are stored as a fraction of a turn.
// The files have the columns of the ones for radians (without libfixmath, which has no such functions),
// for the same types of the numbers. The angles have 16 bits for the 32-bit types, and 32 bits for the 64-bit types.

namespace
{
	constexpr double TWO_PI = 6.283185307179586476925286766559;

	using angle_16 = fpm::fraction<std::uint16_t>;
	using angle_32 = fpm::fraction<std::uint32_t>;
	using fixed_20_12 = fpm::fixed<std::int32_t, std::int64_t, 12>;

	class csv_output
	{
	public:
		explicit csv_output(const std::string& filename)
			: m_stream(filename)
		{
			m_stream.setf(std::ios::fixed);
			m_stream.precision(12);
			m_stream << "x,real,Q24.8,Q20.12,Q16.16,Q8.24,Q32.32,Q24.40,Q16.48,fix16\n";
		}

		/// A row with the results for the types of the numbers: `callable<Angle, Number>()`
		template<typename Callable>
		void write_row(const double x, const double real, Callable&& callable)
		{
			m_stream << x
				<< "," << real
				<< "," << callable.template operator()<angle_16, fpm::fixed_24_8>()
				<< "," << callable.template operator()<angle_16, fixed_20_12>()
				<< "," << callable.template operator()<angle_16, fpm::fixed_16_16>()
				<< "," << callable.template operator()<angle_16, fpm::fixed_8_24>()
#ifdef FPM_INT128
				<< "," << callable.template operator()<angle_32, fpm::fixed_32_32>()
				<< "," << callable.template operator()<angle_32, fpm::fixed_24_40>()
				<< "," << callable.template operator()<angle_32, fpm::fixed_16_48>()
#endif
				<< ",-\n";
		}

	private:
		std::ofstream m_stream;
	};

	/// An angle in turns, in [0, 1)
	double turns(const double radians)
	{
		const double result = radians / TWO_PI;
		return result - std::floor(result);
	}
}

void write_fraction_accuracy()
{
	csv_output out_sin("sin_turns.csv");
	csv_output out_cos("cos_turns.csv");
	csv_output out_tan("tan_turns.csv");
	csv_output out_atan2("atan2_turns.csv");
	for(int angle = 0; angle < 360; ++angle)
	{
		// A part of a turn, as a number: like the radians, the angle is not exactly the one of the type
		const double val = angle / 360.0;

		out_sin.write_row(val, std::sin(val * TWO_PI), [&]<typename A, typename N>() { return static_cast<double>(fpm::sin<N>(A{val})); });
		out_cos.write_row(val, std::cos(val * TWO_PI), [&]<typename A, typename N>() { return static_cast<double>(fpm::cos<N>(A{val})); });
		if((angle + 90) % 180 != 0)
			out_tan.write_row(val, std::tan(val * TWO_PI), [&]<typename A, typename N>() { return static_cast<double>(fpm::tan<N>(A{val})); });

		const auto y = std::sin(val * TWO_PI);
		const auto x = std::cos(val * TWO_PI);
		out_atan2.write_row(val, turns(std::atan2(y, x)), [&]<typename A, typename N>() { return static_cast<double>(fpm::atan2<A>(N{y}, N{x})); });
	}

	csv_output out_asin("asin_turns.csv");
	csv_output out_acos("acos_turns.csv");
	for(int value = -100; value <= 100; ++value)
	{
		const double val = value / 100.0;
		out_asin.write_row(val, turns(std::asin(val)), [&]<typename A, typename N>() { return static_cast<double>(fpm::asin<A>(N{val})); });
		out_acos.write_row(val, turns(std::acos(val)), [&]<typename A, typename N>() { return static_cast<double>(fpm::acos<A>(N{val})); });
	}

	csv_output out_atan("atan_turns.csv");
	for(int value = -5000; value <= 5000; value += 5)
	{
		const double val = value / 1000.0;
		out_atan.write_row(val, turns(std::atan(val)), [&]<typename A, typename N>() { return static_cast<double>(fpm::atan<A>(N{val})); });
	}
}
