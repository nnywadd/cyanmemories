#pragma once

#include <gtk/gtk.h>
#include <memory>

namespace cyan {

class MainWindow;

class Application {
public:
    static std::unique_ptr<Application> create();
    ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    int run(int argc, char* argv[]);

private:
    Application();

    void onActivate();
    void loadCSS();

    static void activateCallback(GtkApplication* app, gpointer user_data);

    GtkApplication*           m_app{nullptr};
    std::unique_ptr<MainWindow> m_window;
};

} // namespace cyan
