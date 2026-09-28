#include <benchmark/benchmark.h>
#include <fpm/fixed/fixed.hpp>
#include <cnl/fixed_point.h>

#include <fixmath.h>

#define BENCHMARK_TEMPLATE1_CAPTURE(func, test_case_name, a, b, ...) \
	BENCHMARK_PRIVATE_DECLARE(func) = \
		(::benchmark::internal::RegisterBenchmarkInternal( \
			new ::benchmark::internal::FunctionBenchmark( \
				#func "<" #a "," #b ">/" #test_case_name, \
				[](::benchmark::State& st) { func<a,b>(st, __VA_ARGS__); })))

// Constants for our arithmetic2 operands.
// Stored as volatile to force the compiler to read them and
// not optimize the entire expression into a constant.
static volatile int16_t s_x = 1543;
static volatile int16_t s_y = 2552;

template<typename TValue, typename TValue2>
static void arithmetic2(benchmark::State& state, TValue (*func)(TValue, TValue2))
{
	for(auto _ : state)
	{
		TValue x{ static_cast<TValue>(static_cast<int16_t>(s_x)) };
		TValue2 y{ static_cast<TValue2>(static_cast<int16_t>(s_y)) };
		benchmark::DoNotOptimize(func(x, y));
	}
}

#define FUNC(TYPE, TYPE2, OP) \
	[](TYPE x, TYPE2 y) -> TYPE { return x OP y; }


BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, add, float, int, FUNC(float, int, +));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, sub, float, int, FUNC(float, int, -));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, mul, float, int, FUNC(float, int, *));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, div, float, int, FUNC(float, int, /));

BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, add, double, int, FUNC(double, int, +));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, sub, double, int, FUNC(double, int, -));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, mul, double, int, FUNC(double, int, *));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, div, double, int, FUNC(double, int, /));

BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, add, Fix16, int, FUNC(Fix16, int, +));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, sub, Fix16, int, FUNC(Fix16, int, -));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, mul, Fix16, int, FUNC(Fix16, int, *));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, div, Fix16, int, FUNC(Fix16, int, /));

using CnlFixed16 = cnl::fixed_point<std::int32_t, -16>;
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, add, CnlFixed16, int, FUNC(CnlFixed16, int, +));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, sub, CnlFixed16, int, FUNC(CnlFixed16, int, -));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, mul, CnlFixed16, int, FUNC(CnlFixed16, int, *));
BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, div, CnlFixed16, int, FUNC(CnlFixed16, int, /));


#define BENCHMARK_TEMPLATE1_CAPTURE_FPM(a_fpm) \
	BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, add, a_fpm, int, FUNC(a_fpm, int, +)); \
	BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, sub, a_fpm, int, FUNC(a_fpm, int, -)); \
	BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, mul, a_fpm, int, FUNC(a_fpm, int, *)); \
	BENCHMARK_TEMPLATE1_CAPTURE(arithmetic2, div, a_fpm, int, FUNC(a_fpm, int, /));

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

