#include "MainWindow.hpp"
#include "core/MemoryCard.hpp"
#include "core/MockData.hpp"
#include "core/formats/DexDriveHandler.hpp"
#include "core/ps1/PS1MemoryCard.hpp"
#include "core/ps2/PS2MemoryCard.hpp"
#include <filesystem>
#include <format>
#include <gtk/gtk.h>

namespace cyan {

// ─── C-linkage signal callbacks (forward declarations) ────────────────────────
extern "C" {
static void cb_factory_setup         (GtkListItemFactory*, GtkListItem*, gpointer);
static void cb_factory_bind          (GtkListItemFactory*, GtkListItem*, gpointer);
static void cb_selection_changed     (GObject*, GParamSpec*, gpointer);
static void cb_import_clicked        (GtkButton*, gpointer);
static void cb_export_clicked        (GtkButton*, gpointer);
static void cb_delete_clicked        (GtkButton*, gpointer);
static void cb_open_card_clicked     (GtkButton*, gpointer);
static void cb_new_card_clicked      (GtkButton*, gpointer);
static void cb_new_card_ps2_clicked  (GtkButton*, gpointer);
static void cb_save_quit_clicked     (GtkButton*, gpointer);
static void cb_search_changed        (GtkSearchEntry*, gpointer);
static void cb_alert_delete_response          (GObject*, GAsyncResult*, gpointer);
static void cb_file_chooser_open_response    (GtkDialog*, gint, gpointer);
static void cb_file_chooser_import_response  (GtkDialog*, gint, gpointer);
static void cb_file_chooser_export_response  (GtkDialog*, gint, gpointer);
static void cb_file_chooser_new_response     (GtkDialog*, gint, gpointer);
static void cb_file_chooser_new_ps2_response (GtkDialog*, gint, gpointer);
} // extern "C"

static GdkTexture* make_fallback_texture();
static void        set_picture_icon(GtkWidget*, const std::vector<IconFrame>&, int);

// ─── Constructor / Destructor ─────────────────────────────────────────────────

MainWindow::MainWindow(GtkApplication* app) {
    m_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(m_window), "CYAN MEMORIES");
    gtk_window_set_default_size(GTK_WINDOW(m_window), 1140, 700);

    loadMockData();
    buildUI();
}

MainWindow::~MainWindow() {
    // GTK removes tick callbacks automatically when the widget is destroyed.
}

// ─── Data setup ───────────────────────────────────────────────────────────────

void MainWindow::loadMockData() {
    m_saves.clear();
    m_save_files.clear();
}

// ─── Full UI construction ─────────────────────────────────────────────────────

