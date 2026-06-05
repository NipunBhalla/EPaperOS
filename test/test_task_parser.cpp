#include <unity.h>
#include <string>

// Pure-logic modules - pull the sources in directly so this runs on the host
// (native env) without any ESP-IDF dependency.
#include "../src/tasks/TaskParser.cpp"
#include "../src/tasks/TaskModel.cpp"

void test_task_round_trip(void)
{
  std::string md =
      "# Tasks\n"
      "\n"
      "- [ ] Buy milk \xF0\x9F\x93\x85 2026-06-10\n"
      "- [x] Pay rent\n"
      "Some prose line\n"
      "\t- [ ] Indented subtask\n";

  TaskModel model;
  model.load(md);

  // 3 checkbox lines (heading + prose are passthrough)
  TEST_ASSERT_EQUAL_INT(3, (int)model.size());
  TEST_ASSERT_FALSE(model.is_done(0));
  TEST_ASSERT_TRUE(model.is_done(1));
  TEST_ASSERT_EQUAL_STRING("Buy milk \xF0\x9F\x93\x85 2026-06-10", model.text(0).c_str());

  // round-trip preserves the whole file verbatim (incl. trailing newline)
  TEST_ASSERT_EQUAL_STRING(md.c_str(), model.to_markdown().c_str());
}

void test_task_toggle_preserves_metadata(void)
{
  std::string md = "- [ ] Buy milk \xF0\x9F\x93\x85 2026-06-10\n- [x] Pay rent";
  TaskModel model;
  model.load(md);

  TEST_ASSERT_FALSE(model.dirty());
  model.toggle(0);
  TEST_ASSERT_TRUE(model.dirty());
  TEST_ASSERT_TRUE(model.is_done(0));

  std::string expected = "- [x] Buy milk \xF0\x9F\x93\x85 2026-06-10\n- [x] Pay rent";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), model.to_markdown().c_str());
}

void test_task_add(void)
{
  TaskModel model;
  model.load("- [ ] First");
  model.add("Second thing");

  TEST_ASSERT_EQUAL_INT(2, (int)model.size());
  TEST_ASSERT_EQUAL_STRING("Second thing", model.text(1).c_str());
  TEST_ASSERT_EQUAL_STRING("- [ ] First\n- [ ] Second thing", model.to_markdown().c_str());
}

// toggle with a date stamps "✅ today"; toggling back strips it.
void test_task_toggle_done_date(void)
{
  TaskModel model;
  model.load("- [ ] Buy milk");

  model.toggle(0, "2026-06-04");
  TEST_ASSERT_TRUE(model.is_done(0));
  TEST_ASSERT_EQUAL_STRING("- [x] Buy milk \xE2\x9C\x85 2026-06-04",
                           model.to_markdown().c_str());

  model.toggle(0, "2026-06-04");
  TEST_ASSERT_FALSE(model.is_done(0));
  TEST_ASSERT_EQUAL_STRING("- [ ] Buy milk", model.to_markdown().c_str());
}

// compose_text emits emoji metadata in canonical order; strip_done_date removes
// a trailing done stamp.
void test_task_compose_and_strip(void)
{
  NewTask t;
  t.text = "Do thing";
  t.priority = TaskPriority::High;
  t.start = "2026-06-04";
  t.scheduled = "2026-06-07";
  t.due = "2026-06-10";
  TEST_ASSERT_EQUAL_STRING(
      "Do thing \xE2\x8F\xAB \xF0\x9F\x9B\xAB 2026-06-04 \xE2\x8F\xB3 "
      "2026-06-07 \xF0\x9F\x93\x85 2026-06-10",
      TaskModel::compose_text(t).c_str());

  TEST_ASSERT_EQUAL_STRING(
      "Task", TaskModel::strip_done_date("Task \xE2\x9C\x85 2026-06-04").c_str());
  TEST_ASSERT_EQUAL_STRING("No stamp",
                           TaskModel::strip_done_date("No stamp").c_str());
}

// add(NewTask) drops each task at the end of the section its priority maps to:
// High -> P1, Medium -> P2, Low/None -> P3.
void test_task_add_sections(void)
{
  std::string md =
      "### P1:\n"
      "- [ ] A\n"
      "### P2:\n"
      "- [ ] B\n"
      "### P3:\n"
      "- [ ] C\n";
  TaskModel model;
  model.load(md);

  NewTask hi;
  hi.text = "HiTask";
  hi.priority = TaskPriority::High;
  model.add(hi);

  NewTask lo;
  lo.text = "LoTask";
  lo.priority = TaskPriority::Low;
  model.add(lo);

  std::string expected =
      "### P1:\n"
      "- [ ] A\n"
      "- [ ] HiTask \xE2\x8F\xAB\n"
      "### P2:\n"
      "- [ ] B\n"
      "### P3:\n"
      "- [ ] C\n"
      "- [ ] LoTask \xF0\x9F\x94\xBD\n";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), model.to_markdown().c_str());
}

