#include "Rsa.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

char *hex2str(const unsigned char *hex, int length) {
    // 将十六进制转换为字符串
    char *str = (char *)malloc(length * 2 + 1);
    if (!str) {
        return nullptr;
    }
    
    for (int i = 0; i < length; i++) {
        sprintf(str + i * 2, "%02x", hex[i]);
    }
    str[length * 2] = 0;
    return str;
}

unsigned char *str2hex(const char *str, int length) {
    // 将字符串转换为十六进制
    if (length % 2 != 0) {
        return nullptr;  // 十六进制字符串长度必须是偶数
    }
    
    unsigned char *hex = (unsigned char *)malloc(length / 2);
    if (!hex) {
        return nullptr;
    }
    
    for (int i = 0; i < length / 2; i++) {
        sscanf(str + i * 2, "%02hhx", hex + i);
    }
    return hex;
}

char *base64_encode(const unsigned char *input, int length) {
    // base64编码
    BIO *bio = nullptr;
    BIO *b64 = nullptr;
    BUF_MEM *bufferPtr = nullptr;
    char *base64_text = nullptr;

    b64 = BIO_new(BIO_f_base64());
    if (!b64) {
        return nullptr;
    }
    
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_new(BIO_s_mem());
    if (!bio) {
        BIO_free(b64);
        return nullptr;
    }
    
    bio = BIO_push(b64, bio);
    
    int written = BIO_write(bio, input, length);
    if (written != length) {
        BIO_free_all(bio);
        return nullptr;
    }
    
    if (BIO_flush(bio) != 1) {
        BIO_free_all(bio);
        return nullptr;
    }
    
    BIO_get_mem_ptr(bio, &bufferPtr);
    if (!bufferPtr || bufferPtr->length == 0) {
        BIO_free_all(bio);
        return nullptr;
    }
    
    base64_text = (char *)malloc(bufferPtr->length + 1);
    if (!base64_text) {
        BIO_free_all(bio);
        return nullptr;
    }
    
    memcpy(base64_text, bufferPtr->data, bufferPtr->length);
    base64_text[bufferPtr->length] = 0;

    BIO_free_all(bio);
    return base64_text;
}

unsigned char *base64_decode(const char *input, int length) {
    // base64解码
    BIO *bio = nullptr;
    BIO *b64 = nullptr;
    unsigned char *buffer = nullptr;
    int buffer_size = 0;
    
    b64 = BIO_new(BIO_f_base64());
    if (!b64) {
        return nullptr;
    }
    
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_new_mem_buf(input, length);
    if (!bio) {
        BIO_free(b64);
        return nullptr;
    }
    
    bio = BIO_push(b64, bio);
    
    // 分配足够的缓冲区（base64解码后的大小小于等于输入大小）
    buffer_size = length;
    buffer = (unsigned char *)malloc(buffer_size);
    if (!buffer) {
        BIO_free_all(bio);
        return nullptr;
    }
    
    int decoded_len = BIO_read(bio, buffer, buffer_size);
    if (decoded_len <= 0) {
        free(buffer);
        buffer = nullptr;
    }
    
    BIO_free_all(bio);
    return buffer;
}

char* public_encrypt(const char *message, const char *public_key_pem) {
    if (!message || !public_key_pem) {
        return nullptr;
    }
    
    // 使用公钥加密
    RSA *public_rsa = nullptr;
    char *base64_encoded = nullptr;
    unsigned char *cipher_text = nullptr;
    
    BIO *bio = BIO_new_mem_buf(public_key_pem, -1);
    if (bio == nullptr) {
        std::cerr << "无法创建BIO对象" << std::endl;
        return nullptr;
    }

    public_rsa = PEM_read_bio_RSA_PUBKEY(bio, &public_rsa, nullptr, nullptr);
    BIO_free(bio);
    
    if (public_rsa == nullptr) {
        std::cerr << "无法加载RSA公钥" << std::endl;
        return nullptr;
    }
    
    int message_len = strlen(message);
    int rsa_size = RSA_size(public_rsa);
    int block_size = rsa_size - 11;  // PKCS1填充
    int num_blocks = (message_len + block_size - 1) / block_size;  // 向上取整
    
    if (block_size <= 0) {
        RSA_free(public_rsa);
        return nullptr;
    }
    
    cipher_text = (unsigned char *)malloc(rsa_size * num_blocks);
    if (!cipher_text) {
        RSA_free(public_rsa);
        return nullptr;
    }
    
    int encrypted_len = 0;
    int success = 1;
    
    for (int i = 0; i < num_blocks; i++) {
        int block_len = (i == num_blocks - 1) ? message_len - i * block_size : block_size;
        int offset = i * block_size;
        int result = RSA_public_encrypt(block_len, 
                                       (unsigned char *)(message + offset), 
                                       cipher_text + i * rsa_size, 
                                       public_rsa, 
                                       RSA_PKCS1_PADDING);
        if (result == -1) {
            std::cerr << "加密失败, 块 " << i << std::endl;
            success = 0;
            break;
        }
        encrypted_len += result;
    }
    
    if (success) {
        base64_encoded = base64_encode(cipher_text, encrypted_len);
    }
    
    free(cipher_text);
    RSA_free(public_rsa);
    
    return base64_encoded;
}

