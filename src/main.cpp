#include <iostream>
#include "hello.h"
#include "color.h"
#include "udata.h"
#include "command.h"
#include "TUXfile.h"
#include <filesystem>
#include <string>
#include <fstream>


int main(int argc, char* argv[]) {
    std::ifstream licenseFile("license");
    std::ifstream detectData("user_data.dat");
    if (licenseFile && !detectData) {
        std::string line;
        while (std::getline(licenseFile, line)) {
            std::cout << line << std::endl;
        }
        licenseFile.close();
        colorcout("yellow", "\nPress Enter to accept the license and continue...");
        std::cin.get();
        clear_screen();
        hello();
        task_main();
    } else if (detectData) {
        detectData.close();
        clear_screen();
        set_title("TundraUX 2.0");
        task_main();
    } else {
        colorcout("red", "Critical files missing! Program aborted.\n");
        pause();
        return 1;
    }
    std::cerr << "Program has run into an unexpected place.\n";
    pause();
    return 1;
}