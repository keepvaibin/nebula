/* MSL's formatter (parse_format, long2str, longlong2str and __pformatter/__wpformatter as MSL_C/printf.c and wprintf.c
   have them), once per character type: msl_runtime.c includes it with FMT_WIDE 1 (wchar_t) and 0 (char). FMT_CHAR,
   FMT_L (literal), FMT_NAME (function name), FMT_STRLEN and FMT_STRCHR come from the includer. */

static int FMT_NAME(is_digit)(FMT_CHAR c) {
    return c >= FMT_L('0') && c <= FMT_L('9');
}

static const FMT_CHAR* FMT_NAME(parse_format)(const FMT_CHAR* s, va_list* ap, print_format* out) {
    print_format f;
    FMT_CHAR c;
    int flag;
    f.justification_options = right_justification;
    f.sign_options = only_minus;
    f.precision_specified = 0;
    f.alternate_form = 0;
    f.argument_options = normal_argument;
    f.pointer = 0;
    f.field_width = 0;
    f.precision = 0;

    if ((c = *++s) == FMT_L('%')) {
        f.conversion_char = c;
        *out = f;
        return s + 1;
    }

    for (;;) {
        flag = 1;
        switch (c) {
        case FMT_L('-'):
            f.justification_options = left_justification;
            break;
        case FMT_L('+'):
            f.sign_options = sign_always;
            break;
        case FMT_L(' '):
            if (f.sign_options != sign_always) {
                f.sign_options = space_holder;
            }
            break;
        case FMT_L('#'):
            f.alternate_form = 1;
            break;
        case FMT_L('0'):
            if (f.justification_options != left_justification) {
                f.justification_options = zero_fill;
            }
            break;
        default:
            flag = 0;
            break;
        }
        if (!flag) {
            break;
        }
        c = *++s;
    }

    if (c == FMT_L('*')) {
        if ((f.field_width = va_arg(*ap, int)) < 0) {
            f.justification_options = left_justification;
            f.field_width = -f.field_width;
        }
        c = *++s;
    } else {
        while (FMT_NAME(is_digit)(c)) {
            f.field_width = f.field_width * 10 + (c - FMT_L('0'));
            c = *++s;
        }
    }

    if (f.field_width > 509) {
        f.conversion_char = BAD_CONVERSION;
        *out = f;
        return s + 1;
    }

    if (c == FMT_L('.')) {
        f.precision_specified = 1;
        if ((c = *++s) == FMT_L('*')) {
            if ((f.precision = va_arg(*ap, int)) < 0) {
                f.precision_specified = 0;
            }
            c = *++s;
        } else {
            while (FMT_NAME(is_digit)(c)) {
                f.precision = f.precision * 10 + (c - FMT_L('0'));
                c = *++s;
            }
        }
    }

    flag = 1;
    switch (c) {
    case FMT_L('h'):
        f.argument_options = short_argument;
        if (s[1] == FMT_L('h')) {
            f.argument_options = char_argument;
            c = *++s;
        }
        break;
    case FMT_L('l'):
        f.argument_options = long_argument;
        if (s[1] == FMT_L('l')) {
            f.argument_options = long_long_argument;
            c = *++s;
        }
        break;
    case FMT_L('L'):
        f.argument_options = long_double_argument;
        break;
    case FMT_L('j'):
        f.argument_options = intmax_argument;
        break;
    case FMT_L('t'):
        f.argument_options = ptrdiff_argument;
        break;
    case FMT_L('z'):
        f.argument_options = size_t_argument;
        break;
    default:
        flag = 0;
        break;
    }
    if (flag) {
        c = *++s;
    }

    f.conversion_char = c;
    switch (c) {
    case FMT_L('d'):
    case FMT_L('i'):
    case FMT_L('u'):
    case FMT_L('o'):
    case FMT_L('x'):
    case FMT_L('X'):
        if (f.argument_options == long_double_argument) {
            f.argument_options = long_long_argument;
        }
        if (!f.precision_specified) {
            f.precision = 1;
        } else if (f.justification_options == zero_fill) {
            f.justification_options = right_justification;
        }
        break;
    case FMT_L('f'):
    case FMT_L('F'):
        if (f.argument_options == short_argument || f.argument_options == intmax_argument ||
            f.argument_options == size_t_argument || f.argument_options == ptrdiff_argument ||
            f.argument_options == long_long_argument) {
            f.conversion_char = BAD_CONVERSION;
            break;
        }
        if (!f.precision_specified) {
            f.precision = 6;
        }
        break;
    case FMT_L('a'):
    case FMT_L('A'):
        if (!f.precision_specified) {
            f.precision = 0xD;
        }
        if (f.argument_options == short_argument || f.argument_options == intmax_argument ||
            f.argument_options == size_t_argument || f.argument_options == ptrdiff_argument ||
            f.argument_options == long_long_argument || f.argument_options == char_argument) {
            f.conversion_char = BAD_CONVERSION;
        }
        break;
    case FMT_L('g'):
    case FMT_L('G'):
        if (!f.precision) {
            f.precision = 1;
        }
        /* fall through, as in MSL */
    case FMT_L('e'):
    case FMT_L('E'):
        if (f.argument_options == short_argument || f.argument_options == intmax_argument ||
            f.argument_options == size_t_argument || f.argument_options == ptrdiff_argument ||
            f.argument_options == long_long_argument || f.argument_options == char_argument) {
            f.conversion_char = BAD_CONVERSION;
            break;
        }
        if (!f.precision_specified) {
            f.precision = 6;
        }
        break;
    case FMT_L('p'):
        f.pointer = 1;
        f.argument_options = long_argument;
        f.alternate_form = 1;
        f.conversion_char = FMT_L('x');
        f.precision = 8;
        break;
    case FMT_L('c'):
        if (f.argument_options == long_argument) {
            f.argument_options = wchar_argument;
        } else if (f.precision_specified || f.argument_options != normal_argument) {
            f.conversion_char = BAD_CONVERSION;
        }
        break;
    case FMT_L('s'):
        if (f.argument_options == long_argument) {
            f.argument_options = wchar_argument;
        } else if (f.argument_options != normal_argument) {
            f.conversion_char = BAD_CONVERSION;
        }
        break;
    case FMT_L('n'):
        if (f.argument_options == long_double_argument) {
            f.argument_options = long_long_argument;
        }
        break;
    default:
        f.conversion_char = BAD_CONVERSION;
        break;
    }

    *out = f;
    return s + 1;
}

