#pragma once

#include <string>

std::string encrypt(const std::string &plaintext);
std::string decrypt(const std::string &ciphertext);
std::string encryptDecrypt(const std::string& input);
