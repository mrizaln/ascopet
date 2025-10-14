# ascopet

`a-scope-t`: Asynchronous scope tracer. Simple scope time measurement library for C++20.

## Introduction

Performance optimization is a common task in C++ programming. One of the thing that we can do to measure the performance of our code is to measure the time it takes to execute a given piece of code. Using RAII we can measure how much time a given scope takes to execute by using a type that records the time point when it is created and the time point when it is destroyed. This library provides such functionality.

## Features

- Simple
- C++20 compatible
- Minimal overhead (**60ns** of overhead per scope measurement on 2.5GHz x86_64 Linux machine)
- Asynchronous timing handling

## Usage

This library is using asynchronous approach in its timing management by collecting the time records in a background thread that run every once in a while (polling). You need to initialize the library before using it. If you are calling `ascopet::trace` before the library is initialized, the scope time measurement won't be recorded, but it will be written into its own TLS buffer.

### Initializing the library

```cpp
#include <ascopet/ascopet.h>

int main()
{
    // you are required to initialize the library before using it
    auto* ascopet = ascopet::init({
        .immediately_start = false,                             // immediately start the background thread
        .poll_interval     = std::chrono::milliseconds{ 100 },  // polling interval
        .record_capacity   = 1024,                              // max number of records for each unique entry (by name)
        .buffer_capacity   = 1024,                              // the size of the thread-local storage
    });

    // if immediately_start is false you need to start the background thread manually
    ascopet->start_tracing();

    // you can pause the background thread if you want to
    ascopet->pause_tracing();

    // to check whether the background thread is running or not
    if (ascopet->is_tracing()) {
        // do something
    }
}
```

The function `ascopet::trace` return a `Tracer` RAII object that will record the time when it is created and the time when it is destroyed into a thread-local storage. The time is recorded is in timestamp counter([`rdtsc`](https://en.wikipedia.org/wiki/Time_Stamp_Counter) assuming `constant_tsc`). `Tracer` is non-movable, non-copyable, and non-assignable. Make sure to always bind the `Tracer` object to a variable, otherwise it will be destroyed immediately and the time recorded will be meaningless.

### Tracing a scope

```cpp
#include <ascopet/ascopet.h>

void foo()
{
    // use custom name
    {
        auto trace = ascopet::trace("foo");
        // do something
    }

    // use function name provided by std::source_location
    {
        auto trace = ascopet::trace();  // overload with std::source_location::current() as default argument
        // do something
    }
}
```

Each thread that calls `ascopet::trace` will create its own thread-local storage on its first call only if the library is initialized.

**One thing to note is that the string provided to the `ascopet::trace` function must be a string with static lifetime.**

### Getting the results

```cpp
#include <ascopet/ascopet.h>

// ...

int main()
{
    // ....

    auto* ascopet = ascopet::instance();

    assert(ascopet != nullptr);

    // get the results but don't clear the records
    auto report = ascopet->report();

    // get the results and clear the records without removing the entries
    auto report = ascopet->report_consume(false);

    // get the results and clear the records and remove the entries
    auto report = ascopet->report_consume(true);

    // if you want the raw records then you can always use
    auto raw_report = ascopet->raw_report();
}
```

The `ascopet::report*` functions return a `Report` which is just a map of threads to a map of entries to a `TimingStat`. This `TimingStat` contains data like the mean, median, stdev, min, and max for both the scope time itself and the time between calls. The `ascopet::raw_report` function returns a `RawReport` which is just a map of threads to a map of entries to a record buffer. This operation copies the data so you can't directly modify the stored record in the `Ascopet` instance.

```cpp
struct TimingStat
{
    struct Stat
    {
        Duration mean;
        Duration median;
        Duration stdev;
        Duration min;
        Duration max;
    };

    Stat        duration;
    Stat        interval;
    std::size_t count = 0;
};

struct Record
{
    Duration start;
    Duration end;
};

using Report    = ThreadMap<StrMap<TimingStat>>;
using RawReport = ThreadMap<StrMap<RingBuf<Record>>>;
```

## Benchmark

In order to measure the overhead of the library, a simple benchmark was created. The benchmark is done by creating `Tracer` object repeatedly in an empty scope in a tight loop. This loop is duplicated in multiple threads corresponds to the number of core my computer has.

```cpp
// ...
for (auto i = 0u; i < count; ++i) {
    auto trace = ascopet::trace(name);    // timing overhead
}
```

The overhead is then defined as the time it takes between two calls to `ascopet::trace`.

The following result is obtained on my Intel(R) Core(TM) i5-10500H (6 core/12 threads) with the frequency locked to 2.5 GHz:

```txt
Report:
    Thread 139900365170368
    > contention1
        > Dur   [ mean: 9ns (+/- 0ns) | median: 10ns | min: 8ns | max: 12ns ]
        > Intvl [ mean: 20ns (+/- 0ns) | median: 20ns | min: 19ns | max: 23ns ]
        > Count: 92160
    Thread 139900348384960
    > contention3
        > Dur   [ mean: 9ns (+/- 0ns) | median: 9ns | min: 8ns | max: 15ns ]
        > Intvl [ mean: 20ns (+/- 1ns) | median: 20ns | min: 19ns | max: 40ns ]
        > Count: 92160
    Thread 139900323206848
    > contention6
        > Dur   [ mean: 9ns (+/- 0ns) | median: 10ns | min: 8ns | max: 12ns ]
        > Intvl [ mean: 20ns (+/- 2ns) | median: 20ns | min: 19ns | max: 41ns ]
        > Count: 92160
    Thread 139900339992256
    > contention4
        > Dur   [ mean: 9ns (+/- 0ns) | median: 9ns | min: 8ns | max: 14ns ]
        > Intvl [ mean: 20ns (+/- 1ns) | median: 20ns | min: 19ns | max: 40ns ]
        > Count: 92160
    Thread 139900356777664
    > contention2
        > Dur   [ mean: 9ns (+/- 0ns) | median: 10ns | min: 8ns | max: 12ns ]
        > Intvl [ mean: 20ns (+/- 1ns) | median: 20ns | min: 19ns | max: 40ns ]
        > Count: 92160
    Thread 139900331599552
    > contention5
        > Dur   [ mean: 11ns (+/- 1ns) | median: 12ns | min: 9ns | max: 15ns ]
        > Intvl [ mean: 30ns (+/- 1ns) | median: 31ns | min: 20ns | max: 53ns ]
        > Count: 92160
```

The code used to measure the overhead is [here](example/source/trace.cpp).
