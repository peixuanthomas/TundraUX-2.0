#include "debug.h"
#include "color.h"
#include "udata.h"
#include <string>
#include "crypto.h"
#include <cstdio>
#include <fstream>
#include <iostream>

void delete_file() {
    if(std::remove("user_data.dat") == 0) {
        colorcout("green", "User data file deleted successfully.\n");
    } else {
        colorcout("red", "Error deleting user data file or file does not exist.\n");
    }
}

//List the whole structure of user_data.dat for debugging
void struct_file() {
    std::ifstream in("user_data.dat", std::ios::binary);
    if (!in) {
        colorcout("red", "Error: Unable to open user_data.dat\n");
        return;
    }
    auto readString = [&](std::string& out) -> bool {
        size_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (!in) return false;
        out.resize(len);
        if (len > 0) in.read(&out[0], len);
        return in.good();
    };
    int version = 0;
    size_t userCount = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&userCount), sizeof(userCount));
    if (!in) {
        colorcout("red", "Error: Failed to read header\n");
        return;
    }
    colorcout("white", std::to_string(version) + "\n");
    colorcout("white", std::to_string(userCount) + "\n");
    for (size_t i = 0; i < userCount; ++i) {
        std::string type, name, encPass, hint;
        if (!readString(type) || !readString(name) || !readString(encPass) || !readString(hint)) {
            colorcout("red", "Error: Failed to read user block\n");
            return;
        }
        int count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in) {
            colorcout("red", "Error: Failed to read count\n");
            return;
        }
        const std::string pass = decrypt(encPass);
        colorcout("white", type + "\n");
        colorcout("white", name + "\n");
        colorcout("white", pass + "\n");
        colorcout("white", hint + "\n");
        colorcout("white", std::to_string(count) + (i + 1 == userCount ? "" : "\n"));
    }
}

void display_test() {
    colorcout("green", "Green Text\n");
    colorcout("red", "Red Text (Note: this should make a sound when displayed)\n");
    colorcout("blue", "Blue Text\n");
    colorcout("yellow", "Yellow Text\n");
    colorcout("cyan", "Cyan Text\n");
    colorcout("magenta", "Magenta Text\n");
    colorcout("white", "White Text\n");
}

void license() {
    std::ifstream licenseFile("license");
    if (!licenseFile) {
        colorcout("red", "License file not found.\n");
        return;
    }
    std::string line;
    while (std::getline(licenseFile, line)) {
        std::cout << line << std::endl;
    }
    licenseFile.close();
}