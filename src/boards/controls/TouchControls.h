#pragma once

#include "Actions.h"

class Renderer;

// TODO - we should move the rendering out of this class so that it's only doing the touch detection
class TouchControls
{
public:
  TouchControls(){};
  // draw the controls on the screen
  virtual void render(Renderer *renderer) {}
  // show the touched state
  virtual void renderPressedState(Renderer *renderer, UIAction action, bool state = true) {}
  // put the touch controller into its lowest-power state before deep sleep
  virtual void prepare_for_deep_sleep() {}
};