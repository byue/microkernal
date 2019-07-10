#include <cdefs.h>
#include <stat.h>
#include <stdarg.h>
#include <user.h>
#include <syscall_message.h>

static void aputc(int fd, char c) { 
  struct arg args[4];

  // num args not including this
  args[0].arg_type = INT_TYPE;
  args[0].arg_val.i = 3;

  args[1].arg_type = INT_TYPE;
  args[1].arg_val.i = fd;

  args[2].arg_type = CHAR_TYPE;
  args[2].arg_val.c = c;

  args[3].arg_type = INT_TYPE;
  args[3].arg_val.i = 1;

  app_syscall(MESSAGE_WRITE, (void *) &args);
}


static void aprintint64(int fd, int xx, int base, int sgn) {
  static char digits[] = "0123456789abcdef";
  char buf[32];
  int i;
  uint64_t x;

  if (sgn && (sgn = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sgn)
    buf[i++] = '-';

  while (--i >= 0)
    aputc(fd, buf[i]);
}

static void aprintint(int fd, int xx, int base, int sgn) {
  static char digits[] = "0123456789ABCDEF";
  char buf[16];
  int i, neg;
  uint x;

  neg = 0;
  if (sgn && xx < 0) {
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);
  if (neg)
    buf[i++] = '-';

  while (--i >= 0)
    aputc(fd, buf[i]);
}

// Print to the given fd. Only understands %d, %x, %p, %s.
void aprintf(int fd, char *fmt, ...) {
  char *s;
  int c, i, state;
  int lflag;
  va_list valist;
  va_start(valist, fmt);

  state = 0;
  for (i = 0; fmt[i]; i++) {
    c = fmt[i] & 0xff;
    if (state == 0) {
      if (c == '%') {
        state = '%';
        lflag = 0;
      } else {
        aputc(fd, c);
      }
    } else if (state == '%') {
      if (c == 'l') {
        lflag = 1;
        continue;
      } else if (c == 'd') {
        if (lflag == 1)
          aprintint64(fd, va_arg(valist, int64_t), 10, 1);
        else
          aprintint(fd, va_arg(valist, int), 10, 1);
      } else if (c == 'x' || c == 'p') {
        if (lflag == 1)
          aprintint64(fd, va_arg(valist, int64_t), 16, 0);
        else
          aprintint(fd, va_arg(valist, int), 16, 0);
      } else if (c == 's') {
        if ((s = (char *)va_arg(valist, char *)) == 0)
          s = "(null)";
        for (; *s; s++)
          aputc(fd, *s);
      } else if (c == '%') {
        aputc(fd, c);
      } else {
        // Unknown % sequence.  Print it to draw attention.
        aputc(fd, '%');
        aputc(fd, c);
      }
      state = 0;
    }
  }

  va_end(valist);
}
