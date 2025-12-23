/**
 * @author (c) Eyal Rozenberg <eyalroz1@gmx.com>
 *             2021-2024, Haifa, Palestine/Israel
 * @author (c) Marco Paland (info@paland.com)
 *             2014-2019, PALANDesign Hannover, Germany
 *
 * @note Others have made smaller contributions to this file: see the
 * contributors page at https://github.com/eyalroz/printf/graphs/contributors
 * or ask one of the authors. The original code for exponential specifiers was
 * contributed by Martijn Jasperse <m.jasperse@gmail.com>.
 *
 * @brief Small stand-alone implementation of the printf family of functions
 * (`(v)printf`, `(v)s(n)printf` etc., geared towards use on embedded systems with
 * limited resources.
 *
 * @note the implementations are thread-safe; re-entrant; use no functions from
 * the standard library; and do not dynamically allocate any memory.
 *
 * @license The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "printf.h"

#include <stdint.h>
#include <limits.h>
#include <stdbool.h>

#if !(defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L))
/* C90 */
#if defined(_MSC_VER)
#define inline __inline
#else
#define inline __inline__
#endif /* defined(_MSC_VER) */
#endif /* !(defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)) */


/*
 * 'ntoa' conversion buffer size, this must be big enough to hold one converted
 * numeric number including padded zeros (dynamically created on stack)
 */
#ifndef PRINTF_INTEGER_BUFFER_SIZE
#define PRINTF_INTEGER_BUFFER_SIZE    64
#endif /* PRINTF_INTEGER_BUFFER_SIZE */

/*===========================================================================*/

/* The following will convert the number-of-digits into an exponential-notation literal */
#define PRINTF_CONCATENATE(s1, s2) s1##s2
#define PRINTF_EXPAND_THEN_CONCATENATE(s1, s2) PRINTF_CONCATENATE(s1, s2)
#define PRINTF_FLOAT_NOTATION_THRESHOLD ((floating_point_t) PRINTF_EXPAND_THEN_CONCATENATE(1e,PRINTF_MAX_INTEGRAL_DIGITS_FOR_DECIMAL))

/* internal flag definitions */
#define FLAGS_ZEROPAD     (1U <<  0U)
#define FLAGS_LEFT        (1U <<  1U)
#define FLAGS_PLUS        (1U <<  2U)
#define FLAGS_SPACE       (1U <<  3U)
#define FLAGS_HASH        (1U <<  4U)
#define FLAGS_UPPERCASE   (1U <<  5U)
#define FLAGS_CHAR        (1U <<  6U)
#define FLAGS_SHORT       (1U <<  7U)
#define FLAGS_INT         (1U <<  8U)
#define FLAGS_LONG        (1U <<  9U)
#define FLAGS_LONG_LONG   (1U << 10U)
#define FLAGS_PRECISION   (1U << 11U)
#define FLAGS_ADAPT_EXP   (1U << 12U)
#define FLAGS_POINTER     (1U << 13U)
  /* Note: Similar, but not identical, effect as FLAGS_HASH */
#define FLAGS_SIGNED      (1U << 14U)
#define FLAGS_LONG_DOUBLE (1U << 15U)

#define FLAGS_INT8 FLAGS_CHAR


#if   (SHRT_MAX   == 32767LL)
#define FLAGS_INT16       FLAGS_SHORT
#elif (INT_MAX    == 32767LL)
#define FLAGS_INT16       FLAGS_INT
#elif (LONG_MAX   == 32767LL)
#define FLAGS_INT16       FLAGS_LONG
#elif (LLONG_MAX  == 32767LL)
#define FLAGS_INT16       FLAGS_LONG_LONG
#else
#error "No basic integer type has a size of 16 bits exactly"
#endif

#if   (SHRT_MAX   == 2147483647LL)
#define FLAGS_INT32       FLAGS_SHORT
#elif (INT_MAX    == 2147483647LL)
#define FLAGS_INT32       FLAGS_INT
#elif (LONG_MAX   == 2147483647LL)
#define FLAGS_INT32       FLAGS_LONG
#elif (LLONG_MAX  == 2147483647LL)
#define FLAGS_INT32       FLAGS_LONG_LONG
#else
#error "No basic integer type has a size of 32 bits exactly"
#endif

