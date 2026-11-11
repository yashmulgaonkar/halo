#pragma once

#include <stdint.h>

void touchBegin();
void touchNoteActivity();
uint32_t touchLastActivityMs();
// 1 = swipe left (next page), -1 = swipe right (previous page), 0 = none.
int touchTakeSwipe();
bool touchDragged();

struct TouchStats {
  uint32_t contacts;  // tracked contacts that ended
  uint32_t swipes;    // of those, how many counted as a swipe
  uint32_t ghosts;    // reads that never became a steady contact
  uint32_t stuck;     // contacts that sat still and were dropped as phantoms
};
TouchStats touchStats();
// True once after a contact was dropped as a phantom. The UI uses it to undo
// what the press did before it was recognised, such as a long press.
bool touchTakeStuck();
