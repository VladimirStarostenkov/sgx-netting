//
// Created by vytautas on 7/27/17.
//

#include "crypto.h"


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sgx.h>
#include <cstdio>

#include <vector>
#include <map>

#include <openssl/ec.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/engine.h>

#include <iostream>
#include <algorithm>
#include <cstring>
#include <exception>
#include <sgx_tcrypto.h>
#include <cassert>

using namespace std;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
static BIGNUM *BN_lebin2bn(const unsigned char *s, int len, BIGNUM *ret)
{
    uint8_t buf[len];
    memcpy(buf, s, len);
    reverse(buf, buf+len);

    return BN_bin2bn(buf, len, ret);
}

static int BN_bn2lebinpad(const BIGNUM *a, unsigned char *to, int tolen)
{
    memset(to, 0, tolen);
    BN_bn2bin(a, to);
    if(tolen > 4)
        reverse(to, to+tolen);
    return tolen;
}


static int RSA_set0_key(RSA *r, BIGNUM *n, BIGNUM *e, BIGNUM *d)
{
    /* If the fields n and e in r are NULL, the corresponding input
     * parameters MUST be non-NULL for n and e.  d may be
     * left NULL (in case only the public key is used).
     */
    if ((r->n == NULL && n == NULL)
        || (r->e == NULL && e == NULL))
        return 0;

    if (n != NULL) {
        BN_free(r->n);
        r->n = n;
    }
    if (e != NULL) {
        BN_free(r->e);
        r->e = e;
    }
    if (d != NULL) {
        BN_free(r->d);
        r->d = d;
    }

    return 1;
}

static void RSA_get0_key(const RSA *r,
                         const BIGNUM **n, const BIGNUM **e, const BIGNUM **d)
{
    if (n != NULL)
        *n = r->n;
    if (e != NULL)
        *e = r->e;
    if (d != NULL)
        *d = r->d;
}
#endif

EC_KEY* to_ec_key(sgx_ec256_private_t* prv_key)
{
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    BIGNUM *bn_prv_r = BN_lebin2bn(prv_key->r, 32, 0);
    if(1 != EC_KEY_set_private_key(ec_key, bn_prv_r))
    {
        ERR_print_errors_fp(stdout);
        throw runtime_error("Crap");
    }

    return ec_key;
}
EC_KEY* to_ec_key(sgx_ec256_public_t* pub_key)
{
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_GROUP *curve = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);

    BIGNUM *prv = BN_new();
    EC_POINT *pub = EC_POINT_new(curve);

    BIGNUM *bn_pub_x = BN_lebin2bn(pub_key->gx, 32, 0);
    BIGNUM *bn_pub_y = BN_lebin2bn(pub_key->gy, 32, 0);

    if(1 != EC_POINT_set_affine_coordinates_GFp(curve, pub, bn_pub_x, bn_pub_y, 0))
    {
        ERR_print_errors_fp(stdout);
        throw runtime_error("Crap");
    }

    EC_KEY_set_public_key(ec_key, pub);
    ERR_print_errors_fp(stdout);
    return ec_key;
}

void gen_key(sgx_ec256_private_t* prv_key, sgx_ec256_public_t* pub_key) {
    EC_GROUP *curve;

    if (NULL == (curve = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1)))
        throw runtime_error("Crap");

    EC_KEY *key;

    if (NULL == (key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1)))
        throw runtime_error("Crap");

    if (1 != EC_KEY_generate_key(key))
        throw runtime_error("Crap");

    BIGNUM *prv = BN_new();
    BIGNUM *pub_x = BN_new();
    BIGNUM *pub_y = BN_new();

    prv = (BIGNUM*)EC_KEY_get0_private_key(key);
    const EC_POINT* pub_p = EC_KEY_get0_public_key(key);

    if(1 != EC_POINT_get_affine_coordinates_GFp(curve, pub_p, pub_x, pub_y, 0))
    {
        ERR_print_errors_fp(stdout);
        throw runtime_error("Crap");
    }

    BN_bn2lebinpad(prv, (uint8_t*)prv_key, 32);
    BN_bn2lebinpad(pub_x, (uint8_t*)pub_key->gx, 32);
    BN_bn2lebinpad(pub_y, (uint8_t*)pub_key->gy, 32);
}

uint8_t* get_shared_dhkey(sgx_ec256_private_t* prv_key, sgx_ec256_public_t* peer_key)
{
    EC_KEY *ec_key = to_ec_key(prv_key);
    EC_KEY *ec_peer = to_ec_key(peer_key);

    int secret_len = 32;
    uint8_t* secret = (uint8_t*)malloc(32);

    /* Derive the shared secret */
    secret_len = ECDH_compute_key(
            secret, secret_len, EC_KEY_get0_public_key(ec_peer), ec_key, NULL);

    /* Clean up */
    EC_KEY_free(ec_key);
    EC_KEY_free(ec_peer);

    // Big endian -> little endian, so that shared secrets match in app and enclave
    reverse(secret,secret+secret_len);
    return secret;
}
void decrypt(uint8_t* key, uint8_t* data, uint32_t data_size, uint8_t* mac, uint8_t* out_data)
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    uint8_t iv[12] = {0};
    EVP_DecryptInit(ctx, EVP_aes_128_gcm(), key, iv);

    int n_processed = 0;

    //EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL); // default: 12
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, mac);

    // Additional Authenticated Data (AAD)
    // EVP_DecryptUpdate(ctx, NULL, &n_processed, 0, 0);

    // decrypting data
    EVP_DecryptUpdate(ctx, out_data, &n_processed, data,
                      data_size);

    // authentication step
    if(1 != EVP_DecryptFinal(ctx, out_data+n_processed, &n_processed))
    {
        ERR_print_errors_fp(stdout);
        throw runtime_error("Failure");
    }
    EVP_CIPHER_CTX_free(ctx);
}

