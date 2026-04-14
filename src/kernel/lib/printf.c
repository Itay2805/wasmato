#include "printf.h"
#include "lib/string.h"

static size_t strlen(const char* str) {
	size_t len;
	for (len = 0; str[len]; len++);
	return len;
}

static int u64toa_r(uint64_t in, char* buffer) {
	int digits = 0;

	int pos = 19;
	do {
		int dig = 0;
		unsigned long long lim = 0;
		for (dig = 0, lim = 1; dig < pos; dig++) {
			lim *= 10;
		}

		if (digits || in >= lim || !pos) {
			for (dig = 0; in >= lim; dig++) {
				in -= lim;
			}

			buffer[digits++] = '0' + dig;
		}
	} while (pos--);

	buffer[digits] = 0;
	return digits;
}

static int i64toa_r(int64_t in, char* buffer) {
	char* ptr = buffer;
	int len = 0;

	if (in < 0) {
		in = -(uint64_t)in;
		*(ptr++) = '-';
		len++;
	}
	len += u64toa_r(in, ptr);
	return len;
}


static int u64toh_r(uint64_t in, char* buffer) {
	signed char pos = 60;

	int digits = 0;
	do {
		int dig = (in >> pos) & 0xF;
		if (dig > 9)
			dig += 'a' - '0' - 10;
		pos -= 4;
		if (dig || digits || pos < 0) {
			buffer[digits++] = '0' + dig;
		}
	} while (pos >= 0);

	buffer[digits] = 0;
	return digits;
}

int vcprintf(printf_cb_t cb, void* user, size_t n, const char *fmt, va_list args) {
	char c;
	unsigned long long v;
	size_t len;
	char tmpbuf[21];
	const char* outstr;

	size_t offset = 0;
	size_t lpref = 0;
	int written = 0;
	bool escape = false;

	while (1) {
		int width = 0;
		char pad = ' ';

		c = fmt[offset++];

		if (escape) {
			// we're in an escape sequence, offset == 1
			escape = false;

			// pad with zero instead of space
			if (c == '0') {
				pad = '0';
				c = fmt[offset++];
			}

			// width
			while (c >= '0' && c <= '9') {
				width *= 10;
				width += c - '0';

				c = fmt[offset++];
			}

			// modifiers or final 0
			while (c == 'l') {
				lpref++;
				c = fmt[offset++];
			}

			if (c == 'c' || c == 'd' || c == 'u' || c == 'x' || c == 'p') {
				char* out = tmpbuf;

				if (c == 'p') {
					v = va_arg(args, unsigned long);
				} else if (lpref) {
					if (lpref > 1) {
						v = va_arg(args, unsigned long long);
					} else {
						v = va_arg(args, unsigned long);
					}
				} else {
					v = va_arg(args, unsigned int);
				}

				if (c == 'd') {
					// sign-extend the value
					if (lpref == 0) {
						v = (long long)(int)v;
					} else if (lpref == 1) {
						v = (long long)(long)v;
					}
				}

				switch (c) {
				case 'c':
					out[0] = v;
					out[1] = 0;
					break;
				case 'd':
					i64toa_r(v, out);
					break;
				case 'u':
					u64toa_r(v, out);
					break;
				case 'p':
					*(out++) = '0';
					*(out++) = 'x';
				default: // 'x' and 'p' above
					u64toh_r(v, out);
					break;
				}
				outstr = tmpbuf;

			} else if (c == 's') {
				outstr = va_arg(args, char *);
				if (outstr == nullptr) {
					outstr = "(null)";
				}

			} else if (c == '%') {
				// queue it verbatim
				continue;

			} else {
				escape = true;
				goto do_escape;
			}

			len = strlen(outstr);
			goto flush_str;
		}

		// not an escape sequence
		if (c == 0 || c == '%') {
			// flush pending data on escape or end
			escape = true;
			lpref = 0;
			outstr = fmt;
			len = offset - 1;

		flush_str:
			if (n) {
				size_t w = len < n ? len : n;
				n -= w;

				while (width-- > w) {
					if (cb(user, &pad, 1) != 0) {
						return -1;
					}
					written += 1;
				}

				if (cb(user, outstr, w) != 0) {
					return -1;
				}
			}

			written += len;

		do_escape:
			if (c == 0)
				break;

			fmt += offset;
			offset = 0;
			continue;
		}

		// literal char, just queue it
	}

	return written;
}

static int sprintf_cb(void* user, const char* buf, size_t size) {
	char** state = (char**)user;
	memcpy(*state, buf, size);
	*state += size;
	return 0;
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
	char* state = buf;

	int ret = vcprintf(sprintf_cb, &state, size, fmt, args);
	if (ret < 0)
		return ret;

	buf[(size_t)ret < size ? (size_t)ret : size - 1] = '\0';
	return ret;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
	va_list args = {};
	va_start(args, fmt);
	int ret = vsnprintf(buf, size, fmt, args);
	va_end(args);
	return ret;
}
