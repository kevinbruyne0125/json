#include "unicode.h"
#include "fbuffer.h"

/*
 * Copyright 2001-2004 Unicode, Inc.
 * 
 * Disclaimer
 * 
 * This source code is provided as is by Unicode, Inc. No claims are
 * made as to fitness for any particular purpose. No warranties of any
 * kind are expressed or implied. The recipient agrees to determine
 * applicability of information provided. If this file has been
 * purchased on magnetic or optical media from Unicode, Inc., the
 * sole remedy for any claim will be exchange of defective media
 * within 90 days of receipt.
 * 
 * Limitations on Rights to Redistribute This Code
 * 
 * Unicode, Inc. hereby grants the right to freely use the information
 * supplied in this file in the creation of products supporting the
 * Unicode Standard, and to make copies of this file in any form
 * for internal or external distribution as long as this notice
 * remains attached.
 */

/*
 * Index into the table below with the first byte of a UTF-8 sequence to
 * get the number of trailing bytes that are supposed to follow it.
 * Note that *legal* UTF-8 values can't have 4 or 5-bytes. The table is
 * left as-is for anyone who may want to do such conversion, which was
 * allowed in earlier algorithms.
 */
static const char trailingBytesForUTF8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

/*
 * Magic values subtracted from a buffer value during UTF8 conversion.
 * This table contains as many values as there might be trailing bytes
 * in a UTF-8 sequence.
 */
static const UTF32 offsetsFromUTF8[6] = { 0x00000000UL, 0x00003080UL, 0x000E2080UL, 
		     0x03C82080UL, 0xFA082080UL, 0x82082080UL };

/*
 * Utility routine to tell whether a sequence of bytes is legal UTF-8.
 * This must be called with the length pre-determined by the first byte.
 * If not calling this from ConvertUTF8to*, then the length can be set by:
 *  length = trailingBytesForUTF8[*source]+1;
 * and the sequence is illegal right away if there aren't that many bytes
 * available.
 * If presented with a length > 4, this returns 0.  The Unicode
 * definition of UTF-8 goes up to 4-byte sequences.
 */

inline static unsigned char isLegalUTF8(const UTF8 *source, int length)
{
    UTF8 a;
    const UTF8 *srcptr = source+length;
    switch (length) {
        default: return 0;
                 /* Everything else falls through when "1"... */
        case 4: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
        case 3: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
        case 2: if ((a = (*--srcptr)) > 0xBF) return 0;

                    switch (*source) {
                        /* no fall-through in this inner switch */
                        case 0xE0: if (a < 0xA0) return 0; break;
                        case 0xED: if (a > 0x9F) return 0; break;
                        case 0xF0: if (a < 0x90) return 0; break;
                        case 0xF4: if (a > 0x8F) return 0; break;
                        default:   if (a < 0x80) return 0;
                    }

        case 1: if (*source >= 0x80 && *source < 0xC2) return 0;
    }
    if (*source > 0xF4) return 0;
    return 1;
}

inline static void unicode_escape(char *buf, UTF16 character)
{
    const char *digits = "0123456789abcdef";

    buf[2] = digits[character >> 12];
    buf[3] = digits[(character >> 8) & 0xf];
    buf[4] = digits[(character >> 4) & 0xf];
    buf[5] = digits[character & 0xf];
}


inline static void unicode_escape_to_buffer(FBuffer *buffer, char buf[6], UTF16 character)
{
    unicode_escape(buf, character);
    fbuffer_append(buffer, buf, 6);
}

