// AES-192 view over tiny-AES-c (see NOTES.md).
#undef AES128
#undef AES256
#ifndef AES192
#define AES192 1
#endif
#define AES_init_ctx aes192_init_ctx
#define AES_init_ctx_iv aes192_init_ctx_iv
#define AES_ctx_set_iv aes192_ctx_set_iv
#define AES_ECB_encrypt aes192_ECB_encrypt
#define AES_ECB_decrypt aes192_ECB_decrypt
#define AES_CBC_encrypt_buffer aes192_CBC_encrypt_buffer
#define AES_CBC_decrypt_buffer aes192_CBC_decrypt_buffer
#define AES_CTR_xcrypt_buffer aes192_CTR_xcrypt_buffer
#include "aes.c"