char* public_decrypt(const char *base64_cipher, const char *public_key_pem) {
    if (!base64_cipher || !public_key_pem) {
        return nullptr;
    }
    
    // 使用公钥解密
    RSA *public_rsa = nullptr;
    unsigned char *decoded_cipher = nullptr;
    unsigned char *result = nullptr;
    char *decrypted_result = nullptr;
    
    BIO *bio = BIO_new_mem_buf(public_key_pem, -1);
    if (bio == nullptr) {
        std::cerr << "无法创建BIO对象" << std::endl;
        return nullptr;
    }

    public_rsa = PEM_read_bio_RSA_PUBKEY(bio, &public_rsa, nullptr, nullptr);
    BIO_free(bio);
    
    if (public_rsa == nullptr) {
        std::cerr << "无法加载RSA公钥" << std::endl;
        return nullptr;
    }
    
    int cipher_len = strlen(base64_cipher);
    decoded_cipher = base64_decode(base64_cipher, cipher_len);
    if (!decoded_cipher) {
        RSA_free(public_rsa);
        return nullptr;
    }
    
    int block_size_public = RSA_size(public_rsa);
    int decoded_len = 0;
    
    // 计算解码后的数据长度
    // Base64解码后的大小大约是原始长度的3/4
    decoded_len = cipher_len * 3 / 4;
    if (decoded_len <= 0) {
        free(decoded_cipher);
        RSA_free(public_rsa);
        return nullptr;
    }
    
    int num_blocks = (decoded_len + block_size_public - 1) / block_size_public;
    if (num_blocks <= 0) {
        free(decoded_cipher);
        RSA_free(public_rsa);
        return nullptr;
    }
    
    result = (unsigned char *)malloc(block_size_public * num_blocks);
    if (!result) {
        free(decoded_cipher);
        RSA_free(public_rsa);
        return nullptr;
    }
    
    int decrypted_len = 0;
    int success = 1;
    
    for (int i = 0; i < num_blocks; i++) {
        int decoded_offset = i * block_size_public;
        int result_offset = i * block_size_public;
        
        // 确保不会读取超出缓冲区
        int bytes_to_process = (decoded_offset + block_size_public <= decoded_len) 
                                ? block_size_public 
                                : decoded_len - decoded_offset;
        
        int res = RSA_public_decrypt(bytes_to_process, 
                                     decoded_cipher + decoded_offset, 
                                     result + result_offset, 
                                     public_rsa, 
                                     RSA_PKCS1_PADDING);
        
        if (res > 0) {
            char* temp = (char*)realloc(decrypted_result, decrypted_len + res + 1);
            if (!temp) {
                success = 0;
                break;
            }
            decrypted_result = temp;
            memcpy(decrypted_result + decrypted_len, result + result_offset, res);
            decrypted_len += res;
            decrypted_result[decrypted_len] = '\0';
        } else {
            // 可能是最后一个块，或者解密失败
            if (i < num_blocks - 1) {
                success = 0;
                break;
            }
        }
    }
    
    free(decoded_cipher);
    free(result);
    RSA_free(public_rsa);
    
    if (!success && decrypted_result) {
        free(decrypted_result);
        decrypted_result = nullptr;
    }
    
    return decrypted_result;
}