/* Digits right-aligned ending at buff[-1] (a NUL there); returns the first, or 0 past 509 characters. */
static FMT_CHAR* FMT_NAME(int2str)(uint64_t magnitude, int minus, unsigned base, FMT_CHAR* buff, print_format f) {
    FMT_CHAR* p = buff;
    int digits = 0;
    *--p = 0;

    if (!magnitude && !f.precision && !(f.alternate_form && f.conversion_char == FMT_L('o'))) {
        return p;
    }

    do {
        unsigned n = (unsigned)(magnitude % base);
        magnitude /= base;
        if (n < 10) {
            n += FMT_L('0');
        } else {
            n -= 10;
            n += (f.conversion_char == FMT_L('x')) ? FMT_L('a') : FMT_L('A');
        }
        *--p = (FMT_CHAR)n;
        ++digits;
    } while (magnitude != 0);

    if (base == 8 && f.alternate_form && *p != FMT_L('0')) {
        *--p = FMT_L('0');
        ++digits;
    }

    if (f.justification_options == zero_fill) {
        f.precision = f.field_width;
        if (minus || f.sign_options != only_minus) {
            --f.precision;
        }
        if (base == 16 && f.alternate_form) {
            f.precision -= 2;
        }
    }

    if (buff - p + f.precision > 509) {
        return 0;
    }

    while (digits < f.precision) {
        *--p = FMT_L('0');
        ++digits;
    }

    if (base == 16 && f.alternate_form) {
        *--p = (FMT_CHAR)f.conversion_char;
        *--p = FMT_L('0');
    }

    if (minus) {
        *--p = FMT_L('-');
    } else if (f.sign_options == sign_always) {
        *--p = FMT_L('+');
    } else if (f.sign_options == space_holder) {
        *--p = FMT_L(' ');
    }
    return p;
}

