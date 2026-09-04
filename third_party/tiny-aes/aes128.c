// AES-128 view over tiny-AES-c (see NOTES.md): same translation unit
// trick as the 192/256 siblings, distinct symbol prefixes per key size.
#undef AES192
#undef AES256
#ifndef AES128
#define AES128 1
#endif
#define AES_init_ctx aes128_init_ctx
#define AES_init_ctx_iv aes128_init_ctx_iv
#define AES_ctx_set_iv aes128_ctx_set_iv
#define AES_ECB_encrypt aes128_ECB_encrypt
#define AES_ECB_decrypt aes128_ECB_decrypt
#define AES_CBC_encrypt_buffer aes128_CBC_encrypt_buffer
#define AES_CBC_decrypt_buffer aes128_CBC_decrypt_buffer
#define AES_CTR_xcrypt_buffer aes128_CTR_xcrypt_buffer
#include "aes.c"
