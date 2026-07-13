#pragma once

#include "core/MemoryCard.hpp"
#include "core/MockData.hpp"
#include "core/SaveFile.hpp"
#include <filesystem>
#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

namespace cyan {

class MainWindow {
public:
    explicit MainWindow(GtkApplication* app);
    ~MainWindow();

    MainWindow(const MainWindow&)            = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    GtkWidget* widget() noexcept { return m_window; }

    // Called from C signal callbacks — must be public.
    const std::vector<MockSaveInfo>&              saves()        const { return m_saves;          }
    const std::vector<std::shared_ptr<SaveFile>>& saveFiles()    const { return m_save_files;     }
    const std::vector<std::size_t>&               displayToSave() const { return m_display_to_save; }

    void updateDetails(guint index);
    void applyFilter(const std::string& query);
    void onImport();
    void onExport();
    void onDelete();
    void onOpenCard();
    void onNewCard();
    void onNewCardPS2();

    // Called from GtkFileDialog async callbacks.
    void loadCardFromPath(const std::filesystem::path& path);
    void onDeleteConfirmed();
    void onExportToPath(const std::filesystem::path& path);
    void onImportFromPath(const std::filesystem::path& path);
    void onNewCardAtPath(const std::filesystem::path& path);
    void onNewCardPS2AtPath(const std::filesystem::path& path);

private:
    void buildUI();
    GtkWidget* createLeftPanel();
    GtkWidget* createCenterPanel();
    GtkWidget* createDetailsPanel();
    void       loadMockData();
    void       setStatus(const char* msg);
    void       showMessage(const std::string& title, const std::string& message);
    void       rebuildStore();
    void       populateFromCard(const MemoryCard& card);

    static gboolean onTick(GtkWidget*, GdkFrameClock*, gpointer);

    // ── Window & containers ──────────────────────────────────────────────────
    GtkWidget* m_window{nullptr};

    // ── List view ────────────────────────────────────────────────────────────
    GtkStringList*      m_string_list{nullptr};
    GtkSingleSelection* m_selection{nullptr};
    GtkWidget*          m_list_view{nullptr};
    GtkWidget*          m_search_entry{nullptr};

    // ── Left panel widgets ───────────────────────────────────────────────────
    GtkWidget* m_card_path_label{nullptr};
    GtkWidget* m_card_progress{nullptr};
    GtkWidget* m_card_usage_label{nullptr};

    // ── Details panel widgets ────────────────────────────────────────────────
    GtkWidget* m_det_gameid{nullptr};
    GtkWidget* m_det_title{nullptr};
    GtkWidget* m_det_region{nullptr};
    GtkWidget* m_det_blocks{nullptr};
    GtkWidget* m_det_icon_pic{nullptr};
    GtkWidget* m_btn_import{nullptr};
    GtkWidget* m_btn_export{nullptr};
    GtkWidget* m_btn_delete{nullptr};
    GtkWidget* m_btn_new_card{nullptr};
    GtkWidget* m_btn_new_card_ps2{nullptr};

    // ── Status bar ───────────────────────────────────────────────────────────
    GtkWidget* m_status_label{nullptr};

    // ── Data ─────────────────────────────────────────────────────────────────
    std::shared_ptr<MemoryCard>            m_card;
    std::vector<MockSaveInfo>              m_saves;
    std::vector<std::shared_ptr<SaveFile>> m_save_files;
    std::vector<std::size_t>               m_display_to_save;
    std::string                            m_search_filter;
    guint                                  m_selected{GTK_INVALID_LIST_POSITION};

    // ── Animation state ───────────────────────────────────────────────────────
    gint64 m_last_anim_time{0};
    int    m_anim_frame{0};
    guint  m_tick_id{0};
};

} // namespace cyan
