#ifndef COLOR_H
#define COLOR_H

#include <string>
#include <vector>

void colorcout(const std::string& color, const std::string& message);
void rollcout(const std::string& color, const std::string& str);
bool getYN(const std::string& prompt);
void clear_screen();
void set_title(const std::string& console_title);
void Sleep(int milsec);
void pause();
std::string getHiddenInput(const std::string& prompt, char symbol);
void print_icon();
std::string readLineWithHistory(std::vector<std::string> &history, int &historyIndex);

#endif // !COLOR_H
