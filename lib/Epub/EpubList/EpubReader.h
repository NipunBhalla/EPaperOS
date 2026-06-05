#pragma once

class Epub;
class Renderer;
class RubbishHtmlParser;

#include "./State.h"

class EpubReader
{
private:
  EpubListItem &state;
  Epub *epub = nullptr;
  Renderer *renderer = nullptr;
  RubbishHtmlParser *parser = nullptr;
  RubbishHtmlParser *next_parser = nullptr;
  int16_t parser_section = -1;
  int16_t next_parser_section = -1;

  bool use_justified = false;

  void parse_and_layout_current_section();
  void prefetch_next_section();

public:
  EpubReader(EpubListItem &state, Renderer *renderer) : state(state), renderer(renderer){};
  ~EpubReader();
  bool load();
  void next();
  void prev();
  void render();
  // Lay out the NEXT section ahead of time so crossing a chapter boundary is an
  // instant swap instead of a multi-second parse. Call AFTER the current page is
  // flushed to the panel (page is already visible; this runs during idle). Kept
  // synchronous on the caller's task on purpose: RubbishHtmlParser::layout uses
  // the shared, non-reentrant FreeType face, so it must not run concurrently
  // with a render on another task.
  void prefetch();
  void set_state_section(uint16_t current_section);
  void next_section();
  void prev_section();
  void set_justified(bool justified);
};