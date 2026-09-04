// AES-256 view over tiny-AES-c (see NOTES.md).
#undef AES128
#undef AES192
#ifndef AES256
#define AES256 1
#endif
#define AES_init_ctx aes256_init_ctx
#define AES_init_ctx_iv aes256_init_ctx_iv
#define AES_ctx_set_iv aes256_ctx_set_iv
#define AES_ECB_encrypt aes256_ECB_encrypt
#define AES_ECB_decrypt aes256_ECB_decrypt
#define AES_CBC_encrypt_buffer aes256_CBC_encrypt_buffer
#define AES_CBC_decrypt_buffer aes256_CBC_decrypt_buffer
#define AES_CTR_xcrypt_buffer aes256_CTR_xcrypt_buffer
#include "aes.c"
