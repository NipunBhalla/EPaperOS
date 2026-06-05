#pragma once

#include <vector>
#include <sys/types.h>
extern "C" {
  #include <dirent.h>
}
#include <string.h>
#include <algorithm>
#include "Epub.h"
#include "Renderer/Renderer.h"
#include "../RubbishHtmlParser/blocks/TextBlock.h"
#include "../RubbishHtmlParser/htmlEntities.h"
#include "./State.h"

#ifndef UNIT_TEST
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#endif
#include <warning.h>

class Epub;
class Renderer;

class EpubList
{
private:
  Renderer *renderer;
  EpubListState &state;
  bool m_needs_redraw = false;
  std::vector<TextBlock *> m_title_blocks;

  bool load_index(const char *books_path, const char *index_path);
  // Best-effort read of the raw index records (no directory validation), used
  // to carry reading progress across a full rescan. Returns items by value.
  std::vector<EpubListItem> read_index_items(const char *index_path);
  // Copy reading progress (section/page/pagination) from a previous index into
  // the freshly-scanned list, matching books by path.
  void carry_over_progress(const std::vector<EpubListItem> &previous);
  // Draw a book cover at (x,y) sized w*h, using a cached grayscale thumbnail on
  // SD (/fs/Books/.thumb). On a cache miss the cover is decoded once, blitted,
  // and the thumbnail written for next time. Returns false (caller draws a
  // title card) if there is no cover or it can't be produced.
  bool draw_cover_cached(Epub &epub, const EpubListItem &item, int x, int y, int w, int h);

public:
  EpubList(Renderer *renderer, EpubListState &state) : renderer(renderer), state(state)
  {
    if (!state.is_loaded)
    {
      state.use_grid_view = false;
    }
  }
  ~EpubList()
  {
    for (auto *block : m_title_blocks)
    {
      delete block;
    }
    m_title_blocks.clear();
  }
  bool load(const char *path);
  void set_needs_redraw() { m_needs_redraw = true; }
  void next();
  void prev();
  void render();
  void save_index(const char *index_path);
};