void MainWindow::buildUI() {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(m_window), root);

    // ── HeaderBar ─────────────────────────────────────────────────────────────
    GtkWidget* header = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(m_window), header);

    // Centered title widget
    GtkWidget* title_lbl = gtk_label_new("CYAN MEMORIES");
    gtk_widget_add_css_class(title_lbl, "neon-title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_lbl);

    // Left side buttons
    GtkWidget* hdr_open = gtk_button_new_with_label("Open Card...");
    g_signal_connect(hdr_open, "clicked", G_CALLBACK(cb_open_card_clicked), this);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), hdr_open);

    m_btn_new_card = gtk_button_new_with_label("New PS1 Card");
    g_signal_connect(m_btn_new_card, "clicked", G_CALLBACK(cb_new_card_clicked), this);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), m_btn_new_card);

    m_btn_new_card_ps2 = gtk_button_new_with_label("New PS2 Card");
    g_signal_connect(m_btn_new_card_ps2, "clicked", G_CALLBACK(cb_new_card_ps2_clicked), this);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), m_btn_new_card_ps2);

    // Right side: Save & Quit
    GtkWidget* btn_quit = gtk_button_new_with_label("Save & Quit");
    g_signal_connect(btn_quit, "clicked", G_CALLBACK(cb_save_quit_clicked), this);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), btn_quit);

    // ── Outer horizontal paned: sidebar | (list + details) ───────────────────
    GtkWidget* outer_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(outer_paned, TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(outer_paned), FALSE);
    gtk_paned_set_shrink_end_child  (GTK_PANED(outer_paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(outer_paned), FALSE);
    gtk_paned_set_resize_end_child  (GTK_PANED(outer_paned), TRUE);
    gtk_paned_set_position          (GTK_PANED(outer_paned), 220);
    gtk_paned_set_start_child(GTK_PANED(outer_paned), createLeftPanel());

    // ── Inner horizontal paned: list | details ────────────────────────────────
    GtkWidget* inner_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_shrink_start_child(GTK_PANED(inner_paned), FALSE);
    gtk_paned_set_shrink_end_child  (GTK_PANED(inner_paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(inner_paned), TRUE);
    gtk_paned_set_resize_end_child  (GTK_PANED(inner_paned), FALSE);
    gtk_paned_set_start_child(GTK_PANED(inner_paned), createCenterPanel());
    gtk_paned_set_end_child  (GTK_PANED(inner_paned), createDetailsPanel());

    gtk_paned_set_end_child(GTK_PANED(outer_paned), inner_paned);

    // ── Status bar ────────────────────────────────────────────────────────────
    m_status_label = gtk_label_new("Ready — load a memory card to begin.");
    gtk_label_set_xalign(GTK_LABEL(m_status_label), 0.0f);
    gtk_widget_add_css_class(m_status_label, "statusbar");
    gtk_widget_set_hexpand(m_status_label, TRUE);

    gtk_box_append(GTK_BOX(root), outer_paned);
    gtk_box_append(GTK_BOX(root), m_status_label);

    m_tick_id = gtk_widget_add_tick_callback(m_window, MainWindow::onTick, this, nullptr);
}

// ── Left panel ────────────────────────────────────────────────────────────────

GtkWidget* MainWindow::createLeftPanel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "sidebar");
    gtk_widget_set_size_request(box, 210, -1);

    GtkWidget* lbl_card = gtk_label_new("MEMORY CARD");
    gtk_label_set_xalign(GTK_LABEL(lbl_card), 0.0f);
    gtk_widget_add_css_class(lbl_card, "sidebar-section-label");

    GtkWidget* open_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(open_area, "open-card-area");
    gtk_widget_set_margin_top(open_area, 2);

    GtkWidget* open_btn = gtk_button_new_with_label("Open Card...");
    g_signal_connect(open_btn, "clicked", G_CALLBACK(cb_open_card_clicked), this);

    m_card_path_label = gtk_label_new("No card loaded");
    gtk_label_set_xalign(GTK_LABEL(m_card_path_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(m_card_path_label), PANGO_ELLIPSIZE_START);
    gtk_label_set_max_width_chars(GTK_LABEL(m_card_path_label), 22);
    gtk_widget_add_css_class(m_card_path_label, "card-path-label");

    gtk_box_append(GTK_BOX(open_area), open_btn);
    gtk_box_append(GTK_BOX(open_area), m_card_path_label);

    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 8);

    GtkWidget* lbl_cap = gtk_label_new("CAPACITY");
    gtk_label_set_xalign(GTK_LABEL(lbl_cap), 0.0f);
    gtk_widget_add_css_class(lbl_cap, "sidebar-section-label");

    m_card_progress = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_card_progress), 0.0);
    gtk_widget_set_margin_top(m_card_progress, 4);

    m_card_usage_label = gtk_label_new("0 / 15 blocks");
    gtk_label_set_xalign(GTK_LABEL(m_card_usage_label), 0.0f);
    gtk_widget_add_css_class(m_card_usage_label, "card-usage-label");

    gtk_box_append(GTK_BOX(box), lbl_card);
    gtk_box_append(GTK_BOX(box), open_area);
    gtk_box_append(GTK_BOX(box), sep);
    gtk_box_append(GTK_BOX(box), lbl_cap);
    gtk_box_append(GTK_BOX(box), m_card_progress);
    gtk_box_append(GTK_BOX(box), m_card_usage_label);

    return box;
}

// ── Center panel (search + GtkListView) ──────────────────────────────────────

GtkWidget* MainWindow::createCenterPanel() {
    // GtkStringList initially empty; populated in rebuildStore / applyFilter.
    auto* slist = gtk_string_list_new(nullptr);

    // gtk_single_selection_new() is "transfer full" — steals the slist ref.
    m_selection = gtk_single_selection_new(G_LIST_MODEL(slist));
    gtk_single_selection_set_autoselect(m_selection, FALSE);
    g_signal_connect(m_selection, "notify::selected",
                     G_CALLBACK(cb_selection_changed), this);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(cb_factory_setup), this);
    g_signal_connect(factory, "bind",  G_CALLBACK(cb_factory_bind),  this);

    // gtk_list_view_new() is "transfer full" for both args.
    m_list_view = gtk_list_view_new(GTK_SELECTION_MODEL(m_selection), factory);
    gtk_list_view_set_show_separators(GTK_LIST_VIEW(m_list_view), FALSE);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), m_list_view);

    // Search entry sits above the list.
    m_search_entry = gtk_search_entry_new();
    gtk_widget_add_css_class(m_search_entry, "save-search");
    gtk_widget_set_hexpand(m_search_entry, TRUE);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(m_search_entry),
                                          "Filter saves…");
    g_signal_connect(m_search_entry, "search-changed",
                     G_CALLBACK(cb_search_changed), this);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_vexpand(vbox, TRUE);
    gtk_box_append(GTK_BOX(vbox), m_search_entry);
    gtk_box_append(GTK_BOX(vbox), scroll);

    return vbox;
}

// ── Details panel ─────────────────────────────────────────────────────────────

