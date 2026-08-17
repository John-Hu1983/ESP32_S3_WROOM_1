#include "service/desktop.h"

#define TAG "desktop"

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
extern const service_item_t g_service_mute;
extern const service_item_t g_service_power;

static const service_item_t* k_services[SERVICE_APP_COUNT] = {
    &g_desktop, &g_service_ai,   &g_service_wifi,   &g_service_scan, &g_service_link,
    &g_service_offline, &g_service_cell, &g_service_signal, &g_service_node, &g_service_debug,
    &g_service_tools,   &g_service_mute, &g_service_power,
};

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

const service_item_t g_desktop = {
    0, "Desktop", "Desktop", "System ready. Select app.", 0, 0,
};

static int first_app_index(void) {
    return desktop_get_count() > 1 ? 1 : DESKTOP_NO_SELECTION;
}

static int last_app_index(void) {
    int count = desktop_get_count();
    return count > 1 ? count - 1 : DESKTOP_NO_SELECTION;
}

static int service_index_to_tile_index(int service_index) { return service_index - 1; }

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
        return;
    }

    if (runtime->state.selected_service_index == DESKTOP_NO_SELECTION) {
        return;
    }

    if (runtime->state.selected_service_index < first_index ||
        runtime->state.selected_service_index > last_index) {
        runtime->state.selected_service_index = DESKTOP_NO_SELECTION;
    }
}

static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

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

static void desktop_set_visible_locked(desktop_runtime_t* runtime, bool visible) {
    lv_obj_t* layer;

    if (runtime == 0 || runtime->desktop_layer == 0) {
        return;
    }

    layer = (lv_obj_t*)runtime->desktop_layer;
    if (!lv_obj_is_valid(layer)) {
        runtime->desktop_layer = 0;
        runtime->desktop_tile_count = 0;
        memset(runtime->desktop_tiles, 0, sizeof(runtime->desktop_tiles));
        return;
    }

    if (visible) {
        lv_obj_remove_flag(layer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
    }
}

static void desktop_set_visible(desktop_runtime_t* runtime, bool visible) {
    if (runtime == 0) {
        return;
    }

    if (!lvgl_port_lock(30000)) {
        ESP_LOGW(TAG, "Failed to lock LVGL for desktop visibility update");
        return;
    }
    desktop_set_visible_locked(runtime, visible);
    lvgl_port_unlock();
}

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

    if (!lvgl_port_lock(30000)) {
        ESP_LOGW(TAG, "Failed to lock LVGL for desktop creation");
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

    // LVGL uses top-left as origin.
    // Physical start near top bar maps to y = top_bar_height in this coordinate system.
    // Target height: lcd_height - top_bar_height - bottom_bar_height
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

    ESP_LOGI(TAG, "desktop parent layout=%u coords=(%d,%d)-(%d,%d)",
             (unsigned int)lv_obj_get_style_layout(layer_parent, 0), (int)parent_coords.x1,
             (int)parent_coords.y1, (int)parent_coords.x2, (int)parent_coords.y2);

    ESP_LOGI(TAG,
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

        runtime->desktop_layer = 0;
        runtime->desktop_tile_count = 0;
        memset(runtime->desktop_tiles, 0, sizeof(runtime->desktop_tiles));
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
    runtime->desktop_layer = layer;
    runtime->desktop_tile_count = 0;
    memset(runtime->desktop_tiles, 0, sizeof(runtime->desktop_tiles));

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

static void desktop_apply_selection(desktop_runtime_t* runtime) {
    if (runtime == 0) {
        return;
    }

    if (!lvgl_port_lock(30000)) {
        ESP_LOGW(TAG, "Failed to lock LVGL for selection update");
        return;
    }
    desktop_apply_selection_locked(runtime);
    lvgl_port_unlock();
}

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

    if (runtime->state.last_click_ms[other_key] != 0 &&
        (now - runtime->state.last_click_ms[other_key]) <= DESKTOP_DUAL_CLICK_WINDOW_MS) {
        runtime->state.last_click_ms[0] = 0;
        runtime->state.last_click_ms[1] = 0;
        runtime->state.current_service_index = -1;
        normalize_selected(runtime);
        desktop_create_controls(runtime);
        desktop_apply_selection(runtime);
        desktop_set_visible(runtime, true);

        if (runtime->ops.enter_desktop != 0) {
            runtime->ops.enter_desktop(runtime->ops.ctx, true);
        }
        return true;
    }

    return false;
}

static void run_service_command(desktop_runtime_t* runtime, service_command_t command) {
    if (runtime == 0 || runtime->ops.run_command == 0) {
        return;
    }
    runtime->ops.run_command(runtime->ops.ctx, command);
}

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
        show_selected_service(runtime);
        return;
    }

    if ((key_index == DESKTOP_PRIMARY_KEY || key_index == DESKTOP_SECONDARY_KEY) &&
        event_type == SERVICE_KEY_EVENT_LONG_PRESS) {
        if (runtime->state.selected_service_index == DESKTOP_NO_SELECTION) {
            return;
        }
        runtime->state.current_service_index = runtime->state.selected_service_index;
        runtime->state.last_click_ms[0] = 0;
        runtime->state.last_click_ms[1] = 0;
        desktop_set_visible(runtime, false);
        if (runtime->ops.enter_service != 0) {
            runtime->ops.enter_service(runtime->ops.ctx, runtime->state.current_service_index);
        }
    }
}

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
        if (result.command == SERVICE_CMD_ENTER_DESKTOP) {
            runtime->state.current_service_index = -1;
            normalize_selected(runtime);
            desktop_create_controls(runtime);
            desktop_apply_selection(runtime);
            desktop_set_visible(runtime, true);
        }

        if (result.notification != 0 && result.notification[0] != '\0' &&
            runtime->ops.show_notification != 0) {
            runtime->ops.show_notification(runtime->ops.ctx, result.notification,
                                           DESKTOP_ACTION_NOTIFICATION_MS);
        }
        run_service_command(runtime, result.command);
        return;
    }

    if (runtime->state.current_service_index == DESKTOP_AI_SERVICE_INDEX) {
        if (key_index == DESKTOP_PRIMARY_KEY && event_type == SERVICE_KEY_EVENT_CLICK) {
            run_service_command(runtime, SERVICE_CMD_TOGGLE_CHAT);
        } else if (key_index == DESKTOP_SECONDARY_KEY &&
                   event_type == SERVICE_KEY_EVENT_PRESS_DOWN) {
            run_service_command(runtime, SERVICE_CMD_START_LISTENING);
        } else if (key_index == DESKTOP_SECONDARY_KEY && event_type == SERVICE_KEY_EVENT_PRESS_UP) {
            run_service_command(runtime, SERVICE_CMD_STOP_LISTENING);
        }
    }
}

