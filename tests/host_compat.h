#ifndef HOST_COMPAT_H
#define HOST_COMPAT_H

/* TinyCC 0.9.27 checks C11 assertions through a negative-size typedef. */
#ifdef __TINYC__
#define HOST_JOIN_INNER(a, b) a##b
#define HOST_JOIN(a, b) HOST_JOIN_INNER(a, b)
#define _Static_assert(condition, message) \
    typedef char HOST_JOIN(host_static_assert_, __LINE__)[(condition) ? 1 : -1]
#endif

#endif