GtkWidget* MainWindow::createDetailsPanel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "details-panel");
    gtk_widget_set_size_request(box, 255, -1);

    GtkWidget* sec_lbl = gtk_label_new("SAVE DETAILS");
    gtk_label_set_xalign(GTK_LABEL(sec_lbl), 0.0f);
    gtk_widget_add_css_class(sec_lbl, "details-section-label");

    GtkWidget* icon_frame = gtk_frame_new(nullptr);
    gtk_widget_add_css_class(icon_frame, "details-icon-area");
    gtk_widget_set_size_request(icon_frame, 64, 64);
    gtk_widget_set_halign(icon_frame, GTK_ALIGN_START);
    gtk_widget_set_margin_top(icon_frame, 10);
    gtk_widget_set_margin_bottom(icon_frame, 4);

    m_det_icon_pic = gtk_picture_new();
    gtk_widget_add_css_class(m_det_icon_pic, "det-icon-pic");
    gtk_picture_set_can_shrink(GTK_PICTURE(m_det_icon_pic), FALSE);
    gtk_widget_set_size_request(m_det_icon_pic, 64, 64);
    gtk_widget_set_halign(m_det_icon_pic, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(m_det_icon_pic, GTK_ALIGN_CENTER);
    gtk_frame_set_child(GTK_FRAME(icon_frame), m_det_icon_pic);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing   (GTK_GRID(grid), 7);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top  (grid, 10);
    gtk_widget_set_margin_bottom(grid, 4);

    auto add_row = [&](int row, const char* key, GtkWidget*& value_out) {
        GtkWidget* k = gtk_label_new(key);
        gtk_label_set_xalign(GTK_LABEL(k), 1.0f);
        gtk_widget_add_css_class(k, "details-key");

        value_out = gtk_label_new("\xe2\x80\x94");
        gtk_label_set_xalign(GTK_LABEL(value_out), 0.0f);
        gtk_label_set_selectable(GTK_LABEL(value_out), TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(value_out), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(value_out, "details-value");
        gtk_widget_set_hexpand(value_out, TRUE);

        gtk_grid_attach(GTK_GRID(grid), k,         0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), value_out, 1, row, 1, 1);
    };

    add_row(0, "Game ID", m_det_gameid);
    add_row(1, "Title",   m_det_title);
    gtk_label_set_wrap (GTK_LABEL(m_det_title), TRUE);
    gtk_label_set_lines(GTK_LABEL(m_det_title), 2);
    add_row(2, "Region",  m_det_region);
    add_row(3, "Blocks",  m_det_blocks);

    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top   (sep, 14);
    gtk_widget_set_margin_bottom(sep, 14);

    GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    m_btn_import = gtk_button_new_with_label("IMPORT");
    gtk_widget_add_css_class(m_btn_import, "btn-import");
    g_signal_connect(m_btn_import, "clicked", G_CALLBACK(cb_import_clicked), this);

    m_btn_export = gtk_button_new_with_label("EXPORT");
    gtk_widget_add_css_class(m_btn_export, "btn-export");
    gtk_widget_set_sensitive(m_btn_export, FALSE);
    g_signal_connect(m_btn_export, "clicked", G_CALLBACK(cb_export_clicked), this);

    m_btn_delete = gtk_button_new_with_label("DELETE");
    gtk_widget_add_css_class(m_btn_delete, "btn-delete");
    gtk_widget_set_sensitive(m_btn_delete, FALSE);
    g_signal_connect(m_btn_delete, "clicked", G_CALLBACK(cb_delete_clicked), this);

    gtk_box_append(GTK_BOX(btn_box), m_btn_import);
    gtk_box_append(GTK_BOX(btn_box), m_btn_export);
    gtk_box_append(GTK_BOX(btn_box), m_btn_delete);

    gtk_box_append(GTK_BOX(box), sec_lbl);
    gtk_box_append(GTK_BOX(box), icon_frame);
    gtk_box_append(GTK_BOX(box), grid);
    gtk_box_append(GTK_BOX(box), sep);
    gtk_box_append(GTK_BOX(box), btn_box);

    return box;
}

// ─── Filtering ────────────────────────────────────────────────────────────────

void MainWindow::applyFilter(const std::string& query) {
    m_search_filter = query;
    m_display_to_save.clear();

    for (std::size_t i = 0u; i < m_saves.size(); ++i) {
        if (query.empty()) {
            m_display_to_save.push_back(i);
            continue;
        }
        auto ci_contains = [&](const std::string& hay) {
            return std::search(hay.begin(), hay.end(),
                               query.begin(), query.end(),
                               [](char a, char b) {
                                   return std::tolower(static_cast<unsigned char>(a)) ==
                                          std::tolower(static_cast<unsigned char>(b));
                               }) != hay.end();
        };
        if (ci_contains(m_saves[i].title) || ci_contains(m_saves[i].game_id))
            m_display_to_save.push_back(i);
    }

    auto* slist = GTK_STRING_LIST(gtk_single_selection_get_model(m_selection));
    if (!slist) return;

    const guint n = g_list_model_get_n_items(G_LIST_MODEL(slist));
    if (n > 0) gtk_string_list_splice(slist, 0, n, nullptr);
    for (const std::size_t idx : m_display_to_save)
        gtk_string_list_append(slist, m_saves[idx].title.c_str());

    gtk_single_selection_set_selected(m_selection, GTK_INVALID_LIST_POSITION);
    updateDetails(GTK_INVALID_LIST_POSITION);
}

