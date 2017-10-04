#ifndef BASE64_H
#define BASE64_H

#include <string>

namespace _base64 {

    const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

    inline void a3_to_a4(unsigned char *a4, unsigned char *a3) {
        a4[0] = (a3[0] & 0xfc) >> 2;
        a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
        a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
        a4[3] = (a3[2] & 0x3f);
    }

    inline void a4_to_a3(unsigned char *a3, unsigned char *a4) {
        a3[0] = (a4[0] << 2) + ((a4[1] & 0x30) >> 4);
        a3[1] = ((a4[1] & 0xf) << 4) + ((a4[2] & 0x3c) >> 2);
        a3[2] = ((a4[2] & 0x3) << 6) + a4[3];
    }

    inline unsigned char b64_lookup(unsigned char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 71;
        if (c >= '0' && c <= '9') return c + 4;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 255;
    }

    int DecodedLength(const char *in, size_t in_length) {
        int numEq = 0;

        const char *in_end = in + in_length;
        while (*--in_end == '=') ++numEq;

        return ((6 * in_length) / 8) - numEq;
    }

    int DecodedLength(const std::string &in) {
        int numEq = 0;
        int n = in.size();

        for (std::string::const_reverse_iterator it = in.rbegin(); *it == '='; ++it) {
            ++numEq;
        }

        return ((6 * n) / 8) - numEq;
    }

    inline int EncodedLength(size_t length) {
        return (length + 2 - ((length + 2) % 3)) / 3 * 4;
    }

    inline int EncodedLength(const std::string &in) {
        return EncodedLength(in.length());
    }

    inline void StripPadding(std::string *in) {
        while (!in->empty() && *(in->rbegin()) == '=') in->resize(in->size() - 1);
    }
}

inline std::string base64_encode(const std::string &in) {
    int i = 0, j = 0;
    size_t enc_len = 0;
    unsigned char a3[3];
    unsigned char a4[4];
    std::string out;

    out.resize(_base64::EncodedLength(in));

    int input_len = in.size();
    std::string::const_iterator input = in.begin();

    while (input_len--) {
        a3[i++] = *(input++);
        if (i == 3) {
            _base64::a3_to_a4(a4, a3);

            for (i = 0; i < 4; i++) {
                out[enc_len++] = _base64::kBase64Alphabet[a4[i]];
            }

            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++) {
            a3[j] = '\0';
        }

        _base64::a3_to_a4(a4, a3);

        for (j = 0; j < i + 1; j++) {
            out[enc_len++] = _base64::kBase64Alphabet[a4[j]];
        }

        while ((i++ < 3)) {
            out[enc_len++] = '=';
        }
    }

    return out;
}

inline std::string base64_decode(const std::string &in) {
    int i = 0, j = 0;
    size_t dec_len = 0;
    unsigned char a3[3];
    unsigned char a4[4];
    std::string out(_base64::DecodedLength(in), 0);

    int input_len = in.size();
    std::string::const_iterator input = in.begin();

    //out.resize(_base64::DecodedLength(in));

    while (input_len--) {
        if (*input == '=') {
            break;
        }

        a4[i++] = *(input++);
        if (i == 4) {
            for (i = 0; i <4; i++) {
                a4[i] = _base64::b64_lookup(a4[i]);
            }

            _base64::a4_to_a3(a3,a4);

            for (i = 0; i < 3; i++) {
                out[dec_len++] = a3[i];
            }

            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++) {
            a4[j] = '\0';
        }

        for (j = 0; j < 4; j++) {
            a4[j] = _base64::b64_lookup(a4[j]);
        }

        _base64::a4_to_a3(a3,a4);

        for (j = 0; j < i - 1; j++) {
            out[dec_len++] = a3[j];
        }
    }

    return out;
}



#endif // BASE64_H