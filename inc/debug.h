#ifndef DEBUG_H
#define DEBUG_H

#include <string>

void delete_file();
void struct_file();
void display_test();
void license();

// Debug-only utilities (not exposed in help)
void dbg_env();
void dbg_hexdump();
void dbg_resetfail(const std::string& username);

#endif // !DEBUG_H