// ─── Public actions ───────────────────────────────────────────────────────────

void MainWindow::updateDetails(guint index) {
    m_selected = index;

    // Translate display position → actual save index.
    const std::size_t actual =
        (index != GTK_INVALID_LIST_POSITION &&
         index < static_cast<guint>(m_display_to_save.size()))
        ? m_display_to_save[index]
        : static_cast<std::size_t>(-1);
    const bool valid = (actual < m_saves.size());

    gtk_widget_set_sensitive(m_btn_export, valid);
    gtk_widget_set_sensitive(m_btn_delete, valid);

    if (!valid) {
        const char* dash = "\xe2\x80\x94";
        gtk_label_set_text(GTK_LABEL(m_det_gameid), dash);
        gtk_label_set_text(GTK_LABEL(m_det_title),  dash);
        gtk_label_set_text(GTK_LABEL(m_det_region), dash);
        gtk_label_set_text(GTK_LABEL(m_det_blocks), dash);
        gtk_picture_set_paintable(GTK_PICTURE(m_det_icon_pic), nullptr);
        m_anim_frame     = 0;
        m_last_anim_time = 0;
        return;
    }

    const auto& s = m_saves[actual];
    gtk_label_set_text(GTK_LABEL(m_det_gameid), s.game_id.c_str());
    gtk_label_set_text(GTK_LABEL(m_det_title),  s.title.c_str());
    gtk_label_set_text(GTK_LABEL(m_det_region), s.region.c_str());

    const auto blocks_str = std::format("{} block{}", s.block_count,
                                        s.block_count != 1u ? "s" : "");
    gtk_label_set_text(GTK_LABEL(m_det_blocks), blocks_str.c_str());

    m_anim_frame     = 0;
    m_last_anim_time = 0;
    if (actual < m_save_files.size())
        set_picture_icon(m_det_icon_pic, m_save_files[actual]->getIcons(), 0);

    setStatus(std::format("Selected  {}  —  {}", s.game_id, s.title).c_str());
}

void MainWindow::onImport() {
    if (!m_card) return;

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Import Save", GTK_WINDOW(m_window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",   GTK_RESPONSE_ACCEPT,
        nullptr);

    GtkFileFilter* save_f = gtk_file_filter_new();
    gtk_file_filter_set_name(save_f, "Save Files (*.mcs, *.raw, *.psu, *.max, *.cbs, *.gme, *.mcr)");
    gtk_file_filter_add_pattern(save_f, "*.mcs");
    gtk_file_filter_add_pattern(save_f, "*.raw");
    gtk_file_filter_add_pattern(save_f, "*.psu");
    gtk_file_filter_add_pattern(save_f, "*.max");
    gtk_file_filter_add_pattern(save_f, "*.cbs");
    gtk_file_filter_add_pattern(save_f, "*.gme");
    gtk_file_filter_add_pattern(save_f, "*.mcr");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), save_f);
    g_object_unref(save_f);

    GtkFileFilter* all_f = gtk_file_filter_new();
    gtk_file_filter_set_name(all_f, "All Files");
    gtk_file_filter_add_pattern(all_f, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_f);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), all_f);
    g_object_unref(all_f);

    g_signal_connect(dialog, "response", G_CALLBACK(cb_file_chooser_import_response), this);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_activate_action(GTK_WIDGET(dialog), "filechooser.location-popup", nullptr);
}

void MainWindow::onImportFromPath(const std::filesystem::path& path) {
    if (!m_card) return;
    if (path.empty()) {
        setStatus("Import failed.");
        return;
    }
    if (m_card->importSave(path)) {
        populateFromCard(*m_card);
        setStatus("Import successful.");
        showMessage("Import Successful",
                    "Successfully imported save:\n" + path.filename().string());
    } else {
        setStatus("Import failed.");
        showMessage("Import Failed",
                    "Failed to import save data.\n"
                    "The save may already exist or the card is full.");
    }
}

void MainWindow::onNewCard() {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Create New PS1 Memory Card", GTK_WINDOW(m_window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Create", GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "NewCard.mcr");
    g_signal_connect(dialog, "response", G_CALLBACK(cb_file_chooser_new_response), this);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_activate_action(GTK_WIDGET(dialog), "filechooser.location-popup", nullptr);
}

void MainWindow::onNewCardAtPath(const std::filesystem::path& path) {
    if (path.empty()) {
        setStatus("Card creation failed.");
        return;
    }
    auto card = PS1MemoryCard::createNew(path);
    if (card) {
        m_card = std::move(card);
        populateFromCard(*m_card);
    } else {
        setStatus(std::format("Failed to create: {}", path.filename().string()).c_str());
    }
}

void MainWindow::onNewCardPS2() {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Create New PS2 Memory Card", GTK_WINDOW(m_window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Create", GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "NewCard.ps2");
    g_signal_connect(dialog, "response", G_CALLBACK(cb_file_chooser_new_ps2_response), this);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_activate_action(GTK_WIDGET(dialog), "filechooser.location-popup", nullptr);
}