#if   (SHRT_MAX   == 9223372036854775807LL)
#define FLAGS_INT64       FLAGS_SHORT
#elif (INT_MAX    == 9223372036854775807LL)
#define FLAGS_INT64       FLAGS_INT
#elif (LONG_MAX   == 9223372036854775807LL)
#define FLAGS_INT64       FLAGS_LONG
#elif (LLONG_MAX  == 9223372036854775807LL)
#define FLAGS_INT64       FLAGS_LONG_LONG
#else
#error "No basic integer type has a size of 64 bits exactly"
#endif


typedef unsigned int printf_flags_t;

#define BASE_BINARY    2
#define BASE_OCTAL     8
#define BASE_DECIMAL  10
#define BASE_HEX      16

typedef uint8_t numeric_base_t;

typedef unsigned long long printf_unsigned_value_t;
typedef long long          printf_signed_value_t;

/*
 * The printf()-family functions return an `int`; it is therefore
 * unnecessary/inappropriate to use size_t - often larger than int
 * in practice - for non-negative related values, such as widths,
 * precisions, offsets into buffers used for printing and the sizes
 * of these buffers. instead, we use:
 */
typedef unsigned int printf_size_t;
#define PRINTF_MAX_POSSIBLE_BUFFER_SIZE INT_MAX
  /*
   * If we were to nitpick, this would actually be INT_MAX + 1,
   * since INT_MAX is the maximum return value, which excludes the
   * trailing '\0'.
   */

#define SIGN(_negative,_x) ( (_negative) ? -(_x) : (_x))

/*
 * Note in particular the behavior here on LONG_MIN or LLONG_MIN; it is valid
 * and well-defined, but if you're not careful you can easily trigger undefined
 * behavior with -LONG_MIN or -LLONG_MIN
 */
#define ABS_FOR_PRINTING(_x) ((printf_unsigned_value_t) ( (_x) > 0 ? (_x) : -((printf_signed_value_t)_x) ))

/*
 * wrapper (used as buffer) for output function type
 *
 * One of the following must hold:
 * 1. max_chars is 0
 * 2. buffer is non-null
 * 3. function is non-null
 *
 * ... otherwise bad things will happen.
 */
typedef struct {
  void (*function)(char c, void* extra_arg);
  void* extra_function_arg;
  char* buffer;
  printf_size_t pos;
  printf_size_t max_chars;
} output_gadget_t;

/*
 * Note: This function currently assumes it is not passed a '\0' c,
 * or alternatively, that '\0' can be passed to the function in the output
 * gadget. The former assumption holds within the printf library. It also
 * assumes that the output gadget has been properly initialized.
 */
static inline void putchar_via_gadget(output_gadget_t* gadget, char c)
{
  printf_size_t write_pos = gadget->pos++;
    /*
     * We're _always_ increasing pos, so as to count how may characters
     * _would_ have been written if not for the max_chars limitation
     */
  if (write_pos >= gadget->max_chars) {
    return;
  }
  if (gadget->function != NULL) {
    /* No check for c == '\0' . */
    gadget->function(c, gadget->extra_function_arg);
  }
  else {
    /*
     * it must be the case that gadget->buffer != NULL , due to the constraint
     * on output_gadget_t ; and note we're relying on write_pos being non-negative.
     */
    gadget->buffer[write_pos] = c;
  }
}

/* Possibly-write the string-terminating '\0' character */
static inline void append_termination_with_gadget(output_gadget_t* gadget)
{
  printf_size_t null_char_pos;
  if (gadget->function != NULL || gadget->max_chars == 0) {
    return;
  }
  if (gadget->buffer == NULL) {
    return;
  }
  null_char_pos = gadget->pos < gadget->max_chars ? gadget->pos : gadget->max_chars - 1;
  gadget->buffer[null_char_pos] = '\0';
}

