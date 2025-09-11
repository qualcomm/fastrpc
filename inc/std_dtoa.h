// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef STD_DTOA_H
#define STD_DTOA_H

//
// Constant Definitions
//

// For floating point numbers, the range of a double precision number is
// approximately +/- 10 ^ 308.25 as per the IEEE Standard 754.
// As such, the maximum size of the integer portion of the
// string is assumed to be 311 (309 + sign + \0). The maximum
// size of the fractional part is assumed to be 100. Thus, the
// maximum size of the string that would contain the number
// after conversion is safely assumed to be 420 (including any
// prefix, the null character and exponent specifiers 'e').
//
// The buffers that contain the converted integer and the fraction parts of
// the float are safely assumed to be of size 310.
#define STD_DTOA_FORMAT_FLOAT_SIZE 420
#define STD_DTOA_FORMAT_INTEGER_SIZE 311
#define STD_DTOA_FORMAT_FRACTION_SIZE 100

// Constants for operations on the IEEE 754 representation of double
// precision floating point numbers.
#define STD_DTOA_DP_SIGN_SHIFT_COUNT 63
#define STD_DTOA_DP_EXPONENT_SHIFT_COUNT 52
#define STD_DTOA_DP_EXPONENT_MASK 0x7ff
#define STD_DTOA_DP_EXPONENT_BIAS 1023
#define STD_DTOA_DP_MANTISSA_MASK (((uint64_t)1 << 52) - 1)
#define STD_DTOA_DP_INFINITY_EXPONENT_ID 0x7FF
#define STD_DTOA_DP_MAX_EXPONENT 1023
#define STD_DTOA_DP_MIN_EXPONENT_NORM -1022
#define STD_DTOA_DP_MIN_EXPONENT_DENORM -1074
#define STD_DTOA_DP_MAX_EXPONENT_DEC 308
#define STD_DTOA_DP_MIN_EXPONENT_DEC_DENORM -323

#define STD_DTOA_PRECISION_ROUNDING_VALUE 4
#define STD_DTOA_DEFAULT_FLOAT_PRECISION 6

#define STD_DTOA_NEGATIVE_INF_UPPER_CASE "-INF"
#define STD_DTOA_NEGATIVE_INF_LOWER_CASE "-inf"
#define STD_DTOA_POSITIVE_INF_UPPER_CASE "INF"
#define STD_DTOA_POSITIVE_INF_LOWER_CASE "inf"
#define STD_DTOA_NAN_UPPER_CASE "NAN"
#define STD_DTOA_NAN_LOWER_CASE "nan"
#define STD_DTOA_FP_POSITIVE_INF 0x7FF0000000000000uLL
#define STD_DTOA_FP_NEGATIVE_INF 0xFFF0000000000000uLL
#define STD_DTOA_FP_SNAN 0xFFF0000000000001uLL
#define STD_DTOA_FP_QNAN 0xFFFFFFFFFFFFFFFFuLL

//
// Useful Macros
//

#define MY_ISDIGIT(c) (((c) >= '0') && ((c) <= '9'))
#define FP_EXPONENT(u)                                                        \
	((((u) >> STD_DTOA_DP_EXPONENT_SHIFT_COUNT)                           \
	  & STD_DTOA_DP_EXPONENT_MASK)                                        \
	 - STD_DTOA_DP_EXPONENT_BIAS)
#define FP_EXPONENT_BIASED(u)                                                 \
	(((u) >> STD_DTOA_DP_EXPONENT_SHIFT_COUNT) & STD_DTOA_DP_EXPONENT_MASK)
#define FP_MANTISSA_NORM(u)                                                   \
	(((u) & STD_DTOA_DP_MANTISSA_MASK)                                    \
	 | ((uint64_t)1 << STD_DTOA_DP_EXPONENT_SHIFT_COUNT))
#define FP_MANTISSA_DENORM(u) ((u) & STD_DTOA_DP_MANTISSA_MASK)
#define FP_MANTISSA(u)                                                        \
	(FP_EXPONENT_BIASED(u) ? FP_MANTISSA_NORM(u) : FP_MANTISSA_DENORM(u))
#define FP_SIGN(u) ((u) >> STD_DTOA_DP_SIGN_SHIFT_COUNT)

//
// Type Definitions
//

typedef enum {
	FP_TYPE_UNKOWN = 0,
	FP_TYPE_NEGATIVE_INF,
	FP_TYPE_POSITIVE_INF,
	FP_TYPE_NAN,
	FP_TYPE_GENERAL,
} FloatingPointType;

//
// Function Declarations
//

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

double fp_pow_10(int nPow);
double fp_round(double dNumber, int nPrecision);
int fp_log_10(double dNumber);
int fp_check_special_cases(double dNumber, FloatingPointType *pNumberType);
int std_dtoa_decimal(double dNumber, int nPrecision,
                     char acIntegerPart[STD_DTOA_FORMAT_INTEGER_SIZE],
                     char acFractionPart[STD_DTOA_FORMAT_FRACTION_SIZE]);
int std_dtoa_hex(double dNumber, int nPrecision, char cFormat,
                 char acIntegerPart[STD_DTOA_FORMAT_INTEGER_SIZE],
                 char acFractionPart[STD_DTOA_FORMAT_FRACTION_SIZE],
                 int *pnExponent);

#ifdef __cplusplus
}
#endif // #ifdef __cplusplus

#endif // STD_DTOA_H
