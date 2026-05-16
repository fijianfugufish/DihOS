#pragma once

#include <stdint.h>

void kwifi_sha1(const uint8_t *data, uint32_t len, uint8_t out[20]);
void kwifi_hmac_sha1(const uint8_t *key,
                     uint32_t key_len,
                     const uint8_t *data,
                     uint32_t data_len,
                     uint8_t out[20]);
void kwifi_wpa_prf_sha1(const uint8_t *key,
                        uint32_t key_len,
                        const char *label,
                        const uint8_t *data,
                        uint32_t data_len,
                        uint8_t *out,
                        uint32_t out_len);
void kwifi_md4(const uint8_t *data, uint32_t len, uint8_t out[16]);
void kwifi_mschapv2_nt_password_hash(const char *password, uint8_t out[16]);
void kwifi_mschapv2_hash_nt_password_hash(const uint8_t nt_hash[16], uint8_t out[16]);
void kwifi_mschapv2_challenge_hash(const uint8_t peer_challenge[16],
                                   const uint8_t authenticator_challenge[16],
                                   const char *username,
                                   uint8_t out[8]);
void kwifi_mschapv2_challenge_response(const uint8_t challenge[8],
                                       const uint8_t nt_hash[16],
                                       uint8_t out[24]);
void kwifi_mschapv2_generate_nt_response(const uint8_t authenticator_challenge[16],
                                         const uint8_t peer_challenge[16],
                                         const char *username,
                                         const char *password,
                                         uint8_t out[24]);
void kwifi_mschapv2_generate_authenticator_response(const char *password,
                                                    const uint8_t nt_response[24],
                                                    const uint8_t peer_challenge[16],
                                                    const uint8_t authenticator_challenge[16],
                                                    const char *username,
                                                    char out[43]);
