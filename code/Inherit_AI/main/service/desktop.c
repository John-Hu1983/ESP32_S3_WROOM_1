#include "service/desktop.h"
#include "service/ui_pidm.h"

extern const service_item_t g_service_ai;
extern const service_item_t g_service_wifi;
extern const service_item_t g_service_scan;
extern const service_item_t g_service_link;
extern const service_item_t g_service_offline;
extern const service_item_t g_service_cell;
extern const service_item_t g_service_signal;
extern const service_item_t g_service_node;
extern const service_item_t g_service_debug;
extern const service_item_t g_service_tools;
extern const service_item_t g_service_pidm;
extern const service_item_t g_service_power;

static const service_item_t* k_services[SERVICE_APP_COUNT] = {
    &g_desktop, &g_service_ai,   &g_service_wifi,   &g_service_scan, &g_service_link,
    &g_service_offline, &g_service_cell, &g_service_signal, &g_service_node, &g_service_debug,
    &g_service_tools,   &g_service_pidm, &g_service_power,
};

#define PIDM_SERVICE_INDEX 11

static const char* k_desktop_icons[DESKTOP_CONTROL_COUNT] = {
    MATERIAL_SYMBOLS_PHOTO_CAMERA, MATERIAL_SYMBOLS_IMAGE,    MATERIAL_SYMBOLS_MUSIC_NOTE,
    MATERIAL_SYMBOLS_EXPLORE,      MATERIAL_SYMBOLS_WIFI,     MATERIAL_SYMBOLS_BLUETOOTH,
    MATERIAL_SYMBOLS_SD_CARD,      MATERIAL_SYMBOLS_MIC,      MATERIAL_SYMBOLS_MEMORY,
    MATERIAL_SYMBOLS_REFRESH,      MATERIAL_SYMBOLS_SETTINGS, MATERIAL_SYMBOLS_POWER_SETTINGS_NEW,
};

static lv_coord_t k_desktop_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                         LV_GRID_TEMPLATE_LAST};
static lv_coord_t k_desktop_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                         LV_GRID_TEMPLATE_LAST};

static void desktop_apply_selection(desktop_runtime_t* runtime);

/* ==================== private functions (static) ==================== */

/*
 * brief  : Find the best parent layer to host desktop controls.
 * input  : screen - Active screen object; screen_width - Display width;
 *          screen_height - Display height.
 * output : Candidate parent object, or null pointer when screen is invalid.
 * type   : private
 */
static lv_obj_t* desktop_find_layer_parent(lv_obj_t* screen, lv_coord_t screen_width,
                                           lv_coord_t screen_height) {
    uint32_t child_count;
    uint32_t i;
    lv_obj_t* best_parent = screen;
    int32_t best_area = 0;

    if (screen == 0) {
        return 0;
    }

    lv_obj_update_layout(screen);
    child_count = lv_obj_get_child_cnt(screen);
    for (i = 0; i < child_count; ++i) {
        lv_obj_t* child = lv_obj_get_child(screen, i);
        lv_area_t coords;
        lv_coord_t w;
        lv_coord_t h;
        int32_t area;

        if (child == 0 || !lv_obj_is_valid(child) || lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }

        lv_obj_get_coords(child, &coords);
        w = (lv_coord_t)(coords.x2 - coords.x1 + 1);
        h = (lv_coord_t)(coords.y2 - coords.y1 + 1);
        if (w < (screen_width * 3) / 4 || h < (screen_height * 3) / 4) {
            continue;
        }

        area = (int32_t)w * (int32_t)h;
        if (area > best_area) {
            best_area = area;
            best_parent = child;
        }
    }

    return best_parent;
}

/*
 * brief  : Walk up to a parent that does not apply LVGL layout rules.
 * input  : candidate - Preferred parent; screen - Active screen fallback.
 * output : Absolute-position-safe parent object.
 * type   : private
 */
static lv_obj_t* desktop_resolve_absolute_parent(lv_obj_t* candidate, lv_obj_t* screen) {
    lv_obj_t* node = candidate;

    if (node == 0) {
        node = screen;
    }

    while (node != 0 && lv_obj_get_parent(node) != 0) {
        uint32_t layout = lv_obj_get_style_layout(node, 0);
        if (layout == LV_LAYOUT_NONE) {
            break;
        }
        node = lv_obj_get_parent(node);
    }

    if (node == 0) {
        node = screen;
    }
    return node;
}