/* The console's long is 32 bits: l-sized arguments are read and printed as 32-bit values. */
static FMT_CHAR* FMT_NAME(num2str)(int is_signed, int64_t value, int wide, FMT_CHAR* buff, print_format f) {
    unsigned base = 10;
    int minus = 0;
    uint64_t magnitude;

    switch (f.conversion_char) {
    case FMT_L('o'):
        base = 8;
        f.sign_options = only_minus;
        break;
    case FMT_L('u'):
        f.sign_options = only_minus;
        break;
    case FMT_L('x'):
    case FMT_L('X'):
        base = 16;
        f.sign_options = only_minus;
        break;
    default:
        break;
    }

    if (wide) {
        magnitude = (uint64_t)value;
        if (is_signed && value < 0) {
            minus = 1;
            magnitude = 0 - (uint64_t)value;
        }
    } else {
        const int32_t v = (int32_t)value;
        magnitude = (uint32_t)v;
        if (is_signed && v < 0) {
            minus = 1;
            magnitude = 0u - (uint32_t)v;
        }
    }
    return FMT_NAME(int2str)(magnitude, minus, base, buff, f);
}

static int FMT_NAME(write_out)(FMT_NAME(out_ctrl) * o, const FMT_CHAR* src, size_t n) {
    size_t room = o->written + n <= o->max ? n : o->max - o->written;
    memmove(o->str + o->written, src, room * sizeof(FMT_CHAR));
    o->written += room;
    return 1;
}

/* The host's narrow formatter supplies a float's digits. */
static FMT_CHAR* FMT_NAME(float2str)(long double value, FMT_CHAR* buff, print_format f) {
    char spec[16], text[512];
    int k = 0, n, i;
    spec[k++] = '%';
    if (f.sign_options == sign_always) {
        spec[k++] = '+';
    } else if (f.sign_options == space_holder) {
        spec[k++] = ' ';
    }
    if (f.alternate_form) {
        spec[k++] = '#';
    }
    spec[k++] = '.';
    spec[k++] = '*';
    spec[k++] = 'L';
    spec[k++] = (char)f.conversion_char;
    spec[k] = 0;
    n = snprintf(text, sizeof(text), spec, f.precision, value);
    if (n < 0 || n >= (int)sizeof(text) || n > 509) {
        return 0;
    }
    buff -= n + 1;
    for (i = 0; i <= n; ++i) {
        buff[i] = (FMT_CHAR)(unsigned char)text[i];
    }
    return buff;
}

