#ifndef DEBUG_LOG_HPP
#define DEBUG_LOG_HPP

#define DEBUG_LOG

#ifdef DEBUG_LOG
#include <iostream>
#endif

#ifdef DEBUG_LOG

inline void Log() {
    std::cerr << std::endl;
}

template <typename T, typename... Args>
inline void Log(T t, Args&&... args) {
    std::cerr << t;
    Log(args...);
}

#else

template <typename... Args>
inline void Log(Args&&... args) {
}

#endif

#ifdef NDEBUG
template <typename T, typename... Args>
inline void DebugLog(T, Args&&...) {
}
#else
template <typename T, typename... Args>
inline void DebugLog(T t, Args&&... args) {
    std::cerr << t;
    Log(args...);
}
#endif

#endif  // DEBUG_LOG_HPP