/*
 * brief  : Recursively detect top and bottom reserved bar heights.
 * input  : node - Current object node; screen_width - Display width;
 *          screen_height - Display height; top_reserved - top height output pointer;
 *          bottom_reserved - bottom height output pointer.
 * output : None.
 * type   : private
 */
static void desktop_resolve_bar_reserve_walk(lv_obj_t* node, lv_coord_t screen_width,
                                             lv_coord_t screen_height, lv_coord_t* top_reserved,
                                             lv_coord_t* bottom_reserved) {
    uint32_t child_count;
    uint32_t i;
    lv_area_t coords;
    lv_coord_t w;
    lv_coord_t h;

    if (node == 0 || !lv_obj_is_valid(node) || lv_obj_has_flag(node, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_obj_get_coords(node, &coords);
    w = (lv_coord_t)(coords.x2 - coords.x1 + 1);
    h = (lv_coord_t)(coords.y2 - coords.y1 + 1);
    if (w >= (screen_width * 3) / 4 && h > 0 && h < (screen_height / 2)) {
        if (coords.y1 <= 1 && h > *top_reserved) {
            *top_reserved = h;
        }
        if (coords.y2 >= (screen_height - 1) && h > *bottom_reserved) {
            *bottom_reserved = h;
        }
    }

    child_count = lv_obj_get_child_cnt(node);
    for (i = 0; i < child_count; ++i) {
        desktop_resolve_bar_reserve_walk(lv_obj_get_child(node, i), screen_width, screen_height,
                                         top_reserved, bottom_reserved);
    }
}

/*
 * brief  : Return first selectable app index in desktop list.
 * input  : None.
 * output : First selectable service index or DESKTOP_NO_SELECTION.
 * type   : private
 */
static int first_app_index(void) {
    return desktop_get_count() > 1 ? 1 : DESKTOP_NO_SELECTION;
}

/*
 * brief  : Return last selectable app index in desktop list.
 * input  : None.
 * output : Last selectable service index or DESKTOP_NO_SELECTION.
 * type   : private
 */
static int last_app_index(void) {
    int count = desktop_get_count();
    return count > 1 ? count - 1 : DESKTOP_NO_SELECTION;
}

/*
 * brief  : Convert service list index to desktop tile index.
 * input  : service_index - Service index in global list.
 * output : Tile index in desktop grid.
 * type   : private
 */
static int service_index_to_tile_index(int service_index) { return service_index - 1; }

/*
 * brief  : Normalize selected index into valid app range.
 * input  : runtime - Desktop runtime state pointer.
 * output : None.
 * type   : private
 */
static void normalize_selected(desktop_runtime_t* runtime) {
    int first_index;
    int last_index;

    if (runtime == 0) {
        return;
    }

    first_index = first_app_index();
    last_index = last_app_index();

    if (first_index == DESKTOP_NO_SELECTION || last_index == DESKTOP_NO_SELECTION) {
        runtime->state.selected_service_index = DESKTOP_NO_SELECTION;
        runtime->state.selected_since_ms = 0;
        return;
    }

    if (runtime->state.selected_service_index == DESKTOP_NO_SELECTION) {
        runtime->state.selected_since_ms = 0;
        return;
    }

    if (runtime->state.selected_service_index < first_index ||
        runtime->state.selected_service_index > last_index) {
        runtime->state.selected_service_index = DESKTOP_NO_SELECTION;
        runtime->state.selected_since_ms = 0;
    }
}

/*
 * brief  : Get current monotonic time in milliseconds.
 * input  : None.
 * output : Current time in ms.
 * type   : private
 */
static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

/*
 * brief  : Update selection activity timestamp for timeout tracking.
 * input  : runtime - Desktop runtime state pointer.
 * output : None.
 * type   : private
 */
static void desktop_mark_selection_activity(desktop_runtime_t* runtime) {
    if (runtime == 0) {
        return;
    }

    if (runtime->state.selected_service_index == DESKTOP_NO_SELECTION) {
        runtime->state.selected_since_ms = 0;
        return;
    }

    runtime->state.selected_since_ms = now_ms();
}

/*
 * brief  : Clear selected icon state in runtime and UI.
 * input  : runtime - Desktop runtime state pointer.
 * output : None.
 * type   : private
 */
static void desktop_clear_selection(desktop_runtime_t* runtime) {
    if (runtime == 0) {
        return;
    }

    runtime->state.selected_service_index = DESKTOP_NO_SELECTION;
    runtime->state.selected_since_ms = 0;
    desktop_apply_selection(runtime);
}

/*
 * brief  : Check whether current selection has exceeded timeout window.
 * input  : runtime - Desktop runtime state pointer.
 * output : true when selected icon should expire; otherwise false.
 * type   : private
 */
static bool desktop_selection_timed_out(const desktop_runtime_t* runtime) {
    if (runtime == 0 || runtime->state.selected_service_index == DESKTOP_NO_SELECTION ||
        runtime->state.selected_since_ms == 0) {
        return false;
    }

    return (now_ms() - runtime->state.selected_since_ms) >= DESKTOP_SELECTION_TIMEOUT_MS;
}

/*
 * brief  : Compute queue wait ticks until selection timeout deadline.
 * input  : runtime - Desktop runtime state pointer.
 * output : portMAX_DELAY when no active selection; 0 when already timed out;
 *          otherwise positive wait ticks until timeout.
 * type   : private
 */
static TickType_t desktop_selection_wait_ticks(const desktop_runtime_t* runtime) {
    uint64_t elapsed_ms;
    uint64_t remain_ms;
    TickType_t wait_ticks;

    if (runtime == 0 || runtime->state.selected_service_index == DESKTOP_NO_SELECTION ||
        runtime->state.selected_since_ms == 0) {
        return portMAX_DELAY;
    }

    elapsed_ms = now_ms() - runtime->state.selected_since_ms;
    if (elapsed_ms >= DESKTOP_SELECTION_TIMEOUT_MS) {
        return 0;
    }

    remain_ms = DESKTOP_SELECTION_TIMEOUT_MS - elapsed_ms;
    if (remain_ms > UINT32_MAX) {
        remain_ms = UINT32_MAX;
    }

    wait_ticks = pdMS_TO_TICKS((uint32_t)remain_ms);
    if (wait_ticks == 0) {
        wait_ticks = 1;
    }
    return wait_ticks;
}

/*
 * brief  : Clear cached desktop layer pointer and tile references.
 * input  : runtime - Desktop runtime state pointer.
 * output : None.
 * type   : private
 */
static void desktop_reset_layer_state(desktop_runtime_t* runtime) {
    if (runtime == 0) {
        return;
    }

    runtime->desktop_layer = 0;
    runtime->desktop_tile_count = 0;
    memset(runtime->desktop_tiles, 0, sizeof(runtime->desktop_tiles));
}

/*
 * brief  : Apply selected-state style to all desktop tiles.
 * input  : runtime - Desktop runtime state pointer.
 * output : None.
 * type   : private
 */
static void desktop_apply_selection_locked(desktop_runtime_t* runtime) {
    int selected_tile_index;
    uint8_t i;

    if (runtime == 0 || runtime->desktop_tile_count == 0) {
        return;
    }

    normalize_selected(runtime);
    selected_tile_index = -1;
    if (runtime->state.selected_service_index != DESKTOP_NO_SELECTION) {
        selected_tile_index = service_index_to_tile_index(runtime->state.selected_service_index);
    }

    for (i = 0; i < runtime->desktop_tile_count; ++i) {
        lv_obj_t* tile = (lv_obj_t*)runtime->desktop_tiles[i];
        if (tile == 0 || !lv_obj_is_valid(tile)) {
            continue;
        }

        if ((int)i == selected_tile_index) {
            lv_obj_add_state(tile, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(tile, LV_STATE_CHECKED);
        }
    }
}

/*
 * brief  : Show or hide desktop layer while LVGL lock is already held.
 * input  : runtime - Desktop runtime state pointer; visible - true to show.
 * output : None.
 * type   : private
 */
static void desktop_set_visible_locked(desktop_runtime_t* runtime, bool visible) {
    lv_obj_t* layer;

    if (runtime == 0 || runtime->desktop_layer == 0) {
        return;
    }

    layer = (lv_obj_t*)runtime->desktop_layer;
    if (!lv_obj_is_valid(layer)) {
        desktop_reset_layer_state(runtime);
        return;
    }

    if (visible) {
        lv_obj_remove_flag(layer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
    }
}

/*
 * brief  : Show or hide desktop layer with LVGL lock management.
 * input  : runtime - Desktop runtime state pointer; visible - true to show.
 * output : None.
 * type   : private
 */
static void desktop_set_visible(desktop_runtime_t* runtime, bool visible) {
    if (runtime == 0) {
        return;
    }

    if (!lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
        ESP_LOGW(DESKTOP_LOG_TAG, "Failed to lock LVGL for desktop visibility update");
        return;
    }
    desktop_set_visible_locked(runtime, visible);
    lvgl_port_unlock();
}

/*
 * brief  : Resolve top and bottom reserved heights for chrome bars.
 * input  : screen - Active screen; screen_width - Display width;
 *          screen_height - Display height; top_reserved - top output pointer;
 *          bottom_reserved - bottom output pointer.
 * output : None.
 * type   : private
 */
static void desktop_resolve_bar_reserve(lv_obj_t* screen, lv_coord_t screen_width,
                                        lv_coord_t screen_height, lv_coord_t* top_reserved,
                                        lv_coord_t* bottom_reserved) {
    if (screen == 0 || top_reserved == 0 || bottom_reserved == 0) {
        return;
    }

    *top_reserved = 0;
    *bottom_reserved = 0;

    lv_obj_update_layout(screen);
    desktop_resolve_bar_reserve_walk(screen, screen_width, screen_height, top_reserved,
                                     bottom_reserved);

    if (*top_reserved == 0) {
        *top_reserved = LV_DPX(20);
    }
    if (*bottom_reserved == 0) {
        *bottom_reserved = LV_DPX(20);
    }
}

/*
 * brief  : Build or refresh desktop control grid layer.
 * input  : runtime - Desktop runtime state pointer.
 * output : None.
 * type   : private
 */
static void desktop_create_controls(desktop_runtime_t* runtime) {
    lv_display_t* display;
    lv_obj_t* layer;
    lv_obj_t* layer_parent;
    lv_area_t parent_coords;
    lv_coord_t screen_width;
    lv_coord_t screen_height;
    lv_coord_t layer_width;
    lv_coord_t layer_height;
    lv_coord_t layer_x;
    lv_coord_t layer_y;
    lv_coord_t parent_width;
    lv_coord_t parent_height;
    lv_obj_t* screen;
    lv_coord_t top_reserved;
    lv_coord_t bottom_reserved;
    int i;
    int control_count;

    if (runtime == 0) {
        return;
    }

    if (!lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
        ESP_LOGW(DESKTOP_LOG_TAG, "Failed to lock LVGL for desktop creation");
        return;
    }

    display = lv_display_get_default();
    if (display == 0) {
        lvgl_port_unlock();
        return;
    }

    screen_width = lv_display_get_horizontal_resolution(display);
    screen_height = lv_display_get_vertical_resolution(display);
    screen = lv_screen_active();
    layer_parent = desktop_find_layer_parent(screen, screen_width, screen_height);
    layer_parent = desktop_resolve_absolute_parent(layer_parent, screen);
    if (layer_parent == 0) {
        layer_parent = screen;
    }

    top_reserved = 0;
    bottom_reserved = 0;
    desktop_resolve_bar_reserve(screen, screen_width, screen_height, &top_reserved,
                                &bottom_reserved);

    layer_width = screen_width - (DESKTOP_LAYER_MARGIN_X * 2);
    layer_height = screen_height - top_reserved - bottom_reserved;
    layer_x = 0;
    layer_y = top_reserved;

    lv_obj_get_coords(layer_parent, &parent_coords);
    layer_x = (lv_coord_t)(layer_x - parent_coords.x1);
    layer_y = (lv_coord_t)(layer_y - parent_coords.y1);
    if (layer_x < 0) {
        layer_x = 0;
    }
    if (layer_y < 0) {
        layer_y = 0;
    }

    parent_width = lv_obj_get_width(layer_parent);
    parent_height = lv_obj_get_height(layer_parent);
    if (parent_width <= 0) {
        parent_width = screen_width;
    }
    if (parent_height <= 0) {
        parent_height = screen_height;
    }

    if (layer_width > (parent_width - layer_x)) {
        layer_width = parent_width - layer_x;
    }
    if (layer_height > (parent_height - layer_y)) {
        layer_height = parent_height - layer_y;
    }

    ESP_LOGI(DESKTOP_LOG_TAG, "desktop parent layout=%u coords=(%d,%d)-(%d,%d)",
             (unsigned int)lv_obj_get_style_layout(layer_parent, 0), (int)parent_coords.x1,
             (int)parent_coords.y1, (int)parent_coords.x2, (int)parent_coords.y2);

    ESP_LOGI(DESKTOP_LOG_TAG,
             "desktop layout screen=%dx%d top=%d bottom=%d pos=(%d,%d) size=%dx%d parent=%dx%d",
             (int)screen_width, (int)screen_height, (int)top_reserved, (int)bottom_reserved,
             (int)layer_x, (int)layer_y, (int)layer_width, (int)layer_height, (int)parent_width,
             (int)parent_height);

    if (runtime->desktop_layer != 0) {
        layer = (lv_obj_t*)runtime->desktop_layer;
        if (lv_obj_is_valid(layer) && lv_obj_get_parent(layer) == layer_parent) {
            lv_obj_set_size(layer, layer_width, layer_height);
            lv_obj_set_pos(layer, layer_x, layer_y);
            desktop_set_visible_locked(runtime, true);
            desktop_apply_selection_locked(runtime);
            lvgl_port_unlock();
            return;
        }

        if (lv_obj_is_valid(layer)) {
            lv_obj_del(layer);
        }
        desktop_reset_layer_state(runtime);
    }

    if (layer_width < 80) {
        layer_width = parent_width;
    }
    if (layer_height < 96) {
        lv_coord_t available_height = parent_height - layer_y;
        if (available_height <= 0) {
            available_height = 1;
        }
        if (available_height >= 96) {
            layer_height = 96;
        } else {
            layer_height = available_height;
        }
    }

    layer = lv_obj_create(layer_parent);
    desktop_reset_layer_state(runtime);
    runtime->desktop_layer = layer;

    lv_obj_set_size(layer, layer_width, layer_height);
    lv_obj_set_pos(layer, layer_x, layer_y);
    lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(layer, lv_color_hex(UBUNTU_SURFACE_HEX), 0);
    lv_obj_set_style_border_width(layer, 1, 0);
    lv_obj_set_style_border_color(layer, lv_color_hex(UBUNTU_ACCENT_HEX), 0);
    lv_obj_set_style_border_opa(layer, LV_OPA_30, 0);
    lv_obj_set_style_radius(layer, 8, 0);
    lv_obj_set_style_pad_all(layer, DESKTOP_LAYER_PAD, 0);
    lv_obj_set_style_pad_row(layer, DESKTOP_GRID_GAP, 0);
    lv_obj_set_style_pad_column(layer, DESKTOP_GRID_GAP, 0);
    lv_obj_set_scrollbar_mode(layer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_grid_dsc_array(layer, k_desktop_col_dsc, k_desktop_row_dsc);

    control_count = desktop_get_count() - 1;
    if (control_count > DESKTOP_CONTROL_COUNT) {
        control_count = DESKTOP_CONTROL_COUNT;
    }

    for (i = 0; i < control_count; ++i) {
        lv_obj_t* tile = lv_obj_create(layer);
        lv_obj_t* icon_label;
        lv_obj_t* text_label;
        const service_item_t* item = desktop_get_item(i + 1);
        const char* name = (item != 0 && item->name != 0) ? item->name : "App";
        int row = i / DESKTOP_GRID_COLS;
        int col = i % DESKTOP_GRID_COLS;

        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_set_style_radius(tile, 10, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(UBUNTU_CARD_HEX), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(tile, lv_color_hex(UBUNTU_ACCENT_HEX), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 1, LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(tile, lv_color_hex(UBUNTU_ACCENT_HEX), LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(tile, lv_color_hex(0xFFFFFF), LV_STATE_CHECKED);
        lv_obj_set_style_border_opa(tile, LV_OPA_40, LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_margin_all(tile, DESKTOP_TILE_MARGIN, 0);
        lv_obj_set_style_pad_all(tile, 4, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        icon_label = lv_label_create(tile);
        lv_label_set_text(icon_label, k_desktop_icons[i]);
        lv_obj_set_style_text_color(icon_label, lv_color_hex(UBUNTU_TEXT_HEX), 0);
        lv_obj_set_style_text_font(icon_label, &BUILTIN_ICON_FONT, 0);
        lv_obj_align(icon_label, LV_ALIGN_TOP_MID, 0, 4);

        text_label = lv_label_create(tile);
        lv_label_set_text(text_label, name);
        lv_obj_set_width(text_label, lv_pct(100));
        lv_label_set_long_mode(text_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(text_label, &BUILTIN_TEXT_FONT, 0);
        lv_obj_set_style_text_color(text_label, lv_color_hex(UBUNTU_TEXT_HEX), 0);
        lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(text_label, LV_ALIGN_BOTTOM_MID, 0, -4);

        runtime->desktop_tiles[i] = tile;
        runtime->desktop_tile_count++;
    }

    desktop_apply_selection_locked(runtime);
    lvgl_port_unlock();
}

/*
 * brief  : Refresh visual selected-state with LVGL lock management.
 * input  : runtime - Desktop runtime state pointer.
 * output : None.
 * type   : private
 */
static void desktop_apply_selection(desktop_runtime_t* runtime) {
    if (runtime == 0) {
        return;
    }

    if (!lvgl_port_lock(DESKTOP_LVGL_LOCK_TIMEOUT_MS)) {
        ESP_LOGW(DESKTOP_LOG_TAG, "Failed to lock LVGL for selection update");
        return;
    }
    desktop_apply_selection_locked(runtime);
    lvgl_port_unlock();
}

/*
 * brief  : Match key event to current service key bindings.
 * input  : service_index - Current service index; key_index - Physical key id;
 *          event_type - Key event type.
 * output : service_key_result_t with consumed flag and command.
 * type   : private
 */
static service_key_result_t handle_service_key(int service_index, uint8_t key_index,
                                               uint8_t event_type) {
    service_key_result_t result = {0};
    const service_item_t* item = desktop_get_item(service_index);
    uint8_t i;

    if (item == 0 || item->bindings == 0 || item->binding_count == 0) {
        return result;
    }

    for (i = 0; i < item->binding_count; ++i) {
        const service_key_binding_t* binding = &item->bindings[i];
        if (binding->key_index != key_index || binding->event_type != event_type) {
            continue;
        }

        result.consumed = true;
        result.command = binding->command;
        result.notification = binding->notification;
        return result;
    }

    return result;
}

/*
 * brief  : Apply selection and show selected app name notification.
 * input  : runtime - Desktop runtime state pointer.
 * output : None.
 * type   : private
 */
static void show_selected_service(desktop_runtime_t* runtime) {
    const service_item_t* item;

    if (runtime == 0 || runtime->ops.show_notification == 0) {
        return;
    }

    desktop_apply_selection(runtime);

    item = desktop_get_item(runtime->state.selected_service_index);
    if (item == 0 || item->name == 0) {
        return;
    }

    runtime->ops.show_notification(runtime->ops.ctx, item->name, DESKTOP_SELECT_NOTIFICATION_MS);
}

/*
 * brief  : Switch runtime back to desktop home mode.
 * input  : runtime - Desktop runtime state pointer.
 * output : None.
 * type   : private
 */
static void desktop_switch_to_home(desktop_runtime_t* runtime) {
    if (runtime == 0) {
        return;
    }

    if (runtime->state.current_service_index == PIDM_SERVICE_INDEX) {
        ui_pidm_on_leave();
    }

    runtime->state.current_service_index = -1;
    normalize_selected(runtime);
    desktop_mark_selection_activity(runtime);
    desktop_create_controls(runtime);
    desktop_apply_selection(runtime);
    desktop_set_visible(runtime, true);
}

/*
 * brief  : Detect dual-click gesture to exit app and return home.
 * input  : runtime - Desktop runtime state pointer; key_index - Physical key id;
 *          event_type - Key event type.
 * output : true if gesture handled; otherwise false.
 * type   : private
 */
static bool handle_dual_click_exit(desktop_runtime_t* runtime, uint8_t key_index,
                                   uint8_t event_type) {
    uint64_t now;
    uint8_t other_key;

    if (runtime == 0 || runtime->state.current_service_index < 0 ||
        event_type != SERVICE_KEY_EVENT_CLICK) {
        return false;
    }

    if (key_index != DESKTOP_PRIMARY_KEY && key_index != DESKTOP_SECONDARY_KEY) {
        return false;
    }

    now = now_ms();
    other_key = key_index == DESKTOP_PRIMARY_KEY ? DESKTOP_SECONDARY_KEY : DESKTOP_PRIMARY_KEY;
    runtime->state.last_click_ms[key_index] = now;

    if (runtime->state.last_click_ms[other_key] == 0 ||
        (now - runtime->state.last_click_ms[other_key]) > DESKTOP_DUAL_CLICK_WINDOW_MS) {
        return false;
    }

    runtime->state.last_click_ms[0] = 0;
    runtime->state.last_click_ms[1] = 0;
    desktop_switch_to_home(runtime);

    if (runtime->ops.enter_desktop != 0) {
        runtime->ops.enter_desktop(runtime->ops.ctx, true);
    }
    return true;
}

/*
 * brief  : Dispatch one service command to upper-layer callback.
 * input  : runtime - Desktop runtime state pointer; command - Service command.
 * output : None.
 * type   : private
 */
static void run_service_command(desktop_runtime_t* runtime, service_command_t command) {
    if (runtime == 0 || runtime->ops.run_command == 0) {
        return;
    }
    runtime->ops.run_command(runtime->ops.ctx, command);
}

/*
 * brief  : Process key events while user stays at desktop home.
 * input  : runtime - Desktop runtime state pointer; key_index - Physical key id;
 *          event_type - Key event type.
 * output : None.
 * type   : private
 */
static void process_home_key(desktop_runtime_t* runtime, uint8_t key_index,
                             uint8_t event_type) {
    int first_index;
    int last_index;

    if (runtime == 0 || desktop_get_count() <= 1) {
        return;
    }

    normalize_selected(runtime);
    first_index = first_app_index();
    last_index = last_app_index();

    if (key_index == DESKTOP_PRIMARY_KEY && event_type == SERVICE_KEY_EVENT_CLICK) {
        if (runtime->state.selected_service_index == DESKTOP_NO_SELECTION) {
            runtime->state.selected_service_index = first_index;
        } else if (runtime->state.selected_service_index <= first_index) {
            runtime->state.selected_service_index = last_index;
        } else {
            runtime->state.selected_service_index--;
        }
        desktop_mark_selection_activity(runtime);
        show_selected_service(runtime);
        return;
    }

    if (key_index == DESKTOP_SECONDARY_KEY && event_type == SERVICE_KEY_EVENT_CLICK) {
        if (runtime->state.selected_service_index == DESKTOP_NO_SELECTION) {
            runtime->state.selected_service_index = first_index;
        } else if (runtime->state.selected_service_index >= last_index) {
            runtime->state.selected_service_index = first_index;
        } else {
            runtime->state.selected_service_index++;
        }
        desktop_mark_selection_activity(runtime);
        show_selected_service(runtime);
        return;
    }

    if ((key_index == DESKTOP_PRIMARY_KEY || key_index == DESKTOP_SECONDARY_KEY) &&
        event_type == SERVICE_KEY_EVENT_LONG_PRESS) {
        if (runtime->state.selected_service_index == DESKTOP_NO_SELECTION) {
            return;
        }
        runtime->state.current_service_index = runtime->state.selected_service_index;
        runtime->state.selected_since_ms = 0;
        runtime->state.last_click_ms[0] = 0;
        runtime->state.last_click_ms[1] = 0;
        desktop_set_visible(runtime, false);

        if (runtime->state.current_service_index == PIDM_SERVICE_INDEX) {
            ui_pidm_on_enter();
        }

        if (runtime->ops.enter_service != 0) {
            runtime->ops.enter_service(runtime->ops.ctx, runtime->state.current_service_index);
        }
    }
}

/*
 * brief  : Process key events while running inside a selected app.
 * input  : runtime - Desktop runtime state pointer; key_index - Physical key id;
 *          event_type - Key event type.
 * output : None.
 * type   : private
 */
static void process_service_key(desktop_runtime_t* runtime, uint8_t key_index,
                                uint8_t event_type) {
    service_key_result_t result;

    if (runtime == 0) {
        return;
    }

    if (handle_dual_click_exit(runtime, key_index, event_type)) {
        return;
    }

    result = handle_service_key(runtime->state.current_service_index, key_index, event_type);
    if (result.consumed) {
        if (runtime->state.current_service_index == PIDM_SERVICE_INDEX) {
            ui_pidm_on_key_event(key_index, event_type);
        }

        if (result.command == SERVICE_CMD_ENTER_DESKTOP) {
            desktop_switch_to_home(runtime);
        }

        if (result.notification != 0 && result.notification[0] != '\0' &&
            runtime->ops.show_notification != 0) {
            runtime->ops.show_notification(runtime->ops.ctx, result.notification,
                                           DESKTOP_ACTION_NOTIFICATION_MS);
        }
        run_service_command(runtime, result.command);
        return;
    }

    if (runtime->state.current_service_index != DESKTOP_AI_SERVICE_INDEX) {
        return;
    }

    if (key_index == DESKTOP_PRIMARY_KEY && event_type == SERVICE_KEY_EVENT_CLICK) {
        run_service_command(runtime, SERVICE_CMD_TOGGLE_CHAT);
    } else if (key_index == DESKTOP_SECONDARY_KEY && event_type == SERVICE_KEY_EVENT_PRESS_DOWN) {
        run_service_command(runtime, SERVICE_CMD_START_LISTENING);
    } else if (key_index == DESKTOP_SECONDARY_KEY && event_type == SERVICE_KEY_EVENT_PRESS_UP) {
        run_service_command(runtime, SERVICE_CMD_STOP_LISTENING);
    }
}

/*
 * brief  : Desktop task loop that consumes key events from queue.
 * input  : arg - desktop_runtime_t pointer.
 * output : None.
 * type   : private
 */
static void desktop_task_loop(void* arg) {
    desktop_runtime_t* runtime = (desktop_runtime_t*)arg;
    desktop_key_event_t event = {0};
    TickType_t wait_ticks;

    if (runtime == 0 || runtime->key_queue == 0) {
        vTaskDelete(0);
        return;
    }

    while (true) {
        if (desktop_is_home(runtime)) {
            wait_ticks = desktop_selection_wait_ticks(runtime);
            if (wait_ticks == 0) {
                desktop_clear_selection(runtime);
                continue;
            }
        } else {
            wait_ticks = portMAX_DELAY;
        }

        if (xQueueReceive(runtime->key_queue, &event, wait_ticks) != pdTRUE) {
            if (desktop_is_home(runtime) && desktop_selection_timed_out(runtime)) {
                desktop_clear_selection(runtime);
            }
            continue;
        }

        if (desktop_is_home(runtime)) {
            process_home_key(runtime, event.key_index, event.event_type);
        } else {
            process_service_key(runtime, event.key_index, event.event_type);
        }
    }
}

/* ==================== public functions ==================== */

const service_item_t g_desktop = {
    0, "Desktop", "Desktop", "System ready. Select app.", 0, 0,
};

/*
 * brief  : Initialize desktop runtime with default state.
 * input  : runtime - Desktop runtime output pointer.
 * output : None.
 * type   : public
 */
void desktop_runtime_init(desktop_runtime_t* runtime) {
    if (runtime == 0) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->state.current_service_index = -1;
    runtime->state.selected_service_index = DESKTOP_NO_SELECTION;
    runtime->state.selected_since_ms = 0;
}

/*
 * brief  : Start desktop task and initialize desktop control layer.
 * input  : runtime - Desktop runtime pointer; ops - Callback table.
 * output : ESP_OK on success; error code otherwise.
 * type   : public
 */
esp_err_t desktop_task_start(desktop_runtime_t* runtime, const desktop_ops_t* ops) {
    BaseType_t created;

    if (runtime == 0 || ops == 0 || ops->ctx == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    runtime->ops = *ops;

    if (runtime->key_queue == 0) {
        runtime->key_queue = xQueueCreate(DESKTOP_KEY_QUEUE_DEPTH, sizeof(desktop_key_event_t));
        if (runtime->key_queue == 0) {
            ESP_LOGE(DESKTOP_LOG_TAG, "Failed to create desktop key queue");
            return ESP_FAIL;
        }
    }

    if (runtime->task_handle != 0) {
        desktop_create_controls(runtime);
        desktop_set_visible(runtime, runtime->state.current_service_index < 0);
        return ESP_OK;
    }

    desktop_create_controls(runtime);
    desktop_set_visible(runtime, runtime->state.current_service_index < 0);

    created = xTaskCreate(desktop_task_loop, "desktop", DESKTOP_TASK_STACK_SIZE, runtime,
                          DESKTOP_TASK_PRIORITY, &runtime->task_handle);
    if (created != pdPASS) {
        ESP_LOGE(DESKTOP_LOG_TAG, "Failed to create desktop service task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*
 * brief  : Post one key event into desktop queue.
 * input  : runtime - Desktop runtime pointer; key_index - Physical key id;
 *          event_type - Key event type.
 * output : true if queued; otherwise false.
 * type   : public
 */
bool desktop_post_key_event(desktop_runtime_t* runtime, uint8_t key_index, uint8_t event_type) {
    desktop_key_event_t event;

    if (runtime == 0 || runtime->key_queue == 0) {
        return false;
    }

    event.key_index = key_index;
    event.event_type = event_type;
    return xQueueSend(runtime->key_queue, &event, 0) == pdTRUE;
}

/*
 * brief  : Get number of service items in desktop list.
 * input  : None.
 * output : Total item count.
 * type   : public
 */
int desktop_get_count(void) { return (int)(sizeof(k_services) / sizeof(k_services[0])); }

/*
 * brief  : Get service item metadata by index.
 * input  : service_index - Service index in list.
 * output : service_item_t pointer when valid; null pointer otherwise.
 * type   : public
 */
const service_item_t* desktop_get_item(int service_index) {
    if (service_index < 0 || service_index >= desktop_get_count()) {
        return 0;
    }
    return k_services[service_index];
}

/*
 * brief  : Check whether runtime is currently in desktop home mode.
 * input  : runtime - Desktop runtime pointer.
 * output : true when home mode; otherwise false.
 * type   : public
 */
bool desktop_is_home(const desktop_runtime_t* runtime) {
    return runtime != 0 && runtime->state.current_service_index < 0;
}
