#include <benchmark/benchmark.h>
#include <fpm/fixed.hpp>
#include <cnl/fixed_point.h>

#include <fixmath.h>

#define BENCHMARK_TEMPLATE1_CAPTURE(func, test_case_name, a, ...) \
	BENCHMARK_PRIVATE_DECLARE(func) = \
		(::benchmark::internal::RegisterBenchmarkInternal( \
			new ::benchmark::internal::FunctionBenchmark( \
				#func "<" #a ">/" #test_case_name, \
				[](::benchmark::State& st) { func<a>(st, __VA_ARGS__); })))

// Constants for our arithmetic operands.
// Stored as volatile to force the compiler to read them and
// not optimize the entire expression into a constant.
static volatile int16_t s_x = 1543;
static volatile int16_t s_y = 2552;

template<typename TValue>
static void arithmetic(benchmark::State& state, TValue (*func)(TValue, TValue))
{
	for(auto _ : state)
	{
		TValue x{ static_cast<TValue>(static_cast<int16_t>(s_x)) }, y{ static_cast<TValue>(static_cast<int16_t>(s_y)) };
		benchmark::DoNotOptimize(func(x, y));
	}
}

#define FUNC(TYPE, OP) \
	[](TYPE x, TYPE y) -> TYPE { return x OP y; }


BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, add, float, FUNC(float, +));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, sub, float, FUNC(float, -));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, mul, float, FUNC(float, *));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, div, float, FUNC(float, /));

BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, add, double, FUNC(double, +));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, sub, double, FUNC(double, -));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, mul, double, FUNC(double, *));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, div, double, FUNC(double, /));

BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, add, Fix16, FUNC(Fix16, +));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, sub, Fix16, FUNC(Fix16, -));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, mul, Fix16, FUNC(Fix16, *));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, div, Fix16, FUNC(Fix16, /));

using CnlFixed16 = cnl::fixed_point<std::int32_t, -16>;
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, add, CnlFixed16, FUNC(CnlFixed16, +));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, sub, CnlFixed16, FUNC(CnlFixed16, -));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, mul, CnlFixed16, FUNC(CnlFixed16, *));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, div, CnlFixed16, FUNC(CnlFixed16, /));


#define BENCHMARK_TEMPLATE1_CAPTURE_FPM(a_fpm) \
	BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, add, a_fpm, FUNC(a_fpm, +)); \
	BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, sub, a_fpm, FUNC(a_fpm, -)); \
	BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, mul, a_fpm, FUNC(a_fpm, *)); \
	BENCHMARK_TEMPLATE1_CAPTURE(arithmetic, div, a_fpm, FUNC(a_fpm, /));

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

