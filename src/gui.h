#ifndef GUI_H
#define GUI_H

// Commands that need user interaction. The engine calls these from its key
// handlers; the GTK front end implements them with (non-blocking) dialogs.

void ui_quit(void);
void ui_clear(void);
void ui_help(int context);
void ui_editadsr(void);
void ui_relocator(void);
void ui_load(int merge);
void ui_save(void);

#endif
