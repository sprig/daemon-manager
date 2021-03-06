//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org> -*- c++ -*-
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __STRPRINTF_H__
#define __STRPRINTF_H__

#include <string>

std::string strprintf(const char *format, ...)
    __attribute__ ((format (printf, 1, 2)));

// Shorthand for "throw strprintf(...)". It has a return type so you can do:
//     mkdir() == -1 && throwstr("mkdir: %s", strerror(errno));
bool throw_str(const char *format, ...)
    __attribute__ ((format (printf, 1, 2)));

// Same plus an implied +": "+strerror(errno)
bool throw_strerr(const char *format, ...)
    __attribute__ ((format (printf, 1, 2)));

#endif /* __STRPRINTF_H__ */