/*
 * We can't use putchar_ as is, since our output gadget
 * only takes pointers to functions with an extra argument
 */
static inline void putchar_wrapper(char c, void* unused)
{
  (void) unused;
  putchar_(c);
}

static inline output_gadget_t discarding_gadget(void)
{
  output_gadget_t gadget;
  gadget.function = NULL;
  gadget.extra_function_arg = NULL;
  gadget.buffer = NULL;
  gadget.pos = 0;
  gadget.max_chars = 0;
  return gadget;
}

static inline output_gadget_t buffer_gadget(char* buffer, size_t buffer_size)
{
  printf_size_t usable_buffer_size = (buffer_size > PRINTF_MAX_POSSIBLE_BUFFER_SIZE) ?
    PRINTF_MAX_POSSIBLE_BUFFER_SIZE : (printf_size_t) buffer_size;
  output_gadget_t result = discarding_gadget();
  if (buffer != NULL) {
    result.buffer = buffer;
    result.max_chars = usable_buffer_size;
  }
  return result;
}

static inline output_gadget_t function_gadget(void (*function)(char, void*), void* extra_arg)
{
  output_gadget_t result = discarding_gadget();
  result.function = function;
  result.extra_function_arg = extra_arg;
  result.max_chars = PRINTF_MAX_POSSIBLE_BUFFER_SIZE;
  return result;
}

static inline output_gadget_t extern_putchar_gadget(void)
{
  return function_gadget(putchar_wrapper, NULL);
}

/*
 * internal secure strlen
 * @return The length of the string (excluding the terminating 0) limited by 'maxsize'
 * @note strlen uses size_t, but wes only use this function with printf_size_t
 * variables - hence the signature.
 */
static inline printf_size_t strnlen_s_(const char* str, printf_size_t maxsize)
{
  const char* s;
  for (s = str; *s && maxsize--; ++s);
  return (printf_size_t)(s - str);
}


/*
 * internal test if char is a digit (0-9)
 * @return true if char is a digit
 */
static inline bool is_digit_(char ch)
{
  return (ch >= '0') && (ch <= '9');
}


/* internal ASCII string to printf_size_t conversion */
static printf_size_t atou_(const char** str)
{
  printf_size_t i = 0U;
  while (is_digit_(**str)) {
    i = i * 10U + (printf_size_t)(*((*str)++) - '0');
  }
  return i;
}


/* output the specified string in reverse, taking care of any zero-padding */
static void out_rev_(output_gadget_t* output, const char* buf, printf_size_t len, printf_size_t width, printf_flags_t flags)
{
  const printf_size_t start_pos = output->pos;

  /* pad spaces up to given width */
  if (!(flags & FLAGS_LEFT) && !(flags & FLAGS_ZEROPAD)) {
    printf_size_t i;
    for (i = len; i < width; i++) {
      putchar_via_gadget(output, ' ');
    }
  }

  /* reverse string */
  while (len) {
    putchar_via_gadget(output, buf[--len]);
  }

  /* append pad spaces up to given width */
  if (flags & FLAGS_LEFT) {
    while (output->pos - start_pos < width) {
      putchar_via_gadget(output, ' ');
    }
  }
}


/*
 * Invoked by print_integer after the actual number has been printed, performing necessary
 * work on the number's prefix (as the number is initially printed in reverse order)
 */
