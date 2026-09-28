#include <benchmark/benchmark.h>
#include <fpm/fixed.hpp>
#include <fpm/fixed/math.hpp>
#include <cnl/fixed_point.h>
#include <fixmath.h>

#define BENCHMARK_TEMPLATE1_CAPTURE(func, test_case_name, a, ...) \
	BENCHMARK_PRIVATE_DECLARE(func) = \
		(::benchmark::internal::RegisterBenchmarkInternal( \
			new ::benchmark::internal::FunctionBenchmark( \
				#func "<" #a ">/" #test_case_name, \
				[](::benchmark::State& st) { func<a>(st, __VA_ARGS__); })))

template <fix16_t (*func)(fix16_t)>
static Fix16 fix16_func(const Fix16 f)
{
	return (*func)(f);
}

// Constants for our power function arguments.
// Stored as volatile to force the compiler to read them and
// not optimize the entire expression into a constant.
static volatile int16_t s_x = 2734;
static volatile int16_t s_y =  174;

template<typename TValue>
static void power1(benchmark::State& state, TValue (*func)(TValue))
{
	for(auto _ : state)
	{
		TValue x{ static_cast<TValue>(s_x / 256.0) };
		benchmark::DoNotOptimize(func(x));
	}
}

template<typename TValue>
static void power1(benchmark::State& state, TValue (*func)(const TValue&))
{
	for(auto _ : state)
	{
		TValue x{ static_cast<TValue>(s_x / 256.0) };
		benchmark::DoNotOptimize(func(x));
	}
}

template<typename TValue>
static void power2(benchmark::State& state, TValue (*func)(TValue, TValue))
{
	for(auto _ : state)
	{
		TValue x{ static_cast<TValue>(s_x / 256.0) };
		TValue y{ static_cast<TValue>(s_y / 256.0) };
		benchmark::DoNotOptimize(func(x, y));
	}
}

BENCHMARK_TEMPLATE1_CAPTURE(power1, sqrt,  float, &std::sqrt);
BENCHMARK_TEMPLATE1_CAPTURE(power1, cbrt,  float, &std::cbrt);
BENCHMARK_TEMPLATE1_CAPTURE(power1, log,   float, &std::log);
BENCHMARK_TEMPLATE1_CAPTURE(power1, log2,  float, &std::log2);
BENCHMARK_TEMPLATE1_CAPTURE(power1, log10, float, &std::log10);
BENCHMARK_TEMPLATE1_CAPTURE(power1, exp,   float, &std::exp);
BENCHMARK_TEMPLATE1_CAPTURE(power1, exp2,  float, &std::exp2);
BENCHMARK_TEMPLATE1_CAPTURE(power2, pow,   float, &std::pow);

BENCHMARK_TEMPLATE1_CAPTURE(power1, sqrt,  double, &std::sqrt);
BENCHMARK_TEMPLATE1_CAPTURE(power1, cbrt,  double, &std::cbrt);
BENCHMARK_TEMPLATE1_CAPTURE(power1, log,   double, &std::log);
BENCHMARK_TEMPLATE1_CAPTURE(power1, log2,  double, &std::log2);
BENCHMARK_TEMPLATE1_CAPTURE(power1, log10, double, &std::log10);
BENCHMARK_TEMPLATE1_CAPTURE(power1, exp,   double, &std::exp);
BENCHMARK_TEMPLATE1_CAPTURE(power1, exp2,  double, &std::exp2);
BENCHMARK_TEMPLATE1_CAPTURE(power2, pow,   double, &std::pow);

BENCHMARK_TEMPLATE1_CAPTURE(power1, sqrt, Fix16, fix16_func<&fix16_sqrt>);
BENCHMARK_TEMPLATE1_CAPTURE(power1, log2, Fix16, fix16_func<&fix16_log2>);
BENCHMARK_TEMPLATE1_CAPTURE(power1, exp,  Fix16, fix16_func<&fix16_exp>);

using CnlFixed16 = cnl::fixed_point<std::int32_t, -16>;
BENCHMARK_TEMPLATE1_CAPTURE(power1, sqrt, CnlFixed16, &cnl::sqrt);
BENCHMARK_TEMPLATE1_CAPTURE(power1, exp,  CnlFixed16, &cnl::exp);


#define BENCHMARK_TEMPLATE1_CAPTURE_FPM(a_fpm) \
	BENCHMARK_TEMPLATE1_CAPTURE(power1, sqrt,  a_fpm, &fpm::sqrt); \
	BENCHMARK_TEMPLATE1_CAPTURE(power1, cbrt,  a_fpm, &fpm::cbrt); \
	BENCHMARK_TEMPLATE1_CAPTURE(power1, log,   a_fpm, &fpm::log); \
	BENCHMARK_TEMPLATE1_CAPTURE(power1, log2,  a_fpm, &fpm::log2); \
	BENCHMARK_TEMPLATE1_CAPTURE(power1, log10, a_fpm, &fpm::log10); \
	BENCHMARK_TEMPLATE1_CAPTURE(power1, exp,   a_fpm, &fpm::exp); \
	BENCHMARK_TEMPLATE1_CAPTURE(power1, exp2,  a_fpm, &fpm::exp2); \
	BENCHMARK_TEMPLATE1_CAPTURE(power2, pow,   a_fpm, &fpm::pow);

#ifdef FPM_INT128
namespace fpm
{
	using fixed_44_20 = fpm::fixed<int64_t, FPM_INT128, 20>;
	using fixed_52_12 = fpm::fixed<int64_t, FPM_INT128, 12>;
}
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_16_48);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_24_40);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_32_32);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_40_24);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_44_20);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_48_16);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_52_12);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_56_8);
#endif
namespace fpm
{
	using fixed_22_10 = fpm::fixed<int32_t, int64_t, 10>;
	using fixed_20_12 = fpm::fixed<int32_t, int64_t, 12>;
	using fixed_18_14 = fpm::fixed<int32_t, int64_t, 14>;
}
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_8_24);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_16_16);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_18_14);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_20_12);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_22_10);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_24_8);
BENCHMARK_TEMPLATE1_CAPTURE_FPM(fpm::fixed_8_8);

