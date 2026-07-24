#include <benchmark/benchmark.h>
#include <fpm/fixed.hpp>
#include <cnl/fixed_point.h>

#include <fixmath.h>

#define BENCHMARK_TEMPLATE1_CAPTURE(func, a, b, ...) \
	BENCHMARK_PRIVATE_DECLARE(func) = \
		(::benchmark::internal::RegisterBenchmarkInternal( \
			new ::benchmark::internal::FunctionBenchmark( \
				#func "<" #a ">/" #b, \
				[](::benchmark::State& st) { func<a,b>(st, __VA_ARGS__); })))

// Constants for to_float argument.
// Stored as volatile to force the compiler to read them and
// not optimize the entire expression into a constant.
static volatile int16_t s_x = 1543;

template<typename TValue, typename TResult>
static void to_float(benchmark::State& state, TResult (*func)(TValue))
{
	for(auto _ : state)
	{
		TValue x{ static_cast<TValue>(static_cast<int16_t>(s_x)) };
		benchmark::DoNotOptimize(func(x));
	}
}

#define FUNC(TYPE, FLOATING) \
	[](TYPE x) -> FLOATING { return static_cast<FLOATING>(x); }

BENCHMARK_TEMPLATE1_CAPTURE(to_float, Fix16, float, FUNC(Fix16, float));
BENCHMARK_TEMPLATE1_CAPTURE(to_float, Fix16, double, FUNC(Fix16, double));

using CnlFixed16 = cnl::fixed_point<std::int32_t, -16>;
BENCHMARK_TEMPLATE1_CAPTURE(to_float, CnlFixed16, float, FUNC(CnlFixed16, float));
BENCHMARK_TEMPLATE1_CAPTURE(to_float, CnlFixed16, double, FUNC(CnlFixed16, double));


#define BENCHMARK_TEMPLATE1_CAPTURE_FPM(a_fpm) \
	BENCHMARK_TEMPLATE1_CAPTURE(to_float, a_fpm, float, FUNC(a_fpm, float)); \
	BENCHMARK_TEMPLATE1_CAPTURE(to_float, a_fpm, double, FUNC(a_fpm, double));

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

