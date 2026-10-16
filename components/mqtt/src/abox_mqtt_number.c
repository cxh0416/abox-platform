#include "abox_mqtt_digest.h"
#include <float.h>
#include <math.h>
#include <string.h>

/* Keep the upstream shortest-conversion implementation private to this codec.
 * Small read-only tables and portable integer operations suit Cortex-M3. */
#define RYU_OPTIMIZE_SIZE
#define RYU_ONLY_64_BIT_OPS
#include "../third_party/ryu/d2s.c"

_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "MQTT Number requires IEEE 754 binary64");

int ABoxMqttDigest_WriteNumber(double value, ABoxCanonicalWriter write, void *context)
{
    char scientific[25], digits[17], result[25];
    size_t count = 0, used = 0, i = 0;
    int exponent = 0, negative_exponent = 0;
    if (!write || !isfinite(value) || (value == 0 && signbit(value))) return 0;
    if (value == 0) return write(context, "0", 1);
    int length = d2s_buffered_n(value, scientific);
    if (scientific[i] == '-') { result[used++] = '-'; ++i; }
    while (i < (size_t)length && scientific[i] != 'E') {
        if (scientific[i] != '.') digits[count++] = scientific[i];
        ++i;
    }
    ++i;
    if (i < (size_t)length && scientific[i] == '-') { negative_exponent = 1; ++i; }
    while (i < (size_t)length) exponent = exponent * 10 + scientific[i++] - '0';
    if (negative_exponent) exponent = -exponent;
    if (exponent >= -6 && exponent < 21) {
        int point = exponent + 1;
        if (point <= 0) {
            result[used++] = '0'; result[used++] = '.';
            while (point++ < 0) result[used++] = '0';
            memcpy(result + used, digits, count); used += count;
        } else {
            for (i = 0; i < count || (int)i < point; ++i) {
                if ((int)i == point) result[used++] = '.';
                result[used++] = i < count ? digits[i] : '0';
            }
        }
    } else {
        result[used++] = digits[0];
        if (count > 1) {
            result[used++] = '.'; memcpy(result + used, digits + 1, count - 1); used += count - 1;
        }
        result[used++] = 'e'; result[used++] = exponent < 0 ? '-' : '+';
        unsigned magnitude = (unsigned)(exponent < 0 ? -exponent : exponent);
        if (magnitude >= 100) result[used++] = (char)('0' + magnitude / 100);
        if (magnitude >= 10) result[used++] = (char)('0' + magnitude / 10 % 10);
        result[used++] = (char)('0' + magnitude % 10);
    }
    return write(context, result, used);
}
