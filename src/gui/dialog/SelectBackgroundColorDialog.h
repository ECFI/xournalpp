/*
 * Xournal++
 *
 * The about dialog
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "gui/GladeGui.h"

class ColorEntry;
class Control;

class SelectBackgroundColorDialog : public GladeGui
{
public:
	SelectBackgroundColorDialog(GladeSearchpath* gladeSearchPath, Control* control);
	virtual ~SelectBackgroundColorDialog();

public:
	virtual void show(GtkWindow* parent);

	int getSelectedColor();

	void showColorchooser();

private:
	void updateLastUsedColors();

	static void buttonSelectedCallback(GtkButton* button, ColorEntry* e);
	static void buttonCustomCallback(GtkButton* button, SelectBackgroundColorDialog* dlg);

private:
	XOJ_TYPE_ATTRIB;

	Control* control;

	int selected;

	GList* colors;

	GtkWidget* colorDlg;
};
