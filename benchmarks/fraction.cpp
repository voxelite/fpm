#include <benchmark/benchmark.h>
#include <fpm/fixed.hpp>
#include <fpm/fixed/math.hpp>
#include <fpm/fraction.hpp>
#include <fpm/fraction/math.hpp>

// Fractions, and the trigonometry for angles that are stored as a fraction of a turn.
// The operations have the names of the ones for fixed-point numbers, so their results are compared with those.

#define BENCHMARK_TEMPLATE1_CAPTURE(func, test_case_name, a, ...) \
	BENCHMARK_PRIVATE_DECLARE(func) = \
		(::benchmark::internal::RegisterBenchmarkInternal( \
			new ::benchmark::internal::FunctionBenchmark( \
				#func "<" #a ">/" #test_case_name, \
				[](::benchmark::State& st) { func<a>(st, __VA_ARGS__); })))

// Constants for the operands.
// Stored as volatile to force the compiler to read them and
// not optimize the entire expression into a constant.
static volatile uint16_t s_x = 174 * 64;
static volatile uint16_t s_y = 9731;
static volatile int16_t s_integer = 7;

namespace fpm
{
	using fraction_8 = fraction<uint8_t>;
	using fraction_16 = fraction<uint16_t>;
	using fraction_32 = fraction<uint32_t>;
	using fraction_64 = fraction<uint64_t>;

	/// An angle as a fraction of a turn, for numbers of a fixed-point type
	template<typename Angle, typename Number>
	struct turns
	{
		using angle = Angle;
		using number = Number;
	};

	using turns_16_for_8_24 = turns<fraction_16, fixed_8_24>;
	using turns_16_for_16_16 = turns<fraction_16, fixed_16_16>;
	using turns_16_for_24_8 = turns<fraction_16, fixed_24_8>;
	using turns_8_for_8_8 = turns<fraction_8, fixed_8_8>;
#ifdef FPM_INT128
	using turns_32_for_16_48 = turns<fraction_32, fixed_16_48>;
	using turns_32_for_32_32 = turns<fraction_32, fixed_32_32>;
	using turns_32_for_48_16 = turns<fraction_32, fixed_48_16>;
#endif
}

/// The fraction with the bits of the operand at its top, so it's the same part of a turn for every type
template<typename TFraction>
static TFraction operand(const uint16_t bits)
{
	return TFraction(fpm::fraction<uint16_t>::from_raw_value(bits));
}

template<>
fpm::fraction<uint16_t> operand<fpm::fraction<uint16_t>>(const uint16_t bits)
{
	return fpm::fraction<uint16_t>::from_raw_value(bits);
}

template<typename TFraction>
static void fraction(benchmark::State& state, TFraction (*func)(TFraction, TFraction, int))
{
	for(auto _ : state)
	{
		const auto x = operand<TFraction>(s_x);
		const auto y = operand<TFraction>(s_y);
		benchmark::DoNotOptimize(func(x, y, s_integer));
	}
}

#define BENCHMARK_FRACTION(a_fpm) \
	BENCHMARK_TEMPLATE1_CAPTURE(fraction, add, a_fpm, [](a_fpm x, a_fpm y, int) -> a_fpm { return x + y; }); \
	BENCHMARK_TEMPLATE1_CAPTURE(fraction, sub, a_fpm, [](a_fpm x, a_fpm y, int) -> a_fpm { return x - y; }); \
	BENCHMARK_TEMPLATE1_CAPTURE(fraction, mul_int, a_fpm, [](a_fpm x, a_fpm, int i) -> a_fpm { return x * i; }); \
	BENCHMARK_TEMPLATE1_CAPTURE(fraction, div_int, a_fpm, [](a_fpm x, a_fpm, int i) -> a_fpm { return x / i; }); \
	BENCHMARK_TEMPLATE1_CAPTURE(fraction, difference, a_fpm, [](a_fpm x, a_fpm y, int) -> a_fpm { return fpm::difference(x, y).distance; });

BENCHMARK_FRACTION(fpm::fraction_8);
BENCHMARK_FRACTION(fpm::fraction_16);
BENCHMARK_FRACTION(fpm::fraction_32);
BENCHMARK_FRACTION(fpm::fraction_64);

/// A number for an angle
template<typename TTurns>
static void turns_forward(benchmark::State& state, typename TTurns::number (*func)(typename TTurns::angle))
{
	for(auto _ : state)
	{
		const auto x = operand<typename TTurns::angle>(s_x);
		benchmark::DoNotOptimize(func(x));
	}
}

/// An angle for numbers: the ones of the benchmark for radians
template<typename TTurns>
static void turns_inverse(benchmark::State& state, typename TTurns::angle (*func)(typename TTurns::number))
{
	for(auto _ : state)
	{
		const typename TTurns::number x{static_cast<int16_t>(s_x / 64) / 256.0};
		benchmark::DoNotOptimize(func(x));
	}
}

#define BENCHMARK_TURNS(a_fpm) \
	BENCHMARK_TEMPLATE1_CAPTURE(turns_forward, sin, a_fpm, [](a_fpm::angle x) { return fpm::sin<a_fpm::number>(x); }); \
	BENCHMARK_TEMPLATE1_CAPTURE(turns_forward, cos, a_fpm, [](a_fpm::angle x) { return fpm::cos<a_fpm::number>(x); }); \
	BENCHMARK_TEMPLATE1_CAPTURE(turns_forward, tan, a_fpm, [](a_fpm::angle x) { return fpm::tan<a_fpm::number>(x); }); \
	BENCHMARK_TEMPLATE1_CAPTURE(turns_inverse, asin, a_fpm, [](a_fpm::number x) { return fpm::asin<a_fpm::angle>(x); }); \
	BENCHMARK_TEMPLATE1_CAPTURE(turns_inverse, acos, a_fpm, [](a_fpm::number x) { return fpm::acos<a_fpm::angle>(x); }); \
	BENCHMARK_TEMPLATE1_CAPTURE(turns_inverse, atan, a_fpm, [](a_fpm::number x) { return fpm::atan<a_fpm::angle>(x); }); \
	BENCHMARK_TEMPLATE1_CAPTURE(turns_inverse, atan2, a_fpm, [](a_fpm::number x) { return fpm::atan2<a_fpm::angle>(x, x + 2); });

#ifdef FPM_INT128
BENCHMARK_TURNS(fpm::turns_32_for_16_48);
BENCHMARK_TURNS(fpm::turns_32_for_32_32);
BENCHMARK_TURNS(fpm::turns_32_for_48_16);
#endif
BENCHMARK_TURNS(fpm::turns_16_for_8_24);
BENCHMARK_TURNS(fpm::turns_16_for_16_16);
BENCHMARK_TURNS(fpm::turns_16_for_24_8);
BENCHMARK_TURNS(fpm::turns_8_for_8_8);