void encrypt(uint8_t* key, const uint8_t* data, uint32_t data_size, uint8_t* out_data, uint8_t* out_mac) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    uint8_t iv[12] = {0};

    int n_processed = 0;

    /* Initialise the encryption operation. */
    if(1 != EVP_EncryptInit(ctx, EVP_aes_128_gcm(), key, iv))
    {
        ERR_print_errors_fp(stdout);
        throw runtime_error("Failure");
    }

    //EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL); // default: 12

    // Additional Authenticated Data (AAD)
    // EVP_EncryptUpdate(ctx, NULL, &n_processed, 0, 0);

    if(1 != EVP_EncryptUpdate(ctx, out_data, &n_processed, data, data_size))
    {
        ERR_print_errors_fp(stdout);
        throw runtime_error("Failure");
    }

    if(1 != EVP_EncryptFinal_ex(ctx, out_data + n_processed, &n_processed))
    {
        ERR_print_errors_fp(stdout);
        throw runtime_error("Failure");
    }

    if(1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out_mac))
    {
        ERR_print_errors_fp(stdout);
        throw runtime_error("Failure");
    }

    EVP_CIPHER_CTX_free(ctx);
}

bool check_point(sgx_ec256_public_t* pub_key) {
    BN_CTX *ctx = BN_CTX_new();

    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_GROUP *curve = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);

    BIGNUM *prv = BN_new();
    EC_POINT *pub = EC_POINT_new(curve);

    BIGNUM *bn_pub_x = BN_lebin2bn(pub_key->gx, 32, 0);
    BIGNUM *bn_pub_y = BN_lebin2bn(pub_key->gy, 32, 0);

    return 1 == EC_POINT_set_affine_coordinates_GFp(curve, pub, bn_pub_x, bn_pub_y, ctx);
}
#define SSL_CHECK(call) if(-1 == (call)) { \
    ERR_print_errors_fp(stdout);    \
    throw runtime_error("Crap");    \
    }

RSA* to_rsa_ctx( sgx_rsa3072_public_key_t* pub_key )
{
    BIGNUM *bn_prv_n = BN_lebin2bn(pub_key->mod, 384, 0);
    //BIGNUM *bn_prv_e = BN_lebin2bn(pub_key->exp, 4, 0);
    BIGNUM* bn_prv_e = BN_new();
    SSL_CHECK(BN_set_word(bn_prv_e, RSA_F4));

    RSA* rsa = RSA_new();
    SSL_CHECK(RSA_set0_key(rsa, bn_prv_n, bn_prv_e, 0));

    return rsa;
}


RSA* to_rsa_ctx( sgx_rsa3072_private_key_t* prv_key )
{
    BIGNUM *bn_prv_n = BN_lebin2bn(prv_key->mod, 384, 0);
    BIGNUM* bn_e = BN_new();
    SSL_CHECK(BN_set_word(bn_e, RSA_F4));
    BIGNUM *bn_prv_d = BN_lebin2bn(prv_key->exp, 384, 0);

    RSA* rsa = RSA_new();
    SSL_CHECK(RSA_set0_key(rsa, bn_prv_n, bn_e, bn_prv_d));

    return rsa;
}

void rsa3072_create_key_pair(sgx_rsa3072_private_key_t * p_private, sgx_rsa3072_public_key_t * p_public)
{
    RSA* rsa = RSA_new();
    BIGNUM* bn_e_ = BN_new();
    SSL_CHECK(BN_set_word(bn_e_, RSA_F4));
    SSL_CHECK(RSA_generate_key_ex(rsa, 3072, bn_e_, 0));

    BN_free(bn_e_);
    const BIGNUM* bn_n;
    const BIGNUM* bn_e;
    const BIGNUM* bn_d;

    RSA_get0_key(rsa, &bn_n, &bn_e, &bn_d);

    BN_bn2lebinpad(bn_n, (uint8_t*)p_private->mod, 384);
    BN_bn2lebinpad(bn_n, (uint8_t*)p_public->mod, 384);
    BN_bn2lebinpad(bn_e, (uint8_t*)p_public->exp, 4);
    BN_bn2lebinpad(bn_d, (uint8_t*)p_private->exp, 384);

    RSA_free(rsa);
}

void rsa3072_encrypt(sgx_rsa3072_public_key_t* pub_key, const uint8_t *data, uint32_t data_size, uint8_t *out_data, uint32_t* out_size) {
    RSA* rsa = to_rsa_ctx(pub_key);

    //SSL_CHECK(RSA_check_key(rsa) - 1);

    int ret = RSA_public_encrypt(data_size, data, out_data, rsa, RSA_PKCS1_PADDING);

    SSL_CHECK(ret);

    *out_size = ret;

    //reverse(out_data, out_data+data_size);
    RSA_free(rsa);
}

void rsa3072_decrypt(sgx_rsa3072_private_key_t* prv_key, const uint8_t *data, uint32_t data_size, uint8_t *out_data) {
    RSA* rsa = to_rsa_ctx(prv_key);

    //SSL_CHECK(RSA_check_key(rsa) - 1);

    SSL_CHECK(RSA_private_decrypt(data_size, data, out_data, rsa, RSA_PKCS1_PADDING));

    RSA_free(rsa);
}