void MainWindow::onNewCardPS2AtPath(const std::filesystem::path& path) {
    if (path.empty()) {
        setStatus("Card creation failed.");
        return;
    }
    auto card = PS2MemoryCard::createNew(path);
    if (card) {
        m_card = std::move(card);
        populateFromCard(*m_card);
        showMessage("Format Successful", "Brand new PS2 memory card created!");
    } else {
        setStatus(std::format("Failed to create: {}", path.filename().string()).c_str());
        showMessage("Format Failed", "Failed to create PS2 memory card.");
    }
}

void MainWindow::onExport() {
    if (!m_card || m_selected == GTK_INVALID_LIST_POSITION) return;
    const std::size_t actual =
        (m_selected < static_cast<guint>(m_display_to_save.size()))
        ? m_display_to_save[m_selected]
        : static_cast<std::size_t>(-1);
    if (actual >= m_save_files.size()) return;

    if (m_card->getType() == MemoryCardType::PS1) {
        const std::string initial_name = m_save_files[actual]->getGameID() + ".raw";
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Export Save", GTK_WINDOW(m_window),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Save",   GTK_RESPONSE_ACCEPT,
            nullptr);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), initial_name.c_str());
        g_signal_connect(dialog, "response", G_CALLBACK(cb_file_chooser_export_response), this);
        gtk_window_present(GTK_WINDOW(dialog));
        gtk_widget_activate_action(GTK_WIDGET(dialog), "filechooser.location-popup", nullptr);
    } else {
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Export Save — Select Destination Folder", GTK_WINDOW(m_window),
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Select", GTK_RESPONSE_ACCEPT,
            nullptr);
        g_signal_connect(dialog, "response", G_CALLBACK(cb_file_chooser_export_response), this);
        gtk_window_present(GTK_WINDOW(dialog));
        gtk_widget_activate_action(GTK_WIDGET(dialog), "filechooser.location-popup", nullptr);
    }
}

void MainWindow::onDelete() {
    if (!m_card || m_selected == GTK_INVALID_LIST_POSITION) return;
    const std::size_t actual =
        (m_selected < static_cast<guint>(m_display_to_save.size()))
        ? m_display_to_save[m_selected]
        : static_cast<std::size_t>(-1);
    if (actual >= m_saves.size()) return;

    const std::string msg = std::format("Delete \"{}\"?", m_saves[actual].title);
    GtkAlertDialog* dlg = gtk_alert_dialog_new("%s", msg.c_str());
    gtk_alert_dialog_set_detail(dlg, "This action cannot be undone.");
    const char* buttons[] = {"Cancel", "Delete", nullptr};
    gtk_alert_dialog_set_buttons(dlg, buttons);
    gtk_alert_dialog_set_cancel_button(dlg, 0);
    gtk_alert_dialog_set_default_button(dlg, 0);

    gtk_alert_dialog_choose(dlg, GTK_WINDOW(m_window),
                            nullptr, cb_alert_delete_response, this);
    g_object_unref(dlg);
}

void MainWindow::onDeleteConfirmed() {
    if (!m_card || m_selected == GTK_INVALID_LIST_POSITION) return;
    const std::size_t actual =
        (m_selected < static_cast<guint>(m_display_to_save.size()))
        ? m_display_to_save[m_selected]
        : static_cast<std::size_t>(-1);
    if (actual >= m_saves.size()) return;

    const std::string game_id = (actual < m_save_files.size())
                                ? m_save_files[actual]->getGameID() : "";
    if (m_card->deleteSave(actual)) {
        populateFromCard(*m_card);
        setStatus(std::format("Deleted save: {}.", game_id).c_str());
        showMessage("Delete Successful", "Successfully deleted save:\n" + game_id);
    } else {
        setStatus("Delete failed.");
        showMessage("Delete Failed", "Failed to delete save data.");
    }
}

void MainWindow::onExportToPath(const std::filesystem::path& path) {
    if (!m_card || m_selected == GTK_INVALID_LIST_POSITION) return;
    if (path.empty()) { setStatus("Export failed."); return; }
    const std::size_t actual =
        (m_selected < static_cast<guint>(m_display_to_save.size()))
        ? m_display_to_save[m_selected]
        : static_cast<std::size_t>(-1);
    if (actual >= m_save_files.size()) return;

    const std::string game_id = m_save_files[actual]->getGameID();
    if (m_card->exportSave(actual, path)) {
        setStatus(std::format("Exported {} to {}.",
                              game_id, path.filename().string()).c_str());
        showMessage("Export Successful",
                    "Save data exported successfully:\n" + game_id);
    } else {
        setStatus(std::format("Export failed: unable to write {}.",
                              path.filename().string()).c_str());
        showMessage("Export Failed", "Failed to export save data.");
    }
}