static void print_integer_finalization(output_gadget_t* output, char* buf, printf_size_t len, bool negative, numeric_base_t base, printf_size_t precision, printf_size_t width, printf_flags_t flags)
{
  printf_size_t unpadded_len = len;

  /* pad with leading zeros */
  {
    if (!(flags & FLAGS_LEFT)) {
      if (width && (flags & FLAGS_ZEROPAD) && (negative || (flags & (FLAGS_PLUS | FLAGS_SPACE)))) {
        width--;
      }
      while ((flags & FLAGS_ZEROPAD) && (len < width) && (len < PRINTF_INTEGER_BUFFER_SIZE)) {
        buf[len++] = '0';
      }
    }

    while ((len < precision) && (len < PRINTF_INTEGER_BUFFER_SIZE)) {
      buf[len++] = '0';
    }

    if (base == BASE_OCTAL && (len > unpadded_len)) {
      /* Since we've written some zeros, we've satisfied the alternative format leading space requirement */
      flags &= ~FLAGS_HASH;
    }
  }

  /* handle hash */
  if (flags & (FLAGS_HASH | FLAGS_POINTER)) {
    if (!(flags & FLAGS_PRECISION) && len && ((len == precision) || (len == width))) {
      /*
       * Let's take back some padding digits to fit in what will eventually
       * be the format-specific prefix
       */
      if (unpadded_len < len) {
        len--; /* This should suffice for BASE_OCTAL */
      }
      if (len && (base == BASE_HEX || base == BASE_BINARY) && (unpadded_len < len)) {
        len--; /* ... and an extra one for 0x or 0b */
      }
    }
    if ((base == BASE_HEX) && !(flags & FLAGS_UPPERCASE) && (len < PRINTF_INTEGER_BUFFER_SIZE)) {
      buf[len++] = 'x';
    }
    else if ((base == BASE_HEX) && (flags & FLAGS_UPPERCASE) && (len < PRINTF_INTEGER_BUFFER_SIZE)) {
      buf[len++] = 'X';
    }
    else if ((base == BASE_BINARY) && (len < PRINTF_INTEGER_BUFFER_SIZE)) {
      buf[len++] = 'b';
    }
    if (len < PRINTF_INTEGER_BUFFER_SIZE) {
      buf[len++] = '0';
    }
  }

  if (len < PRINTF_INTEGER_BUFFER_SIZE) {
    if (negative) {
      buf[len++] = '-';
    }
    else if (flags & FLAGS_PLUS) {
      buf[len++] = '+';  /* ignore the space if the '+' exists */
    }
    else if (flags & FLAGS_SPACE) {
      buf[len++] = ' ';
    }
  }

  out_rev_(output, buf, len, width, flags);
}

/* An internal itoa-like function */
static void print_integer(output_gadget_t* output, printf_unsigned_value_t value, bool negative, numeric_base_t base, printf_size_t precision, printf_size_t width, printf_flags_t flags)
{
  char buf[PRINTF_INTEGER_BUFFER_SIZE];
  printf_size_t len = 0U;

  if (!value) {
    if ( !(flags & FLAGS_PRECISION) ) {
      buf[len++] = '0';
      flags &= ~FLAGS_HASH;
      /*
       * We drop this flag this since either the alternative and regular modes of the specifier
       * don't differ on 0 values, or (in the case of octal) we've already provided the special
       * handling for this mode.
       */
    }
    else if (base == BASE_HEX) {
      flags &= ~FLAGS_HASH;
      /*
       * We drop this flag this since either the alternative and regular modes of the specifier
       * don't differ on 0 values
       */
    }
  }
  else {
    do {
      const char digit = (char)(value % base);
      buf[len++] = (char)(digit < 10 ? '0' + digit : (flags & FLAGS_UPPERCASE ? 'A' : 'a') + digit - 10);
      value /= base;
    } while (value && (len < PRINTF_INTEGER_BUFFER_SIZE));
  }

  print_integer_finalization(output, buf, len, negative, base, precision, width, flags);
}

/*
 * Advances the format pointer past the flags, and returns the parsed flags
 * due to the characters passed
 */
static printf_flags_t parse_flags(const char** format)
{
  printf_flags_t flags = 0U;
  do {
    switch (**format) {
      case '0': flags |= FLAGS_ZEROPAD; (*format)++; break;
      case '-': flags |= FLAGS_LEFT;    (*format)++; break;
      case '+': flags |= FLAGS_PLUS;    (*format)++; break;
      case ' ': flags |= FLAGS_SPACE;   (*format)++; break;
      case '#': flags |= FLAGS_HASH;    (*format)++; break;
      default : return flags;
    }
  } while (true);
}

