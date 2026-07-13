#include "app/Application.hpp"
#include <clocale>
#include <iostream>
#include <string_view>

bool g_verbose = false;

// Suppress known one-time allocations from system libraries that LSAN flags
// as leaks but are intentionally never freed (global caches, Wayland state, etc.).
extern "C" {
const char* __lsan_default_suppressions() {
    return "leak:libfontconfig\n"
           "leak:libglib-2.0\n"
           "leak:libwayland-client\n"
           "leak:libvulkan_radeon\n"
           "leak:libgtk-4\n"
           "leak:libpango\n";
}
}

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "");

    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]).find("-v") != std::string_view::npos) {
            g_verbose = true;
            std::cerr << "[DEBUG] Verbose logging enabled.\n" << std::flush;
            argc = 1; // hide flag from GTK's arg parser
            break;
        }
    }

    auto app = cyan::Application::create();
    return app->run(argc, argv);
}