void MainWindow::onOpenCard() {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Open Memory Card", GTK_WINDOW(m_window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",   GTK_RESPONSE_ACCEPT,
        nullptr);

    GtkFileFilter* gme_f = gtk_file_filter_new();
    gtk_file_filter_set_name(gme_f, "DexDrive Save (*.gme)");
    gtk_file_filter_add_pattern(gme_f, "*.gme");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), gme_f);
    g_object_unref(gme_f);

    GtkFileFilter* raw_f = gtk_file_filter_new();
    gtk_file_filter_set_name(raw_f, "Raw PS1 Memory Card (*.mcr, *.mcd)");
    gtk_file_filter_add_pattern(raw_f, "*.mcr");
    gtk_file_filter_add_pattern(raw_f, "*.mcd");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), raw_f);
    g_object_unref(raw_f);

    GtkFileFilter* ps2_f = gtk_file_filter_new();
    gtk_file_filter_set_name(ps2_f, "PS2 Memory Card (*.ps2)");
    gtk_file_filter_add_pattern(ps2_f, "*.ps2");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), ps2_f);
    g_object_unref(ps2_f);

    GtkFileFilter* all_f = gtk_file_filter_new();
    gtk_file_filter_set_name(all_f, "All Files");
    gtk_file_filter_add_pattern(all_f, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_f);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), all_f);
    g_object_unref(all_f);

    g_signal_connect(dialog, "response", G_CALLBACK(cb_file_chooser_open_response), this);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_activate_action(GTK_WIDGET(dialog), "filechooser.location-popup", nullptr);
}

void MainWindow::loadCardFromPath(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();

    std::shared_ptr<MemoryCard> card;
    bool success = false;

    if (ext == ".gme") {
        auto ps1 = std::make_shared<PS1MemoryCard>();
        success  = formats::DexDriveHandler::importFrom(path, *ps1);
        card     = std::move(ps1);
    } else if (ext == ".mcr" || ext == ".mcd") {
        auto ps1 = std::make_shared<PS1MemoryCard>();
        success  = ps1->load(path);
        card     = std::move(ps1);
    } else if (ext == ".ps2") {
        auto ps2 = std::make_shared<PS2MemoryCard>();
        success  = ps2->load(path);
        card     = std::move(ps2);
    } else {
        setStatus(std::format("Unsupported format: {} (use .gme, .mcr, .mcd, or .ps2)",
                              ext).c_str());
        return;
    }

    if (success) {
        m_card = std::move(card);
        populateFromCard(*m_card);
    } else {
        setStatus(std::format("Failed to load: {}", path.filename().string()).c_str());
    }
}

void MainWindow::populateFromCard(const MemoryCard& card) {
    m_saves.clear();
    m_save_files = card.getSaves();
    const std::string card_type = card.getType() == MemoryCardType::PS1 ? "PS1" : "PS2";
    for (const auto& sf : m_save_files) {
        m_saves.push_back({
            sf->getGameID(),
            sf->getTitle(),
            sf->getRegionString(),
            static_cast<std::uint32_t>(sf->getBlockCount()),
            card_type
        });
    }

    gtk_label_set_text(GTK_LABEL(m_card_path_label),
                       card.getPath().filename().string().c_str());

    const std::size_t used = card.getUsedBlocks();
    const std::size_t cap  = card.getCapacity();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(m_card_progress),
                                  cap > 0 ? static_cast<double>(used) / cap : 0.0);
    gtk_label_set_text(GTK_LABEL(m_card_usage_label),
                       std::format("{} / {} blocks used", used, cap).c_str());

    // Clear search so the full list is shown on card load.
    m_search_filter.clear();
    if (m_search_entry)
        gtk_editable_set_text(GTK_EDITABLE(m_search_entry), "");

    rebuildStore();
    setStatus(std::format("Loaded: {}  —  {} save{} found",
                          card.getPath().filename().string(),
                          m_saves.size(),
                          m_saves.size() != 1 ? "s" : "").c_str());
}

void MainWindow::setStatus(const char* msg) {
    if (m_status_label) gtk_label_set_text(GTK_LABEL(m_status_label), msg);
}

void MainWindow::showMessage(const std::string& title, const std::string& message) {
    GtkAlertDialog* dlg = gtk_alert_dialog_new("%s", title.c_str());
    gtk_alert_dialog_set_detail(dlg, message.c_str());
    gtk_alert_dialog_show(dlg, GTK_WINDOW(m_window));
    g_object_unref(dlg);
}

void MainWindow::rebuildStore() {
    applyFilter(m_search_filter);
}

// ─── Icon helpers ─────────────────────────────────────────────────────────────

static GdkTexture* make_fallback_texture() {
    static constexpr std::size_t N = 16u * 16u * 4u;
    static const auto px = [] {
        std::array<std::uint8_t, N> a{};
        for (std::size_t i = 0u; i < 256u; ++i) {
            a[i * 4u + 0u] = 0x1Cu; a[i * 4u + 1u] = 0x1Cu;
            a[i * 4u + 2u] = 0x1Cu; a[i * 4u + 3u] = 0xFFu;
        }
        return a;
    }();
    GBytes*     b   = g_bytes_new(px.data(), N);
    GdkTexture* tex = gdk_memory_texture_new(16, 16, GDK_MEMORY_R8G8B8A8, b, 16 * 4);
    g_bytes_unref(b);
    return tex;
}