// set_today hides done tasks completed on another day; pending + done-today
// stay. Section labels track the "### Px:" heading above each visible task.
void test_task_filter_and_sections(void)
{
  std::string md =
      "### P1:\n"
      "- [x] old done \xE2\x9C\x85 2024-01-01\n"
      "- [x] done today \xE2\x9C\x85 2026-06-04\n"
      "### P3:\n"
      "- [ ] pending\n";
  TaskModel model;
  model.load(md);
  TEST_ASSERT_EQUAL_INT(3, (int)model.size()); // unfiltered before set_today

  model.set_today("2026-06-04");
  TEST_ASSERT_EQUAL_INT(2, (int)model.size()); // stale done hidden
  TEST_ASSERT_TRUE(model.is_done(0));
  TEST_ASSERT_EQUAL_STRING("P1", model.section_label(0).c_str());
  TEST_ASSERT_EQUAL_STRING("P3", model.section_label(1).c_str());

  // to_markdown still preserves the hidden task (round-trip intact).
  TEST_ASSERT_EQUAL_STRING(md.c_str(), model.to_markdown().c_str());
}

// Emoji are replaced with ASCII tags for on-display rendering.
void test_task_to_display(void)
{
  // Priority + done date dropped; due -> [DD].
  TEST_ASSERT_EQUAL_STRING(
      "Call Arjun [DD] 2023-08-23",
      TaskModel::to_display(
          "Call Arjun \xE2\x8F\xAB \xF0\x9F\x93\x85 2023-08-23 \xE2\x9C\x85 "
          "2026-06-04")
          .c_str());
  // Emoji jammed onto text with no spaces still tokenize cleanly.
  TEST_ASSERT_EQUAL_STRING(
      "ping [SD] 2026-06-07 [DD] 2026-06-07",
      TaskModel::to_display("ping\xE2\x8F\xB3 2026-06-07\xF0\x9F\x93\x85 "
                            "2026-06-07")
          .c_str());
}

// The calendar (Day) filter shows tasks started, scheduled, OR completed on the
// chosen day; set_today restores the working view.
void test_task_filter_day(void)
{
  std::string md =
      "### P1:\n"
      "- [ ] started today \xF0\x9F\x9B\xAB 2026-06-04\n"
      "- [x] done other day \xE2\x9C\x85 2026-06-02\n"
      "- [ ] sched on 6th \xE2\x8F\xB3 2026-06-06\n"
      "### P3:\n"
      "- [ ] unrelated\n";
  TaskModel model;
  model.load(md);

  model.set_filter_day("2026-06-04");
  TEST_ASSERT_EQUAL_INT(1, (int)model.size()); // started that day
  TEST_ASSERT_EQUAL_STRING("started today \xF0\x9F\x9B\xAB 2026-06-04",
                           model.text(0).c_str());

  model.set_filter_day("2026-06-02");
  TEST_ASSERT_EQUAL_INT(1, (int)model.size()); // completed that day

  model.set_filter_day("2026-06-06");
  TEST_ASSERT_EQUAL_INT(1, (int)model.size()); // scheduled that day

  // Back to the working view: 3 pending; the done-2026-06-02 task stays hidden.
  model.set_today("2026-06-04");
  TEST_ASSERT_EQUAL_INT(3, (int)model.size());
}

// Per-field date extraction helpers.
void test_task_field_dates(void)
{
  std::string t =
      "x \xF0\x9F\x9B\xAB 2026-06-01 \xE2\x8F\xB3 2026-06-02 \xF0\x9F\x93\x85 "
      "2026-06-03 \xE2\x9C\x85 2026-06-04";
  TEST_ASSERT_EQUAL_STRING("2026-06-01", TaskModel::start_date(t).c_str());
  TEST_ASSERT_EQUAL_STRING("2026-06-02", TaskModel::scheduled_date(t).c_str());
  TEST_ASSERT_EQUAL_STRING("2026-06-03", TaskModel::due_date(t).c_str());
  TEST_ASSERT_EQUAL_STRING("2026-06-04", TaskModel::done_date(t).c_str());
  TEST_ASSERT_EQUAL_STRING("", TaskModel::start_date("no dates").c_str());
}
