#pragma once

#include <stdint.h>

void touchBegin();
void touchNoteActivity();
uint32_t touchLastActivityMs();
// 1 = swipe left (next page), -1 = swipe right (previous page), 0 = none.
int touchTakeSwipe();
bool touchDragged();