static void set_picture_icon(GtkWidget* pic, const std::vector<IconFrame>& icons, int frame) {
    if (icons.empty()) {
        GdkTexture* tex = make_fallback_texture();
        gtk_picture_set_paintable(GTK_PICTURE(pic), GDK_PAINTABLE(tex));
        g_object_unref(tex);
        return;
    }
    const auto& f   = icons[static_cast<std::size_t>(frame) % icons.size()];
    GBytes*     b   = g_bytes_new(f.pixels.data(), f.pixels.size());
    GdkTexture* tex = gdk_memory_texture_new(16, 16, GDK_MEMORY_R8G8B8A8, b, 16 * 4);
    g_bytes_unref(b);
    gtk_picture_set_paintable(GTK_PICTURE(pic), GDK_PAINTABLE(tex));
    g_object_unref(tex);
}

// ─── Animation tick callback ──────────────────────────────────────────────────

gboolean MainWindow::onTick(GtkWidget* /*widget*/, GdkFrameClock* clock, gpointer user_data) {
    auto* win = static_cast<MainWindow*>(user_data);

    if (win->m_selected == GTK_INVALID_LIST_POSITION ||
        win->m_selected >= static_cast<guint>(win->m_display_to_save.size()))
        return G_SOURCE_CONTINUE;

    const std::size_t actual = win->m_display_to_save[win->m_selected];
    if (actual >= win->m_save_files.size()) return G_SOURCE_CONTINUE;

    const auto icons = win->m_save_files[actual]->getIcons();
    if (icons.size() <= 1u) return G_SOURCE_CONTINUE;

    const gint64 now = gdk_frame_clock_get_frame_time(clock);
    if (win->m_last_anim_time == 0) { win->m_last_anim_time = now; return G_SOURCE_CONTINUE; }
    if (now - win->m_last_anim_time < 333'000) return G_SOURCE_CONTINUE;

    win->m_last_anim_time = now;
    ++win->m_anim_frame;
    set_picture_icon(win->m_det_icon_pic, icons, win->m_anim_frame);
    return G_SOURCE_CONTINUE;
}

// ─── GTK4 signal callbacks (C linkage) ───────────────────────────────────────

extern "C" {

static void cb_factory_setup(GtkListItemFactory* /*fac*/,
                              GtkListItem*        item,
                              gpointer            /*user_data*/) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "save-entry-row");

    GtkWidget* icon_box = gtk_frame_new(nullptr);
    gtk_widget_add_css_class(icon_box, "save-icon-placeholder");
    gtk_widget_set_size_request(icon_box, 38, 38);
    gtk_widget_set_valign(icon_box, GTK_ALIGN_CENTER);

    GtkWidget* icon_pic = gtk_picture_new();
    gtk_widget_add_css_class(icon_pic, "save-icon-pic");
    gtk_picture_set_can_shrink(GTK_PICTURE(icon_pic), FALSE);
    gtk_widget_set_size_request(icon_pic, 32, 32);
    gtk_widget_set_halign(icon_pic, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon_pic, GTK_ALIGN_CENTER);
    gtk_frame_set_child(GTK_FRAME(icon_box), icon_pic);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

    GtkWidget* lbl_title  = gtk_label_new("Title");
    GtkWidget* lbl_gameid = gtk_label_new("GAME-ID");
    GtkWidget* lbl_meta   = gtk_label_new("Region · Blocks");

    gtk_label_set_xalign(GTK_LABEL(lbl_title),  0.0f);
    gtk_label_set_xalign(GTK_LABEL(lbl_gameid), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(lbl_meta),   0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(lbl_title), PANGO_ELLIPSIZE_END);

    gtk_widget_add_css_class(lbl_title,  "save-entry-title");
    gtk_widget_add_css_class(lbl_gameid, "save-entry-gameid");
    gtk_widget_add_css_class(lbl_meta,   "save-entry-meta");

    gtk_box_append(GTK_BOX(vbox), lbl_title);
    gtk_box_append(GTK_BOX(vbox), lbl_gameid);
    gtk_box_append(GTK_BOX(vbox), lbl_meta);

    gtk_box_append(GTK_BOX(row), icon_box);
    gtk_box_append(GTK_BOX(row), vbox);

    g_object_set_data(G_OBJECT(row), "icon_pic",  icon_pic);
    g_object_set_data(G_OBJECT(row), "lbl_title",  lbl_title);
    g_object_set_data(G_OBJECT(row), "lbl_gameid", lbl_gameid);
    g_object_set_data(G_OBJECT(row), "lbl_meta",   lbl_meta);

    gtk_list_item_set_child(item, row);
}