static void desktop_task_loop(void* arg) {
    desktop_runtime_t* runtime = (desktop_runtime_t*)arg;
    desktop_key_event_t event = {0};

    if (runtime == 0 || runtime->key_queue == 0) {
        vTaskDelete(0);
        return;
    }

    while (true) {
        if (xQueueReceive(runtime->key_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (desktop_is_home(runtime)) {
            process_home_key(runtime, event.key_index, event.event_type);
        } else {
            process_service_key(runtime, event.key_index, event.event_type);
        }
    }
}

void desktop_runtime_init(desktop_runtime_t* runtime) {
    if (runtime == 0) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->state.current_service_index = -1;
    runtime->state.selected_service_index = DESKTOP_NO_SELECTION;
}

esp_err_t desktop_task_start(desktop_runtime_t* runtime,
                                     const desktop_ops_t* ops) {
    BaseType_t created;

    if (runtime == 0 || ops == 0 || ops->ctx == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    runtime->ops = *ops;

    if (runtime->key_queue == 0) {
        runtime->key_queue =
            xQueueCreate(DESKTOP_KEY_QUEUE_DEPTH, sizeof(desktop_key_event_t));
        if (runtime->key_queue == 0) {
            ESP_LOGE(TAG, "Failed to create desktop key queue");
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

    created = xTaskCreate(desktop_task_loop, "desktop", DESKTOP_TASK_STACK_SIZE,
                          runtime, DESKTOP_TASK_PRIORITY, &runtime->task_handle);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create desktop service task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool desktop_post_key_event(desktop_runtime_t* runtime, uint8_t key_index,
                                    uint8_t event_type) {
    desktop_key_event_t event;

    if (runtime == 0 || runtime->key_queue == 0) {
        return false;
    }

    event.key_index = key_index;
    event.event_type = event_type;
    return xQueueSend(runtime->key_queue, &event, 0) == pdTRUE;
}

int desktop_get_count(void) { return (int)(sizeof(k_services) / sizeof(k_services[0])); }

const service_item_t* desktop_get_item(int service_index) {
    if (service_index < 0 || service_index >= desktop_get_count()) {
        return 0;
    }
    return k_services[service_index];
}

bool desktop_is_home(const desktop_runtime_t* runtime) {
    return runtime != 0 && runtime->state.current_service_index < 0;
}