static inline void format_string_loop(output_gadget_t* output, const char* format, va_list args)
{
#define ADVANCE_IN_FORMAT_STRING(cptr_) do { (cptr_)++; if (!*(cptr_)) return; } while(0)

  while (*format)
  {
    printf_flags_t flags;
    printf_size_t width;
    printf_size_t precision;
    if (*format != '%') {
      /* A regular content character */
      putchar_via_gadget(output, *format);
      format++;
      continue;
    }
    /* We're parsing a format specifier: %[flags][width][.precision][length] */
    ADVANCE_IN_FORMAT_STRING(format);

    flags = parse_flags(&format);

    /* evaluate width field */
    width = 0U;
    if (is_digit_(*format)) {
      /*
       * Note: If the width is negative, we've already parsed its
       * sign character '-' as a FLAG_LEFT
       */
      width = (printf_size_t) atou_(&format);
    }
    else if (*format == '*') {
      const int w = va_arg(args, int);
      if (w < 0) {
        flags |= FLAGS_LEFT;    /* reverse padding */
        width = (printf_size_t)-w;
      }
      else {
        width = (printf_size_t)w;
      }
      ADVANCE_IN_FORMAT_STRING(format);
    }

    /* evaluate precision field */
    precision = 0U;
    if (*format == '.') {
      flags |= FLAGS_PRECISION;
      ADVANCE_IN_FORMAT_STRING(format);
      if (*format == '-') {
        do {
          ADVANCE_IN_FORMAT_STRING(format);
        } while (is_digit_(*format));
        flags &= ~FLAGS_PRECISION;
      }
      else if (is_digit_(*format)) {
        precision = atou_(&format);
      }
      else if (*format == '*') {
        const int precision_ = va_arg(args, int);
        if (precision_ < 0) {
          flags &= ~FLAGS_PRECISION;
        }
        else {
          precision = precision_ > 0 ? (printf_size_t) precision_ : 0U;
        }
        ADVANCE_IN_FORMAT_STRING(format);
      }
    }

    /* evaluate length field */
    switch (*format) {
      case 'I' : {
        ADVANCE_IN_FORMAT_STRING(format);
        /* Greedily parse for size in bits: 8, 16, 32 or 64 */
        switch(*format) {
          case '8':               flags |= FLAGS_INT8;
            ADVANCE_IN_FORMAT_STRING(format);
            break;
          case '1':
            ADVANCE_IN_FORMAT_STRING(format);
          if (*format == '6') { format++; flags |= FLAGS_INT16; }
            break;
          case '3':
            ADVANCE_IN_FORMAT_STRING(format);
            if (*format == '2') { ADVANCE_IN_FORMAT_STRING(format); flags |= FLAGS_INT32; }
            break;
          case '6':
            ADVANCE_IN_FORMAT_STRING(format);
            if (*format == '4') { ADVANCE_IN_FORMAT_STRING(format); flags |= FLAGS_INT64; }
            break;
          default: break;
        }
        break;
      }
      case 'l' :
        flags |= FLAGS_LONG;
        ADVANCE_IN_FORMAT_STRING(format);
        if (*format == 'l') {
          flags |= FLAGS_LONG_LONG;
          ADVANCE_IN_FORMAT_STRING(format);
        }
        break;
      case 'L' :
        flags |= FLAGS_LONG_DOUBLE;
        ADVANCE_IN_FORMAT_STRING(format);
        break;
      case 'h' :
        flags |= FLAGS_SHORT;
        ADVANCE_IN_FORMAT_STRING(format);
        if (*format == 'h') {
          flags |= FLAGS_CHAR;
          ADVANCE_IN_FORMAT_STRING(format);
        }
        break;
      case 't' :
        flags |= (sizeof(ptrdiff_t) <= sizeof(int) ) ? FLAGS_INT : (sizeof(ptrdiff_t) == sizeof(long)) ? FLAGS_LONG : FLAGS_LONG_LONG;
        ADVANCE_IN_FORMAT_STRING(format);
        break;
      case 'j' :
        flags |= (sizeof(intmax_t) == sizeof(long) ? FLAGS_LONG : FLAGS_LONG_LONG);
        ADVANCE_IN_FORMAT_STRING(format);
        break;
      case 'z' :
        flags |= (sizeof(size_t) <= sizeof(int) ) ? FLAGS_INT : (sizeof(size_t) == sizeof(long)) ? FLAGS_LONG : FLAGS_LONG_LONG;
        ADVANCE_IN_FORMAT_STRING(format);
        break;
      default:
        break;
    }

    /* evaluate specifier */
    switch (*format) {
      case 'd' :
      case 'i' :
      case 'u' :
      case 'x' :
      case 'X' :
      case 'o' :
      case 'b' : {
        numeric_base_t base;

        if (*format == 'd' || *format == 'i') {
          flags |= FLAGS_SIGNED;
        }

        if (*format == 'x' || *format == 'X') {
          base = BASE_HEX;
        }
        else if (*format == 'o') {
          base =  BASE_OCTAL;
        }
        else if (*format == 'b') {
          base =  BASE_BINARY;
        }
        else {
          base = BASE_DECIMAL;
          flags &= ~FLAGS_HASH; /* decimal integers have no alternative presentation */
        }

        if (*format == 'X') {
          flags |= FLAGS_UPPERCASE;
        }

        format++;
        /* ignore '0' flag when precision is given */
        if (flags & FLAGS_PRECISION) {
          flags &= ~FLAGS_ZEROPAD;
        }

        if (flags & FLAGS_SIGNED) {
          /* A signed specifier: d, i or possibly I + bit size if enabled */

          if (flags & FLAGS_LONG_LONG) {
            const long long value = va_arg(args, long long);
            print_integer(output, ABS_FOR_PRINTING(value), value < 0, base, precision, width, flags);
          }
          else if (flags & FLAGS_LONG) {
            const long value = va_arg(args, long);
            print_integer(output, ABS_FOR_PRINTING(value), value < 0, base, precision, width, flags);
          }
          else {
            /*
             * We never try to interpret the argument as something potentially-smaller than int,
             * due to integer promotion rules: Even if the user passed a short int, short unsigned
             * etc. - these will come in after promotion, as int's (or unsigned for the case of
             * short unsigned when it has the same size as int)
             */
            const int value =
              (flags & FLAGS_CHAR) ? (signed char) va_arg(args, int) :
              (flags & FLAGS_SHORT) ? (short int) va_arg(args, int) :
              va_arg(args, int);
            print_integer(output, ABS_FOR_PRINTING(value), value < 0, base, precision, width, flags);
          }
        }
        else {
          /* An unsigned specifier: u, x, X, o, b */

          flags &= ~(FLAGS_PLUS | FLAGS_SPACE);

          if (flags & FLAGS_LONG_LONG) {
            print_integer(output, (printf_unsigned_value_t) va_arg(args, unsigned long long), false, base, precision, width, flags);
          }
          else if (flags & FLAGS_LONG) {
            print_integer(output, (printf_unsigned_value_t) va_arg(args, unsigned long), false, base, precision, width, flags);
          }
          else {
            const unsigned int value =
              (flags & FLAGS_CHAR) ? (unsigned char)va_arg(args, unsigned int) :
              (flags & FLAGS_SHORT) ? (unsigned short int)va_arg(args, unsigned int) :
              va_arg(args, unsigned int);
            print_integer(output, (printf_unsigned_value_t) value, false, base, precision, width, flags);
          }
        }
        break;
      }
      case 'c' : {
        printf_size_t l = 1U;
        /* pre padding */
        if (!(flags & FLAGS_LEFT)) {
          while (l++ < width) {
            putchar_via_gadget(output, ' ');
          }
        }
        /* char output */
        putchar_via_gadget(output, (char) va_arg(args, int) );
        /* post padding */
        if (flags & FLAGS_LEFT) {
          while (l++ < width) {
            putchar_via_gadget(output, ' ');
          }
        }
        format++;
        break;
      }

      case 's' : {
        const char* p = va_arg(args, char*);
        if (p == NULL) {
          out_rev_(output, ")llun(", 6, width, flags);
        }
        else {
          printf_size_t l = strnlen_s_(p, precision ? precision : PRINTF_MAX_POSSIBLE_BUFFER_SIZE);
          /* pre padding */
          if (flags & FLAGS_PRECISION) {
            l = (l < precision ? l : precision);
          }
          if (!(flags & FLAGS_LEFT)) {
            while (l++ < width) {
              putchar_via_gadget(output, ' ');
            }
          }
          /* string output */
          while ((*p != 0) && (!(flags & FLAGS_PRECISION) || precision)) {
            putchar_via_gadget(output, *(p++));
            --precision;
          }
          /* post padding */
          if (flags & FLAGS_LEFT) {
            while (l++ < width) {
              putchar_via_gadget(output, ' ');
            }
          }
        }
        format++;
        break;
      }

      case 'p' : {
        uintptr_t value;
        width = sizeof(void*) * 2U + 2; /* 2 hex chars per byte + the "0x" prefix */
        flags |= FLAGS_ZEROPAD | FLAGS_POINTER;
        value = (uintptr_t)va_arg(args, void*);
        (value == (uintptr_t) NULL) ?
          out_rev_(output, ")lin(", 5, width, flags) :
          print_integer(output, (printf_unsigned_value_t) value, false, BASE_HEX, precision, width, flags);
        format++;
        break;
      }

      case '%' :
        putchar_via_gadget(output, '%');
        format++;
        break;

      default :
        putchar_via_gadget(output, *format);
        format++;
        break;
    }
  }
}

