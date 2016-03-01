/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

#include <include/configure.h>
#include <include/nf_macros.h>

///////////////////////////////////////////////////////////////////////////////
int bin2txt(FILE* fd, size_t len, const char* data) {
  FUSION_ENSURE(fd,   return -1);
  FUSION_ENSURE(data, return -1);

  int rc = 0;

  for (size_t i = 0; i < len && rc >= 0; ++i)
    if (::isalnum((unsigned char)data[i]))
      rc = ::fprintf(fd, "%c", data[i]);
    else
      rc = ::fprintf(fd, (i + 1 < len && ::isxdigit((unsigned char)data[i+1])) ? "\\x%0.2X" : "\\x%X", (unsigned char)data[i]);

  return rc >= 0;
}

///////////////////////////////////////////////////////////////////////////////
int bin2txt(size_t dlen, const char* data, size_t tmax, char* txt) {
  FUSION_ENSURE(data, return -1);
  FUSION_ENSURE(txt,  return -1);

  size_t tlen = 0;

  for (size_t i = 0; i < dlen && tlen < tmax; ++i) {
    int t;

    if (::isalnum((unsigned char)data[i]))
      t = ::_snprintf(txt + tlen, tmax - tlen, "%c", data[i]);
    else
      t = ::_snprintf(txt + tlen, tmax - tlen, (i + 1 < dlen && ::isxdigit((unsigned char)data[i+1])) ? "\\x%0.2X" : "\\x%X", (unsigned char)data[i]);

    if (0 < t && t < (tmax - tlen))
      tlen += t;
    else
      return -1;
  }

  return tlen;
}

///////////////////////////////////////////////////////////////////////////////
int txtlen(size_t blen, const char* bin) {
  FUSION_ENSURE(bin, return -1);

  int tlen = 0;

  for (size_t i = 0; i < blen; ++i)
    if (::isalnum((unsigned char)bin[i]))
      tlen += ::_snprintf(0, 0, "%c", bin[i]);
    else
      tlen += ::_snprintf(0, 0, (i + 1 < tlen && ::isxdigit((unsigned char)bin[i+1])) ? "\\x%0.2X" : "\\x%X", (unsigned char)bin[i]);

  return tlen;
}

///////////////////////////////////////////////////////////////////////////////
int txt2bin(size_t tlen, const char* txt, size_t dmax, char* bin) {
  FUSION_ENSURE(txt, return -1);

  const unsigned char *t  = (const unsigned char*)txt,
                      *t1 = t + tlen;
  unsigned char       *b0 = (unsigned char*)bin,
                      *b  = b0,
                      *b1 = b + (bin ? dmax : UINTMAX_MAX);

  while ((t1 - t > 3) && (b < b1)) {
    if (t[0] == '\\') {
      if (t[1] == 'x' || t[1] == 'X') {
        switch (t[2]) {
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
          switch (t[3]) {
          case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            if (bin)
              switch (t[2]) {
              case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                *b = ((t[2] - '0') << 4)      + (t[3] - '0');

                break;

              case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                *b = ((t[2] - 'a' + 10) << 4) + (t[3] - '0');

                break;

              case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
                *b = ((t[2] - 'A' + 10) << 4) + (t[3] - '0');

                break;
              }

            ++b;
            t += 4;

            break;

          case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
            if (bin)
              switch (t[2]) {
              case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                *b = ((t[2] - '0') << 4)      + (t[3] - 'a' + 10);

                break;

              case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                *b = ((t[2] - 'a' + 10) << 4) + (t[3] - 'a' + 10);

                break;

              case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
                *b = ((t[2] - 'A' + 10) << 4) + (t[3] - 'a' + 10);

                break;
              }

            ++b;
            t += 4;

            break;

          case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            if (bin)
              switch (t[2]) {
              case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                *b = ((t[2] - '0') << 4)      + (t[3] - 'A' + 10);

                break;

              case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                *b = ((t[2] - 'a' + 10) << 4) + (t[3] - 'A' + 10);

                break;

              case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
                *b = ((t[2] - 'A' + 10) << 4) + (t[3] - 'A' + 10);

                break;
              }

            ++b;
            t += 4;

            break;

          default:
            switch (t[2]) {
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
              if (bin)
                *b = t[2] - '0';

              break;

            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
              if (bin)
                *b = t[2] - 'a' + 10;

              break;

            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
              if (bin)
                *b = t[2] - 'A' + 10;

              break;

            default:
              return -1;
            }

            ++b;
            t += 3;
          }

          continue;

        default:
          return -1;
        }
      }
      else
        return -1;
    }
    else {
      if (bin)
        *b = *t++;

      ++b;
    }
  }

  while ((t1 - t > 2) && (b < b1)) {
    if (t[0] == '\\') {
      if (t[1] == 'x' || t[1] == 'X') {
        switch (t[2]) {
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
          if (bin)
            *b = t[2] - '0';

          break;

        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
          if (bin)
            *b = t[2] - 'a' + 10;

          break;

        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
          if (bin)
            *b = t[2] - 'A' + 10;

          break;

        default:
          return -1;
        }

        ++b;
        t += 3;
      }
      else
        return -1;
    }
    else {
      if (bin)
        *b = *t++;

      ++b;
    }
  }

  while ((t < t1) && (b < b1)) {
    if (t[0] == '\\')
      return -1;

    if (bin)
      *b = *t++;

    ++b;
  }

  return b - b0;
}
