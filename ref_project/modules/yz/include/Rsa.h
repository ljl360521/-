#ifndef RSA_H
#define RSA_H

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 将十六进制字节数组转换为十六进制字符串
 * @param hex 十六进制字节数组
 * @param length 数组长度
 * @return 返回十六进制字符串，需要调用者释放内存
 */
char *hex2str(const unsigned char *hex, int length);

/**
 * 将十六进制字符串转换为字节数组
 * @param str 十六进制字符串
 * @param length 字符串长度（必须是偶数）
 * @return 返回字节数组，需要调用者释放内存
 */
unsigned char *str2hex(const char *str, int length);

/**
 * Base64编码
 * @param input 输入数据
 * @param length 数据长度
 * @return 返回Base64编码字符串，需要调用者释放内存
 */
char *base64_encode(const unsigned char *input, int length);

/**
 * Base64解码
 * @param input Base64编码字符串
 * @param length 字符串长度
 * @return 返回解码后的字节数组，需要调用者释放内存
 */
unsigned char *base64_decode(const char *input, int length);

/**
 * 使用RSA公钥加密数据
 * @param message 要加密的明文
 * @param public_key_pem PEM格式的公钥
 * @return 返回Base64编码的密文，需要调用者释放内存
 */
char* public_encrypt(const char *message, const char *public_key_pem);

/**
 * 使用RSA公钥解密数据
 * @param base64_cipher Base64编码的密文
 * @param public_key_pem PEM格式的公钥
 * @return 返回解密后的明文，需要调用者释放内存
 */
char* public_decrypt(const char *base64_cipher, const char *public_key_pem);

#ifdef __cplusplus
}
#endif

#endif // RSA_H