static void cb_factory_bind(GtkListItemFactory* /*fac*/,
                             GtkListItem*        item,
                             gpointer            user_data) {
    auto*       win = static_cast<MainWindow*>(user_data);
    const guint pos = gtk_list_item_get_position(item);

    // pos is a display index — translate to actual save index.
    const auto& d2s = win->displayToSave();
    if (pos >= static_cast<guint>(d2s.size())) return;
    const std::size_t actual = d2s[pos];

    const auto& saves = win->saves();
    if (actual >= saves.size()) return;
    const auto& s = saves[actual];

    GtkWidget* row = gtk_list_item_get_child(item);

    auto get_label = [&](const char* key) {
        return GTK_LABEL(g_object_get_data(G_OBJECT(row), key));
    };

    auto* icon_pic = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(row), "icon_pic"));
    if (icon_pic) {
        const auto& save_files = win->saveFiles();
        if (actual < save_files.size())
            set_picture_icon(icon_pic, save_files[actual]->getIcons(), 0);
    }

    std::string row_title = s.title;
    for (char& c : row_title) if (c == '\n') c = ' ';
    gtk_label_set_text(get_label("lbl_title"),  row_title.c_str());
    gtk_label_set_text(get_label("lbl_gameid"), s.game_id.c_str());

    const auto meta = std::format("{} \xc2\xb7 {} block{}",
                                  s.region, s.block_count,
                                  s.block_count != 1u ? "s" : "");
    gtk_label_set_text(get_label("lbl_meta"), meta.c_str());
}

static void cb_selection_changed(GObject*    sel,
                                  GParamSpec* /*pspec*/,
                                  gpointer    user_data) {
    auto* win = static_cast<MainWindow*>(user_data);
    const guint pos = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(sel));
    win->updateDetails(pos);
}

static void cb_search_changed(GtkSearchEntry* entry, gpointer user_data) {
    auto*       win  = static_cast<MainWindow*>(user_data);
    const char* text = gtk_editable_get_text(GTK_EDITABLE(entry));
    win->applyFilter(text ? text : "");
}

static void cb_import_clicked      (GtkButton*, gpointer u) { static_cast<MainWindow*>(u)->onImport();    }
static void cb_export_clicked      (GtkButton*, gpointer u) { static_cast<MainWindow*>(u)->onExport();    }
static void cb_delete_clicked      (GtkButton*, gpointer u) { static_cast<MainWindow*>(u)->onDelete();    }
static void cb_open_card_clicked   (GtkButton*, gpointer u) { static_cast<MainWindow*>(u)->onOpenCard();  }
static void cb_new_card_clicked    (GtkButton*, gpointer u) { static_cast<MainWindow*>(u)->onNewCard();   }
static void cb_new_card_ps2_clicked(GtkButton*, gpointer u) { static_cast<MainWindow*>(u)->onNewCardPS2(); }

static void cb_save_quit_clicked(GtkButton*, gpointer /*u*/) {
    g_application_quit(g_application_get_default());
}

static void cb_alert_delete_response(GObject*      source,
                                      GAsyncResult* result,
                                      gpointer      user_data) {
    GError*   error = nullptr;
    const int btn   = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source),
                                                     result, &error);
    if (error) { g_error_free(error); return; }
    if (btn == 1)
        static_cast<MainWindow*>(user_data)->onDeleteConfirmed();
}

// ── GtkFileChooserDialog response callbacks ───────────────────────────────────
// Each extracts the chosen path, destroys the dialog, then dispatches to the
// appropriate MainWindow method.

static void cb_file_chooser_open_response(GtkDialog* dialog, gint response, gpointer user_data) {
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file) {
            gchar* p = g_file_get_path(file);
            g_object_unref(file);
            if (p) {
                static_cast<MainWindow*>(user_data)->loadCardFromPath(std::filesystem::path(p));
                g_free(p);
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void cb_file_chooser_import_response(GtkDialog* dialog, gint response, gpointer user_data) {
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file) {
            gchar* p = g_file_get_path(file);
            g_object_unref(file);
            if (p) {
                static_cast<MainWindow*>(user_data)->onImportFromPath(std::filesystem::path(p));
                g_free(p);
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void cb_file_chooser_export_response(GtkDialog* dialog, gint response, gpointer user_data) {
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file) {
            gchar* p = g_file_get_path(file);
            g_object_unref(file);
            if (p) {
                static_cast<MainWindow*>(user_data)->onExportToPath(std::filesystem::path(p));
                g_free(p);
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void cb_file_chooser_new_response(GtkDialog* dialog, gint response, gpointer user_data) {
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file) {
            gchar* p = g_file_get_path(file);
            g_object_unref(file);
            if (p) {
                static_cast<MainWindow*>(user_data)->onNewCardAtPath(std::filesystem::path(p));
                g_free(p);
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void cb_file_chooser_new_ps2_response(GtkDialog* dialog, gint response, gpointer user_data) {
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file) {
            gchar* p = g_file_get_path(file);
            g_object_unref(file);
            if (p) {
                static_cast<MainWindow*>(user_data)->onNewCardPS2AtPath(std::filesystem::path(p));
                g_free(p);
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

} // extern "C"

} // namespace cyan
