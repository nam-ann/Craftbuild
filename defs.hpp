#pragma once

#ifdef _MSC_VER
#define NO_WARNING _Pragma("warning(push, 0)")
#endif

#if defined(__GNUC__) or defined(__clang__)
#define NO_WARNING \
        _Pragma("GCC diagnostic push") \
        _Pragma("GCC diagnostic ignored \"-Wconversion\"") \
        _Pragma("GCC diagnostic ignored \"-Wfloat-conversion\"") \
        _Pragma("GCC diagnostic ignored \"-Wnarrowing\"") \
        _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"") \
        _Pragma("GCC diagnostic ignored \"-Wexpose-global-module-tu-local\"")
#endif

#ifdef _MSC_VER
#define DO_WARNING _Pragma("warning(pop)")
#endif

#if defined(__GNUC__) or defined(__clang__)
#define DO_WARNING _Pragma("GCC diagnostic pop")
#endif