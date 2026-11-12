/* ltoa / ultoa (déclarées par api/itoa.h ; newlib ne fournit que itoa et utoa). */
#include <stdint.h>

char *ultoa(unsigned long value, char *string, int radix)
{
  char tmp[33];
  int i = 0;
  if (radix < 2 || radix > 36) { string[0] = 0; return string; }
  do {
    int d = (int)(value % (unsigned long)radix);
    tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    value /= (unsigned long)radix;
  } while (value);
  int j = 0;
  while (i) string[j++] = tmp[--i];
  string[j] = 0;
  return string;
}

char *ltoa(long value, char *string, int radix)
{
  if (value < 0 && radix == 10) {
    string[0] = '-';
    ultoa((unsigned long)(-(value + 1)) + 1UL, string + 1, radix);
    return string;
  }
  return ultoa((unsigned long)value, string, radix);
}
