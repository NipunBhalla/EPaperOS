#include <unity.h>
#include <string>

// Pure-logic D2 modules - pull the sources in directly so this runs on the host
// (native env) without any ESP-IDF dependency. SettingsStore.cpp compiles its
// host (no-NVS) branch because ESP_PLATFORM is not defined here.
#include "../src/ui/HitTest.cpp"
#include "../src/ui/Refresh.cpp"
#include "../src/ui/Keyboard.cpp"
#include "../src/settings/SettingsStore.cpp"
#include "../src/tasks/ObsidianUrl.cpp"

using ui::Key;
using ui::KeyboardModel;
using ui::KeyType;
using ui::Rect;
using ui::RefreshPolicy;

// ---- HitTest ----

void test_hittest_rect(void)
{
  Rect r{10, 20, 100, 50};
  TEST_ASSERT_TRUE(r.contains(10, 20));    // top-left inclusive
  TEST_ASSERT_TRUE(r.contains(109, 69));   // bottom-right inside
  TEST_ASSERT_FALSE(r.contains(110, 70));  // far edge exclusive
  TEST_ASSERT_FALSE(r.contains(9, 20));    // left of
  TEST_ASSERT_FALSE(r.contains(10, 19));   // above
}

void test_hittest_grid(void)
{
  Rect area{0, 0, 300, 200}; // 3 cols x 2 rows => cells 100x100
  TEST_ASSERT_EQUAL_INT(0, ui::grid_cell(area, 3, 2, 0, 0));
  TEST_ASSERT_EQUAL_INT(2, ui::grid_cell(area, 3, 2, 250, 50));   // row0 col2
  TEST_ASSERT_EQUAL_INT(3, ui::grid_cell(area, 3, 2, 50, 150));   // row1 col0
  TEST_ASSERT_EQUAL_INT(5, ui::grid_cell(area, 3, 2, 299, 199));  // last cell (remainder clamps)
  TEST_ASSERT_EQUAL_INT(-1, ui::grid_cell(area, 3, 2, 400, 50));  // outside
  TEST_ASSERT_EQUAL_INT(-1, ui::grid_cell(area, 0, 2, 10, 10));   // degenerate
}

// ---- RefreshPolicy ----

void test_refresh_policy(void)
{
  RefreshPolicy rp(3);
  TEST_ASSERT_FALSE(rp.note_partial()); // 1
  TEST_ASSERT_FALSE(rp.note_partial()); // 2
  TEST_ASSERT_TRUE(rp.note_partial());  // 3 -> full flash due, resets
  TEST_ASSERT_EQUAL_INT(0, rp.count());
  TEST_ASSERT_FALSE(rp.note_partial()); // 1 again
  rp.note_full();
  TEST_ASSERT_EQUAL_INT(0, rp.count());
}

// ---- KeyboardModel ----

void test_keyboard_typing(void)
{
  KeyboardModel kb;
  kb.begin("", false);
  Key h{KeyType::CHAR, 'h'};
  Key i{KeyType::CHAR, 'i'};
  kb.press(h);
  kb.press(i);
  TEST_ASSERT_EQUAL_STRING("hi", kb.buffer().c_str());
  kb.press(Key{KeyType::BACKSPACE});
  TEST_ASSERT_EQUAL_STRING("h", kb.buffer().c_str());
  kb.press(Key{KeyType::SPACE});
  TEST_ASSERT_EQUAL_STRING("h ", kb.buffer().c_str());
}

void test_keyboard_done_cancel(void)
{
  KeyboardModel kb;
  kb.begin("seed", false);
  TEST_ASSERT_FALSE(kb.done());
  TEST_ASSERT_FALSE(kb.cancelled());
  kb.press(Key{KeyType::DONE});
  TEST_ASSERT_TRUE(kb.done());
  TEST_ASSERT_EQUAL_STRING("seed", kb.buffer().c_str());
}

void test_keyboard_layer_and_mask(void)
{
  KeyboardModel kb;
  kb.begin("pw", true);
  TEST_ASSERT_EQUAL_STRING("**", kb.display().c_str());     // masked display
  TEST_ASSERT_EQUAL_STRING("pw", kb.buffer().c_str());      // real buffer intact
  TEST_ASSERT_EQUAL_INT(0, kb.layer());
  kb.press(Key{KeyType::LAYER});
  TEST_ASSERT_EQUAL_INT(1, kb.layer());                     // letters -> symbols
  // First key of the symbols layer is '1'.
  TEST_ASSERT_EQUAL_INT((int)KeyType::CHAR, (int)kb.layout()[0][0].type);
  TEST_ASSERT_EQUAL_INT('1', kb.layout()[0][0].ch);
}

void test_keyboard_shift(void)
{
  KeyboardModel kb;
  kb.begin("", false);
  TEST_ASSERT_EQUAL_INT('q', kb.layout()[0][0].ch); // lowercase by default
  kb.press(Key{KeyType::SHIFT});
  TEST_ASSERT_TRUE(kb.shift());
  TEST_ASSERT_EQUAL_INT('Q', kb.layout()[0][0].ch); // shifted
}

void test_keyboard_hit(void)
{
  KeyboardModel kb;
  kb.begin("", false);
  // 4 rows; row 0 'qwertyuiop' has 10 keys. Area 540 wide x 400 tall =>
  // rh=100, row0 col0 width 54.
  Rect area{0, 0, 540, 400};
  Key k = kb.key_at(area, 5, 5);
  TEST_ASSERT_EQUAL_INT((int)KeyType::CHAR, (int)k.type);
  TEST_ASSERT_EQUAL_INT('q', k.ch);
  // Outside the area -> NONE.
  Key miss = kb.key_at(area, 600, 5);
  TEST_ASSERT_EQUAL_INT((int)KeyType::NONE, (int)miss.type);
}

// ---- SettingsStore / AppConfig ----

void test_settings_configured(void)
{
  settings::AppConfig cfg;
  TEST_ASSERT_FALSE(cfg.tasks_configured()); // empty => first run
  cfg.wifi_ssid = "net";
  cfg.obsidian_url = "http://host:27123";
  cfg.obsidian_token = "tok";
  TEST_ASSERT_FALSE(cfg.tasks_configured()); // still missing note_path
  cfg.note_path = "Tasks.md";
  TEST_ASSERT_TRUE(cfg.tasks_configured());
}

// ---- Obsidian REST URL builder ----

void test_vault_url(void)
{
  TEST_ASSERT_EQUAL_STRING(
      "http://192.168.1.50:27123/vault/Tasks.md",
      tasks::build_vault_url("http://192.168.1.50:27123", "Tasks.md").c_str());
  // Trailing slash on base + leading slash on path are normalised.
  TEST_ASSERT_EQUAL_STRING(
      "http://h:27123/vault/Inbox/Tasks.md",
      tasks::build_vault_url("http://h:27123/", "/Inbox/Tasks.md").c_str());
  // Spaces and other reserved chars are percent-encoded; '/' is preserved.
  TEST_ASSERT_EQUAL_STRING(
      "http://h:27123/vault/My%20Notes/To%20Do.md",
      tasks::build_vault_url("http://h:27123", "My Notes/To Do.md").c_str());
}
