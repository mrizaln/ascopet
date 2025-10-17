#pragma once

// copied with slight modification from:
// https://gist.github.com/pmttavara/6f06fc5c7679c07375483b06bb77430c

// SPDX-FileCopyrightText: © 2022 Phillip Trudeau-Tavara <pmttavara@protonmail.com>
// SPDX-License-Identifier: 0BSD

#ifdef __WIN32__

// https://hero.handmade.network/forums/code-discussion/t/7485-queryperformancefrequency_returning_10mhz_bug/2
// https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/timers#partition-reference-tsc-mechanism

#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>

inline uint64_t get_rdtsc_freq(void)
{
    // Cache the answer so that multiple calls never take the slow path more than once
    static uint64_t tsc_freq = 0;
    if (tsc_freq) {
        return tsc_freq;
    }

    // Fast path: Load kernel-mapped memory page
    HMODULE ntdll = LoadLibraryA("ntdll.dll");
    if (ntdll) {
        int (*NtQuerySystemInformation)(int, void*, unsigned int, unsigned int*)
            = (int (*)(int, void*, unsigned int, unsigned int*)
            )GetProcAddress(ntdll, "NtQuerySystemInformation");
        if (NtQuerySystemInformation) {

            volatile uint64_t* hypervisor_shared_page = NULL;
            unsigned int       size                   = 0;

            // SystemHypervisorSharedPageInformation == 0xc5
            int result = (NtQuerySystemInformation)(0xc5,
                                                    (void*)&hypervisor_shared_page,
                                                    sizeof(hypervisor_shared_page),
                                                    &size);

            // success
            if (size == sizeof(hypervisor_shared_page) && result >= 0) {
                // docs say ReferenceTime = ((VirtualTsc * TscScale) >> 64)
                //      set ReferenceTime = 10000000 = 1 second @ 10MHz, solve for VirtualTsc
                //       =>    VirtualTsc = 10000000 / (TscScale >> 64)
                tsc_freq = (10'000'000ull << 32) / (hypervisor_shared_page[1] >> 32);
                // If your build configuration supports 128 bit arithmetic, do this:
                // tsc_freq = ((unsigned __int128)10000000ull << (unsigned __int128)64ull) /
                // hypervisor_shared_page[1];
            }
        }
        FreeLibrary(ntdll);
    }

    // Slow path
    if (!tsc_freq) {
        // Get time before sleep
        uint64_t qpc_begin = 0;
        QueryPerformanceCounter((LARGE_INTEGER*)&qpc_begin);
        uint64_t tsc_begin = __rdtsc();

        Sleep(2);

        // Get time after sleep
        uint64_t qpc_end = qpc_begin + 1;
        QueryPerformanceCounter((LARGE_INTEGER*)&qpc_end);
        uint64_t tsc_end = __rdtsc();

        // Do the math to extrapolate the RDTSC ticks elapsed in 1 second
        uint64_t qpc_freq = 0;
        QueryPerformanceFrequency((LARGE_INTEGER*)&qpc_freq);
        tsc_freq = (tsc_end - tsc_begin) * qpc_freq / (qpc_end - qpc_begin);
    }

    // Failure case
    if (!tsc_freq) {
        tsc_freq = 1'000'000'000;
    }

    return tsc_freq;
}

#else /* linux */

// https://linux.die.net/man/2/perf_event_open
// https://stackoverflow.com/a/57835630

#include <linux/perf_event.h>
#include <sys/mman.h>

#include <unistd.h>
#include <x86intrin.h>

#include <cstdint>
#include <ctime>

inline uint64_t get_rdtsc_freq(void)
{
    // Cache the answer so that multiple calls never take the slow path more than once
    static uint64_t tsc_freq = 0;
    if (tsc_freq) {
        return tsc_freq;
    }

    // Fast path: Load kernel-mapped memory page
    struct perf_event_attr pe = {};
    pe.type                   = PERF_TYPE_HARDWARE;
    pe.size                   = sizeof(struct perf_event_attr);
    pe.config                 = PERF_COUNT_HW_INSTRUCTIONS;
    pe.disabled               = 1;
    pe.exclude_kernel         = 1;
    pe.exclude_hv             = 1;

    // __NR_perf_event_open == 298 (on x86_64)
    int fd = syscall(298, &pe, 0, -1, -1, 0);
    if (fd != -1) {
        struct perf_event_mmap_page* pc = (struct perf_event_mmap_page*)mmap(
            NULL, 4096, PROT_READ, MAP_SHARED, fd, 0
        );
        if (pc) {
            // success
            if (pc->cap_user_time == 1) {
                // docs say nanoseconds = (tsc * time_mult) >> time_shift
                //      set nanoseconds = 1000000000 = 1 second in nanoseconds, solve for tsc
                //       =>         tsc = 1000000000 / (time_mult >> time_shift)
#ifdef __SIZEOF_INT128__
                tsc_freq = ((__uint128_t)1'000'000'000ull << (__uint128_t)pc->time_shift) / pc->time_mult;
#else
                tsc_freq = (1'000'000'000ull << (pc->time_shift / 2))
                         / (pc->time_mult >> (pc->time_shift - pc->time_shift / 2));
#endif
            }
            munmap(pc, 4096);
        }
        close(fd);
    }

    // Slow path
    if (!tsc_freq) {
        // Get time before sleep
        uint64_t nsc_begin = 0;
        {
            struct timespec t;
            if (!clock_gettime(CLOCK_MONOTONIC_RAW, &t)) {
                nsc_begin = (uint64_t)t.tv_sec * 1'000'000'000ull + t.tv_nsec;
            }
        }
        uint64_t tsc_begin = __rdtsc();

        // 10ms gives ~4.5 digits of precision - the longer you sleep, the more precise you get
        usleep(10000);

        // Get time after sleep
        uint64_t nsc_end = nsc_begin + 1;
        {
            struct timespec t;
            if (!clock_gettime(CLOCK_MONOTONIC_RAW, &t)) {
                nsc_end = (uint64_t)t.tv_sec * 1'000'000'000ull + t.tv_nsec;
            }
        }
        uint64_t tsc_end = __rdtsc();

        // Do the math to extrapolate the RDTSC ticks elapsed in 1 second
        tsc_freq = (tsc_end - tsc_begin) * 1'000'000'000 / (nsc_end - nsc_begin);
    }

    // Failure case
    if (!tsc_freq) {
        tsc_freq = 1'000'000'000;
    }

    return tsc_freq;
}

#endif
