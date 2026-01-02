#ifndef CRYPTO_H
#define CRYPTO_H

#include <string>

std::string encrypt(const std::string &plaintext);
std::string decrypt(const std::string &ciphertext);

#endif // !CRYPTO_H