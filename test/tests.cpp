#include <unity.h>

void test_xml_parser(void);
void test_parser(void);
void test_epub_no_oebps_load(void);
void test_epub_load(void);
void test_epub_relative_image_paths(void);
void test_html_entity_replacement(void);
void test_epub_toc_load(void);
void test_task_round_trip(void);
void test_task_toggle_preserves_metadata(void);
void test_task_add(void);
void test_task_toggle_done_date(void);
void test_task_compose_and_strip(void);
void test_task_add_sections(void);
void test_task_filter_and_sections(void);
void test_task_to_display(void);
void test_task_filter_day(void);
void test_task_field_dates(void);
// D2 UI / settings / transport modules.
void test_hittest_rect(void);
void test_hittest_grid(void);
void test_refresh_policy(void);
void test_keyboard_typing(void);
void test_keyboard_done_cancel(void);
void test_keyboard_layer_and_mask(void);
void test_keyboard_shift(void);
void test_keyboard_hit(void);
void test_settings_configured(void);
void test_vault_url(void);

int main(int argc, char **argv)
{
  UNITY_BEGIN();
  RUN_TEST(test_xml_parser);
  RUN_TEST(test_parser);
  RUN_TEST(test_epub_no_oebps_load);
  RUN_TEST(test_epub_load);
  RUN_TEST(test_epub_relative_image_paths);
  RUN_TEST(test_html_entity_replacement);
  RUN_TEST(test_epub_toc_load);
  RUN_TEST(test_task_round_trip);
  RUN_TEST(test_task_toggle_preserves_metadata);
  RUN_TEST(test_task_add);
  RUN_TEST(test_task_toggle_done_date);
  RUN_TEST(test_task_compose_and_strip);
  RUN_TEST(test_task_add_sections);
  RUN_TEST(test_task_filter_and_sections);
  RUN_TEST(test_task_to_display);
  RUN_TEST(test_task_filter_day);
  RUN_TEST(test_task_field_dates);
  RUN_TEST(test_hittest_rect);
  RUN_TEST(test_hittest_grid);
  RUN_TEST(test_refresh_policy);
  RUN_TEST(test_keyboard_typing);
  RUN_TEST(test_keyboard_done_cancel);
  RUN_TEST(test_keyboard_layer_and_mask);
  RUN_TEST(test_keyboard_shift);
  RUN_TEST(test_settings_configured);
  RUN_TEST(test_vault_url);
  UNITY_END();

  return 0;
}