inline void convert_UTF8_to_JSON_ASCII(FBuffer *buffer, VALUE string)
{
    const UTF8 *source = (UTF8 *) RSTRING_PTR(string);
    const UTF8 *sourceEnd = source + RSTRING_LEN(string);
    char buf[6] = { '\\', 'u' };

    while (source < sourceEnd) {
        UTF32 ch = 0;
        unsigned short extraBytesToRead = trailingBytesForUTF8[*source];
        if (source + extraBytesToRead >= sourceEnd) {
            rb_raise(rb_path2class("JSON::GeneratorError"),
                    "partial character in source, but hit end");
        }
        if (!isLegalUTF8(source, extraBytesToRead+1)) {
            rb_raise(rb_path2class("JSON::GeneratorError"),
                    "source sequence is illegal/malformed utf-8");
        }
        /*
         * The cases all fall through. See "Note A" below.
         */
        switch (extraBytesToRead) {
            case 5: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
            case 4: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
            case 3: ch += *source++; ch <<= 6;
            case 2: ch += *source++; ch <<= 6;
            case 1: ch += *source++; ch <<= 6;
            case 0: ch += *source++;
        }
        ch -= offsetsFromUTF8[extraBytesToRead];

        if (ch <= UNI_MAX_BMP) { /* Target is a character <= 0xFFFF */
            /* UTF-16 surrogate values are illegal in UTF-32 */
            if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_LOW_END) {
#if UNI_STRICT_CONVERSION
                source -= (extraBytesToRead+1); /* return to the illegal value itself */
                rb_raise(rb_path2class("JSON::GeneratorError"),
                        "source sequence is illegal/malformed utf-8");
#else
                unicode_escape_to_buffer(buffer, buf, UNI_REPLACEMENT_CHAR);
#endif
            } else {
                /* normal case */
                switch (ch) {
                    case '\n':
                        fbuffer_append(buffer, "\\n", 2);
                        break;
                    case '\r':
                        fbuffer_append(buffer, "\\r", 2);
                        break;
                    case '\\':
                        fbuffer_append(buffer, "\\\\", 2);
                        break;
                    case '"':
                        fbuffer_append(buffer, "\\\"", 2);
                        break;
                    case '\t':
                        fbuffer_append(buffer, "\\t", 2);
                        break;
                    case '\f':
                        fbuffer_append(buffer, "\\f", 2);
                        break;
                    case '\b':
                        fbuffer_append(buffer, "\\b", 2);
                        break;
                    default:
                        if (ch >= 0x20 && ch <= 0x7f) {
                            fbuffer_append_char(buffer, ch);
                        } else {
                            unicode_escape_to_buffer(buffer, buf, (UTF16) ch);
                        }
                        break;
                }
            }
        } else if (ch > UNI_MAX_UTF16) {
#if UNI_STRICT_CONVERSION
            source -= (extraBytesToRead+1); /* return to the start */
            rb_raise(rb_path2class("JSON::GeneratorError"),
                    "source sequence is illegal/malformed utf8");
#else
            unicode_escape_to_buffer(buffer, buf, UNI_REPLACEMENT_CHAR);
#endif
        } else {
            /* target is a character in range 0xFFFF - 0x10FFFF. */
            ch -= halfBase;
            unicode_escape_to_buffer(buffer, buf, (UTF16)((ch >> halfShift) + UNI_SUR_HIGH_START));
            unicode_escape_to_buffer(buffer, buf, (UTF16)((ch & halfMask) + UNI_SUR_LOW_START));
        }
    }
}

inline void convert_UTF8_to_JSON(FBuffer *buffer, VALUE string)
{
    const char *ptr = RSTRING_PTR(string), *p;
    int len = RSTRING_LEN(string), start = 0, end = 0;
    const char *escape = NULL;
    int escape_len;
    unsigned char c;
    char buf[6] = { '\\', 'u' };

    for (start = 0, end = 0; end < len;) {
        p = ptr + end;
        c = (unsigned char) *p;
        if (c < 0x20) {
            switch (c) {
                case '\n':
                    escape = "\\n";
                    escape_len = 2;
                    break;
                case '\r':
                    escape = "\\r";
                    escape_len = 2;
                    break;
                case '\t':
                    escape = "\\t";
                    escape_len = 2;
                    break;
                case '\f':
                    escape = "\\f";
                    escape_len = 2;
                    break;
                case '\b':
                    escape = "\\b";
                    escape_len = 2;
                    break;
                default:
                    unicode_escape(buf, (UTF16) *p);
                    escape = buf;
                    escape_len = 6;
                    break;
            }
        } else {
            switch (c) {
                case '\\':
                    escape = "\\\\";
                    escape_len = 2;
                    break;
                case '"':
                    escape =  "\\\"";
                    escape_len = 2;
                    break;
                default:
                    end++;
                    continue;
                    break;
            }
        }
        fbuffer_append(buffer, ptr + start, end - start);
        fbuffer_append(buffer, escape, escape_len);
        start = ++end;
        escape = NULL;
    }
    fbuffer_append(buffer, ptr + start, end - start);
}
