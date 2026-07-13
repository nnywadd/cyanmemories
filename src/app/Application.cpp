#include "Application.hpp"
#include "MainWindow.hpp"
#include <filesystem>
#include <gtk/gtk.h>

namespace cyan {

// ─── Lifecycle ────────────────────────────────────────────────────────────────

Application::Application() {
    m_app = gtk_application_new("com.johnnygentoo.cyanmemories",
                                G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(m_app, "activate", G_CALLBACK(activateCallback), this);
}

Application::~Application() {
    m_window.reset();
    if (m_app) g_object_unref(m_app);
}

std::unique_ptr<Application> Application::create() {
    return std::unique_ptr<Application>(new Application());
}

int Application::run(int argc, char* argv[]) {
    return g_application_run(G_APPLICATION(m_app), argc, argv);
}

// ─── Activate signal ──────────────────────────────────────────────────────────

void Application::activateCallback(GtkApplication* /*app*/, gpointer user_data) {
    static_cast<Application*>(user_data)->onActivate();
}

void Application::onActivate() {
    loadCSS();
    m_window = std::make_unique<MainWindow>(m_app);
    gtk_window_present(GTK_WINDOW(m_window->widget()));
}

// ─── CSS loading ──────────────────────────────────────────────────────────────

void Application::loadCSS() {
    // Dev build: style.css is copied next to the binary by CMake's configure_file.
    // Installed: binary is in bin/, CSS is in ../share/cyan-memories/style.css.
    std::filesystem::path css_path;
    try {
        const auto exe_dir = std::filesystem::canonical("/proc/self/exe").parent_path();
        const auto dev_css = exe_dir / "style.css";
        if (std::filesystem::exists(dev_css)) {
            css_path = dev_css;
        } else {
            css_path = exe_dir.parent_path() / "share" / "cyan-memories" / "style.css";
        }
    } catch (...) {
        css_path = "style.css";
    }

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, css_path.c_str());
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
}

} // namespace cyan
