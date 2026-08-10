#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(min)
#    undef min
#  endif
#  if defined(max)
#    undef max
#  endif
#  if defined(HTTPCLIENT_STATIC_DEFINE)
#    define HTTPCLIENT_API
#  elif defined(HTTPCLIENT_EXPORTS)
#    define HTTPCLIENT_API __declspec(dllexport)
#  else
#    define HTTPCLIENT_API __declspec(dllimport)
#  endif
#  define HTTPCLIENT_LOCAL
#else
#  if __GNUC__ >= 4
#    define HTTPCLIENT_API __attribute__((visibility("default")))
#    define HTTPCLIENT_LOCAL __attribute__((visibility("hidden")))
#  else
#    define HTTPCLIENT_API
#    define HTTPCLIENT_LOCAL
#  endif
#endif
