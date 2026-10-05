#ifndef GUNDO_H
#define GUNDO_H

// Undo/redo and unsaved-changes tracking for the song data.
//
// Instead of instrumenting every editing command, the song data is compared
// after each action with a shadow copy of how it was after the previous one.
// Only the blocks that changed (a pattern, a subtune's orderlists, the
// instruments, ...) are stored in the undo entry.

// Forget the history and treat the current song as saved (new or loaded song)
void undo_reset(void);

// Remember the edit cursor before running a command, so that undoing it
// returns the cursor to where the edit happened
void undo_markcursor(void);

// Record whatever changed since the last checkpoint as one undo step.
// Consecutive changes with the same nonzero coalesce key (typing in a text
// field, dragging a slider) are merged into one step. Returns 1 if anything
// changed.
int undo_checkpoint(int coalesce);

int undo_undo(void);
int undo_redo(void);
int undo_canundo(void);
int undo_canredo(void);

// Unsaved changes: the song differs from the last saved or loaded state
int undo_isdirty(void);
void undo_marksaved(void);

#endif
