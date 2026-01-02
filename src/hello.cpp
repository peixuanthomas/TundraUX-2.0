#include "hello.h"
#include <iostream>
#include "color.h"
#include <fstream>
#include "udata.h"
#include <string>
#include <cctype>
#include "crypto.h"

void hello() {
START:
    clear_screen();
    set_title("TundraUX 2.0");
    std::cout << std::endl;
    rollcout("cyan", "Welcome to TundraUX 2.0");
    std::cout << std::endl;
    Sleep(500);
    std::string username, password, password_hint;
    while(true) {
        colorcout("white", "Please enter your username:");
        getline(std::cin, username);
        if (username == "null") {
            colorcout("red", "\"null\" is a reserved username. Please choose a different username.\n");
            continue;
        }
        if (!username.empty()) {
            if(getYN("Caution: Username cannot be changed once set. Are you sure you want to proceed?")) break;
        }
        else colorcout("red", "Username cannot be empty. Please try again.\n");
    }
    while(true) {
        colorcout("white", "Please enter your password:");
        password = getHiddenInput("", '*');

        if(password.empty()) {
            colorcout("red", "Password cannot be empty. Please try again.\n");
            continue;
        }

        //Check for complexity:
        //Longer than 6 characters
        if(password.length() < 6) {
            colorcout("red", "Password must be at least 6 characters long. Please try again.\n");
            continue;
        }
        
        //Contains uppercase, lowercase, digit
        bool hasUpper = false, hasLower = false, hasDigit = false;
        for(char c : password) {
            if(std::isupper(static_cast<unsigned char>(c))) hasUpper = true;
            else if(std::islower(static_cast<unsigned char>(c))) hasLower = true;
            else if(std::isdigit(static_cast<unsigned char>(c))) hasDigit = true;
        }
        if(!(hasUpper && hasLower && hasDigit)) {
            colorcout("red", "Password must contain at least one uppercase letter, one lowercase letter, and one digit. Please try again.\n");
            continue;
        }

        std::string confirm = getHiddenInput("Please re-enter your password:", '*');
        if(password != confirm) {
            colorcout("red", "Passwords do not match. Please try again.\n");
            continue;
        }
        
        // All checks passed
        break;
    }
    std::cout << std::endl;
    while(true) {
        colorcout("white", "Please enter your password hint (leave blank if none):");
        getline(std::cin, password_hint);
        std::cout << std::endl;
        if(password_hint == password) {
            colorcout("red", "Password hint cannot be the same as the password. Please try again.\n");
            continue;
        }
        else break;
    }
    clear_screen();
    colorcout("cyan", "Please confirm your details:\n\n");
    colorcout("white", "Username: " + username + "\n");
    colorcout("white", "Password: " + password + "\n");
    colorcout("white", "Password Hint: " + (password_hint.empty() ? "(none)" : password_hint) + "\n\n");
    if(getYN("Are these details correct?")) {
        createfile();
        USER newUser;
        newUser.type = "admin";
        newUser.name = username;
        newUser.password = password;
        newUser.password_hint = password_hint;
        newUser.count = 0;
        DataManager dataManager("user_data.dat");
        dataManager.AddUser(newUser);
        dataManager.RemoveUser("null"); // Remove default user
        colorcout("green", "User created successfully!\n");
        pause();
    } else {
        colorcout("red", "User creation cancelled. Restarting process.\n");
        Sleep(1500);
        goto START;
    }
}