static int FMT_NAME(formatter)(FMT_NAME(out_ctrl) * o, const FMT_CHAR* format, va_list* ap) {
    int num_chars, chars_written = 0, field_width;
    const FMT_CHAR* format_ptr = format;
    const FMT_CHAR* curr_format;
    print_format f;
    FMT_CHAR buff[512];
    FMT_CHAR* buff_ptr;
    FMT_CHAR fill_char;

    while (*format_ptr) {
        if (!(curr_format = FMT_STRCHR(format_ptr, FMT_L('%')))) {
            num_chars = (int)FMT_STRLEN(format_ptr);
            chars_written += num_chars;
            if (num_chars) {
                FMT_NAME(write_out)(o, format_ptr, (size_t)num_chars);
            }
            break;
        }

        num_chars = (int)(curr_format - format_ptr);
        chars_written += num_chars;
        if (num_chars) {
            FMT_NAME(write_out)(o, format_ptr, (size_t)num_chars);
        }

        format_ptr = FMT_NAME(parse_format)(curr_format, ap, &f);

        switch (f.conversion_char) {
        case FMT_L('d'):
        case FMT_L('i'):
        case FMT_L('o'):
        case FMT_L('u'):
        case FMT_L('x'):
        case FMT_L('X'): {
            const int is_signed = f.conversion_char == FMT_L('d') || f.conversion_char == FMT_L('i');
            int64_t value;
            int wide = 0;
            if (f.argument_options == long_long_argument || f.argument_options == intmax_argument) {
                value = va_arg(*ap, long long);
                wide = 1;
            } else if (f.argument_options == size_t_argument) {
                value = (int64_t)va_arg(*ap, size_t);
            } else if (f.argument_options == ptrdiff_argument) {
                value = (int64_t)va_arg(*ap, ptrdiff_t);
            } else if (f.pointer) {
                /* %p: a native pointer, printed in full (the console's was 32 bits). */
                value = (int64_t)(uintptr_t)va_arg(*ap, void*);
                wide = 1;
            } else {
                value = is_signed ? (int64_t)va_arg(*ap, int) : (int64_t)va_arg(*ap, unsigned int);
            }
            if (f.argument_options == short_argument) {
                value = is_signed ? (int64_t)(short)value : (int64_t)(unsigned short)value;
            } else if (f.argument_options == char_argument) {
                value = is_signed ? (int64_t)(signed char)value : (int64_t)(unsigned char)value;
            }
            if (!(buff_ptr = FMT_NAME(num2str)(is_signed, value, wide, buff + 512, f))) {
                goto conversion_error;
            }
            num_chars = (int)(buff + 512 - 1 - buff_ptr);
            break;
        }

        case FMT_L('f'):
        case FMT_L('F'):
        case FMT_L('e'):
        case FMT_L('E'):
        case FMT_L('g'):
        case FMT_L('G'):
        case FMT_L('a'):
        case FMT_L('A'): {
            long double value = f.argument_options == long_double_argument ? va_arg(*ap, long double)
                                                                           : (long double)va_arg(*ap, double);
            if (!(buff_ptr = FMT_NAME(float2str)(value, buff + 512, f))) {
                goto conversion_error;
            }
            num_chars = (int)(buff + 512 - 1 - buff_ptr);
            break;
        }

        case FMT_L('s'):
#if FMT_WIDE
            if (f.argument_options == wchar_argument) {
                buff_ptr = va_arg(*ap, wchar_t*);
                if (buff_ptr == 0) {
                    static wchar_t empty[1];
                    buff_ptr = empty;
                }
                if (f.alternate_form) {
                    num_chars = (unsigned char)*buff_ptr++;
                    if (f.precision_specified && num_chars > f.precision) {
                        num_chars = f.precision;
                    }
                } else if (f.precision_specified) {
                    int k = 0;
                    while (k < f.precision && buff_ptr[k]) {
                        ++k;
                    }
                    num_chars = k;
                } else {
                    num_chars = (int)nebula_msl_wcslen(buff_ptr);
                }
            } else {
                const char* str = va_arg(*ap, const char*);
                int k;
                if (str == 0) {
                    str = "";
                }
                if (f.alternate_form) {
                    num_chars = (unsigned char)*str++;
                    if (f.precision_specified && num_chars > f.precision) {
                        num_chars = f.precision;
                    }
                } else if (f.precision_specified) {
                    const char* end = (const char*)memchr(str, 0, (size_t)f.precision);
                    num_chars = end ? (int)(end - str) : f.precision;
                } else {
                    num_chars = (int)strlen(str);
                }
                /* MSL's mbstowcs in the "C" locale: each byte widened, stopping at a NUL. */
                if (num_chars > 512) {
                    num_chars = 512;
                }
                for (k = 0; k < num_chars && str[k]; ++k) {
                    buff[k] = (unsigned char)str[k];
                }
                num_chars = k;
                buff_ptr = buff;
            }
#else
            if (f.argument_options == wchar_argument) {
                /* MSL's wcstombs in the "C" locale: each character to its byte, failing past 0xFF. */
                const wchar_t* wcs = va_arg(*ap, const wchar_t*);
                int k;
                if (wcs == 0) {
                    wcs = L"";
                }
                for (k = 0; k < 512 && wcs[k]; ++k) {
                    if (wcs[k] > 0xFF) {
                        goto conversion_error;
                    }
                    buff[k] = (char)wcs[k];
                }
                if (k == 512) {
                    goto conversion_error;
                }
                buff[k] = 0;
                buff_ptr = buff;
            } else {
                buff_ptr = va_arg(*ap, char*);
            }
            if (buff_ptr == 0) {
                static char empty[1];
                buff_ptr = empty;
            }
            if (f.alternate_form) {
                num_chars = (unsigned char)*buff_ptr++;
                if (f.precision_specified && num_chars > f.precision) {
                    num_chars = f.precision;
                }
            } else if (f.precision_specified) {
                const char* end = (const char*)memchr(buff_ptr, 0, (size_t)f.precision);
                num_chars = end ? (int)(end - buff_ptr) : f.precision;
            } else {
                num_chars = (int)strlen(buff_ptr);
            }
#endif
            break;

        case FMT_L('n'): {
            void* dst = va_arg(*ap, void*);
            switch (f.argument_options) {
            case normal_argument:
            case long_argument:
                *(int*)dst = chars_written;
                break;
            case short_argument:
                *(short*)dst = (short)chars_written;
                break;
            case intmax_argument:
            case long_long_argument:
                *(long long*)dst = chars_written;
                break;
            case size_t_argument:
                *(size_t*)dst = (size_t)chars_written;
                break;
            case ptrdiff_argument:
                *(ptrdiff_t*)dst = chars_written;
                break;
            default:
                break;
            }
            continue;
        }

        case FMT_L('c'):
            buff_ptr = buff;
#if FMT_WIDE
            if (f.argument_options == wchar_argument) {
                *buff_ptr = (wchar_t)va_arg(*ap, int);
                num_chars = 1;
            } else {
                /* MSL's mbtowc: a NUL character converts to nothing. */
                const char ch = (char)va_arg(*ap, int);
                *buff_ptr = (unsigned char)ch;
                num_chars = ch ? 1 : 0;
            }
#else
            /* The byte as it is, NUL included. */
            *buff_ptr = (char)va_arg(*ap, int);
            num_chars = 1;
#endif
            break;

        case FMT_L('%'):
            buff_ptr = buff;
            *buff_ptr = FMT_L('%');
            num_chars = 1;
            break;

        default:
        conversion_error:
            /* The rest of the format, unconverted, ends the output. */
            num_chars = (int)FMT_STRLEN(curr_format);
            chars_written += num_chars;
            if (num_chars) {
                FMT_NAME(write_out)(o, curr_format, (size_t)num_chars);
            }
            return chars_written;
        }

        field_width = num_chars;
        if (f.justification_options != left_justification) {
            fill_char = (f.justification_options == zero_fill) ? FMT_L('0') : FMT_L(' ');
            if ((*buff_ptr == FMT_L('+') || *buff_ptr == FMT_L('-') || *buff_ptr == FMT_L(' ')) && fill_char == FMT_L('0')) {
                FMT_NAME(write_out)(o, buff_ptr, 1);
                ++buff_ptr;
                num_chars--;
            }
            while (field_width < f.field_width) {
                FMT_NAME(write_out)(o, &fill_char, 1);
                ++field_width;
            }
        }
        if (num_chars) {
            FMT_NAME(write_out)(o, buff_ptr, (size_t)num_chars);
        }
        if (f.justification_options == left_justification) {
            const FMT_CHAR blank = FMT_L(' ');
            while (field_width < f.field_width) {
                FMT_NAME(write_out)(o, &blank, 1);
                ++field_width;
            }
        }
        chars_written += field_width;
    }
    return chars_written;
}
