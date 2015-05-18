ICACHE_FLASH_ATTR
int kmp_pow(int x, int y) {
    int i;
    int result;

    if (y == 0) {
        return 1;
    }

    result = x;
    for (i = 1; i < y; i++) {
        result *= x;
    }
    return result;
}

ICACHE_FLASH_ATTR
unsigned int decimal_number_length(int n) {
	int digits;

	digits = n < 0;	//count "minus"
	do {
		digits++;
	} while (n /= 10);

	return digits;
}

ICACHE_FLASH_ATTR
void kmp_value_to_string(int32_t value, uint8_t si_ex, unsigned char *value_string) {
    double result;
    uint32_t result_int, result_frac;
    int8_t sign_i = (si_ex & 0x80) >> 7;
    int8_t sign_e = (si_ex & 0x40) >> 6;
    int8_t exponent = (si_ex & 0x3f);
	uint32_t factor;
	unsigned char leading_zeroes[16];
	unsigned int i;

	factor = kmp_pow(10, exponent);
    if (sign_i) {
        if (sign_e) {
            result = value / kmp_pow(10, exponent);
            result_int = (int32_t)result;
			result_frac = value - result_int * kmp_pow(10, exponent);

			// prepare decimal string
			strcpy(leading_zeroes, "");
			for (i = 0; i < (exponent - decimal_number_length(result_frac)); i++) {
				strcat(leading_zeroes, "0");
			}
			os_sprintf(value_string, "-%u.%s%u", result_int, leading_zeroes, result_frac);
        }
        else {
            os_sprintf(value_string, "-%u", value * factor);
        }
    }
    else {
        if (sign_e) {
            result = value / kmp_pow(10, exponent);
            result_int = (int32_t)result;
			result_frac = value - result_int * kmp_pow(10, exponent);

			// prepare decimal string
			strcpy(leading_zeroes, "");
			for (i = 0; i < (exponent - decimal_number_length(result_frac)); i++) {
				strcat(leading_zeroes, "0");
			}
			os_sprintf(value_string, "%u.%s%u", result_int, leading_zeroes, result_frac);
        }
        else {
            os_sprintf(value_string, "%u", value * factor);
        }
    }
}