/* internal vsnprintf - used for implementing _all library functions */
static int vsnprintf_impl(output_gadget_t* output, const char* format, va_list args)
{
  /*
   * Note: The library only calls vsnprintf_impl() with output->pos being 0. However, it is
   * possible to call this function with a non-zero pos value for some "remedial printing".
   */
  format_string_loop(output, format, args);

  /* termination */
  append_termination_with_gadget(output);

  /* return written chars without terminating \0 */
  return (int)output->pos;
}

/*===========================================================================*/

int vprintf_(const char* format, va_list arg)
{
  output_gadget_t gadget = extern_putchar_gadget();
  return vsnprintf_impl(&gadget, format, arg);
}

int vsnprintf_(char* s, size_t n, const char* format, va_list arg)
{
  output_gadget_t gadget = buffer_gadget(s, n);
  return vsnprintf_impl(&gadget, format, arg);
}

int vsprintf_(char* s, const char* format, va_list arg)
{
  return vsnprintf_(s, PRINTF_MAX_POSSIBLE_BUFFER_SIZE, format, arg);
}

int vfctprintf(void (*out)(char c, void* extra_arg), void* extra_arg, const char* format, va_list arg)
{
  output_gadget_t gadget;
  if (out == NULL) { return 0; }
  gadget = function_gadget(out, extra_arg);
  return vsnprintf_impl(&gadget, format, arg);
}

int printf_(const char* format, ...)
{
  int ret;
  va_list args;
  va_start(args, format);
  ret = vprintf_(format, args);
  va_end(args);
  return ret;
}

int sprintf_(char* s, const char* format, ...)
{
  int ret;
  va_list args;
  va_start(args, format);
  ret = vsprintf_(s, format, args);
  va_end(args);
  return ret;
}

int snprintf_(char* s, size_t n, const char* format, ...)
{
  int ret;
  va_list args;
  va_start(args, format);
  ret = vsnprintf_(s, n, format, args);
  va_end(args);
  return ret;
}

int fctprintf(void (*out)(char c, void* extra_arg), void* extra_arg, const char* format, ...)
{
  int ret;
  va_list args;
  va_start(args, format);
  ret = vfctprintf(out, extra_arg, format, args);
  va_end(args);
  return ret;
}
