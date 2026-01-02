#include "crypto.h"
#include <string>
#include <vector>
#include <random>
#include <cstdint>
#include <iostream>

const std::string key = "TundraUX2";

// function to generate a keystream based on the key and desired length
std::vector<uint8_t> generate_keystream(const std::string& key0, size_t length) {
    std::seed_seq seed(key0.begin(), key0.end());
    std::mt19937 rng(seed);
    std::vector<uint8_t> keystream(length);
    for (size_t i = 0; i < length; ++i) {
        keystream[i] = static_cast<uint8_t>(rng() & 0xFF);
    }
    return keystream;
}

// encryption function: takes plaintext and key, returns ciphertext
std::string encrypt(const std::string& plaintext) {
    if (key.empty()) {
        throw std::invalid_argument("Key must not be empty.");
    }
    std::vector<uint8_t> keystream = generate_keystream(key, plaintext.size());
    std::string ciphertext;
    ciphertext.reserve(plaintext.size());
    for (size_t i = 0; i < plaintext.size(); ++i) {
        uint8_t p = static_cast<uint8_t>(plaintext[i]);
        uint8_t k = keystream[i];
        uint8_t c = (p + k) % 256; 
        ciphertext.push_back(static_cast<char>(c));
    }
    return ciphertext;
}

// decryption function: takes ciphertext and key, returns original plaintext
std::string decrypt(const std::string& ciphertext) {
    if (key.empty()) {
        throw std::invalid_argument("Key must not be empty.");
    }
    std::vector<uint8_t> keystream = generate_keystream(key, ciphertext.size());
    std::string plaintext;
    plaintext.reserve(ciphertext.size());
    for (size_t i = 0; i < ciphertext.size(); ++i) {
        uint8_t c = static_cast<uint8_t>(ciphertext[i]);
        uint8_t k = keystream[i];
        uint8_t p = (c - k + 256) % 256;
        plaintext.push_back(static_cast<char>(p));
    }
    return plaintext;
}