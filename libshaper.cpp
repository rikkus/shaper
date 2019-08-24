/*
  vim:tabstop=2:shiftwidth=2:expandtab:cinoptions=(s,U1,m1

  Copyright (C) 2001 Rik Hemsley (rikkus) <rik@kde.org>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to
  deal in the Software without restriction, including without limitation the
  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
  sell copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <dlfcn.h>        // For dlopen() and dlsym().
#include <sys/socket.h>   // For socket(2).
#include <unistd.h>       // For usleep(3).
#include <sys/time.h>     // For gettimeofday(3).
#include <algorithm>
#include <list>

static const unsigned int shaper_debug_none           = 0;
static const unsigned int shaper_debug_errors_only    = 1;
static const unsigned int shaper_debug_basic          = 2;
static const unsigned int shaper_debug_full           = 3;

static const unsigned int shaper_default_byte_limit   = 1024;   // per second.
static const unsigned int shaper_default_interval     = 100000; // us.
static const unsigned int shaper_default_debug_level  = shaper_debug_none;

static std::list<int> * shaper_fd_list  = 0L;

static unsigned int shaper_byte_limit;
static unsigned int shaper_bytes_left_this_interval;
static unsigned int shaper_interval;
static unsigned int shaper_bytes_per_interval;
static unsigned int shaper_debug_level;

static int     (*shaper_original_socket) (int, int, int);
static ssize_t (*shaper_original_read)   (int, void *, size_t);
static int     (*shaper_original_close)  (int);

static timeval  shaper_last_read = {0, 0};

extern "C"
{
  int _init()
  {
    // XXX: What should retval be ?

    if (shaper_debug_level > shaper_debug_errors_only)
    {
      std::cerr << "shaper: init" << std::endl;
    }

    shaper_fd_list = new std::list<int>;

    void * libc_handle = ::dlopen("/lib/libc.so.6", RTLD_LAZY);

    if (0 == libc_handle)
    {
      if (shaper_debug_level > shaper_debug_none)
      {
        std::cerr << "Can't open libc" << std::endl;
      }
      return 0;
    }

    shaper_original_socket =
      (int (*)(int, int, int))
      ::dlsym(libc_handle, "socket");

    if (0 == shaper_original_socket)
    {
      if (shaper_debug_level > shaper_debug_none)
      {
        std::std::cerr << "Can't find socket()" << std::endl;
      }
      return 0;
    }

    shaper_original_read =
      (ssize_t (*)(int, void *, size_t))
      ::dlsym(libc_handle, "read");

    if (0 == shaper_original_read)
    {
      if (shaper_debug_level > shaper_debug_none)
      {
        std::std::cerr << "Can't find read()" << std::endl;
      }
      return 0;
    }

    shaper_original_close =
      (int (*)(int))
      ::dlsym(libc_handle, "close");

    if (0 == shaper_original_close)
    {
      if (shaper_debug_level > shaper_debug_none)
      {
        std::std::cerr << "Can't find close()" << std::endl;
      }
      return 0;
    }

    if (shaper_debug_level > shaper_debug_errors_only)
    {
      std::cerr << "shaper: init done" << std::endl;
    }

    if (-1 == ::gettimeofday(&shaper_last_read, 0))
    {
      if (shaper_debug_level > shaper_debug_none)
      {
        std::cerr
          << "shaper: gettimeofday failed"
          << std::endl;

        return 0;
      }
    }

    char * debug_env    = ::getenv("SHAPER_DEBUG_LEVEL");
    char * limit_env    = ::getenv("SHAPER_READ_LIMIT");
    char * interval_env = ::getenv("SHAPER_INTERVAL");

    if (0 != debug_env)
      shaper_debug_level = atoi(debug_env);
    else
      shaper_debug_level = shaper_default_debug_level;

    if (0 != limit_env)
      shaper_byte_limit = shaper_bytes_left_this_interval = atoi(limit_env);
    else
      shaper_byte_limit = shaper_default_byte_limit;

    if (0 != interval_env)
      shaper_interval = atoi(interval_env);
    else
      shaper_interval = shaper_default_interval;

    shaper_bytes_per_interval =
      shaper_byte_limit / (1000000 / shaper_interval);

    if (shaper_debug_level > shaper_debug_errors_only)
    {
      std::cerr << "shaper: byte_limit          == "
        << shaper_byte_limit
        << std::endl;

      std::cerr << "shaper: interval            == "
        << shaper_interval
        << " us"
        << std::endl;

      std::cerr << "shaper: bytes_per_interval  == "
        << shaper_bytes_per_interval
        << std::endl;
    }

    return 0;
  }

  int socket(int domain, int type, int protocol)
  {
    if (shaper_debug_level > shaper_debug_errors_only)
    {
      std::std::cerr
        << "shaper: socket "
        << domain
        << " "
        << type
        << " "
        << protocol
        << std::std::endl;
    }

    int fd = shaper_original_socket(domain, type, protocol);

    if (PF_INET == domain)
    {
      if (shaper_debug_level > shaper_debug_errors_only)
      {
        std::cerr << "shaper: it was a PF_INET socket" << std::endl;
      }

      shaper_fd_list->push_back(fd);
    }

    return fd;
  }

  int close(int fd)
  {
    if (shaper_debug_level > shaper_debug_errors_only)
    {
      std::cerr << "shaper: close " << fd << std::endl;
    }

    unsigned int l = shaper_fd_list->size();

    shaper_fd_list->remove(fd);

    if (shaper_fd_list->size() != l)
    {
      if (shaper_debug_level > shaper_debug_errors_only)
      {
        std::cerr << "shaper: removed fd from list" << std::endl;
      }
    }

    return shaper_original_close(fd);
  }

  ssize_t read(int fd, void * buf, size_t count)
  {
    if (shaper_debug_level > shaper_debug_basic)
    {
      std::cerr << "shaper: read " << fd << " (buf) " << count << std::endl;
    }

    std::list<int>::iterator it =
      std::find(shaper_fd_list->begin(), shaper_fd_list->end(), fd);

    if (it == shaper_fd_list->end())
    {
      if (shaper_debug_level > shaper_debug_basic)
      {
        std::cerr << "shaper: not in fd list" << std::endl;
      }
      return shaper_original_read(fd, buf, count);
    }
    else
    {
      if (shaper_debug_level > shaper_debug_errors_only)
      {
        std::cerr << "shaper: found in fd list" << std::endl;
      }
    }

    struct timeval current_time = {0, 0};

    if (-1 == ::gettimeofday(&current_time, 0))
    {
      if (shaper_debug_level > shaper_debug_none)
      {
        std::cerr
          << "shaper: gettimeofday failed"
          << std::endl;

        return shaper_original_read(fd, buf, count);
      }
    }

    unsigned long usec_from_last_read =
      (
        1000000 * (current_time.tv_sec   - shaper_last_read.tv_sec)
        +         (current_time.tv_usec  - shaper_last_read.tv_usec)
      );

    if (shaper_debug_level > shaper_debug_errors_only)
    {
      cerr << "shaper: usec_from_last_read == " << usec_from_last_read << endl;
    }

    if (usec_from_last_read >= shaper_interval)
    {
      if (shaper_debug_level > shaper_debug_errors_only)
      {
        std::cerr << "shaper: after end of interval. resetting limit." << endl;
      }

      shaper_bytes_left_this_interval = shaper_byte_limit;
      usec_from_last_read = 0;
    }

    unsigned long usec_until_interval_end =
      shaper_interval - usec_from_last_read;

    if (shaper_debug_level > shaper_debug_errors_only)
    {
      std::cerr
        << "shaper: usec_until_interval_end == "
        << usec_until_interval_end
        << std::endl;
      std::cerr
        << "shaper: % of interval left == "
        << usec_until_interval_end / float(shaper_interval) * 100
        << std::endl;
    }

    size_t adjusted_count = shaper_bytes_per_interval;

    if (shaper_debug_level > shaper_debug_errors_only)
    {
      std::cerr
        << "shaper: allowing " << adjusted_count << " bytes"
        << std::endl;
    }

    int bytes_read = shaper_original_read(fd, buf, adjusted_count);

    if (bytes_read > 0)
    {
      shaper_bytes_left_this_interval -= bytes_read;
    }

    unsigned int time_to_wait = usec_until_interval_end;

    if (shaper_debug_level > shaper_debug_errors_only)
    {
      std::cerr
        << "shaper: sleeping before return for " << time_to_wait / 1000 << "ms"
        << std::endl;
    }

    ::usleep(time_to_wait);

    if (-1 == ::gettimeofday(&shaper_last_read, 0))
    {
      if (shaper_debug_level > shaper_debug_none)
      {
        std::cerr
          << "shaper: gettimeofday failed"
          << std::endl;
      }
    }

    return bytes_read;
  }
}

