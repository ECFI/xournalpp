/*
 * Xournal++
 *
 * A Sidebar preview widget
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "model/PageRef.h"

#include <Util.h>
#include <XournalType.h>

#include <gtk/gtk.h>

class SidebarPreviews;

class SidebarPreviewPage
{
public:
	SidebarPreviewPage(SidebarPreviews* sidebar, PageRef page);
	virtual ~SidebarPreviewPage();

	GtkWidget* getWidget();
	int getWidth();
	int getHeight();

	void setSelected(bool selected);

	void repaint();
	void updateSize();

private:
	static gboolean exposeEventCallback(GtkWidget* widget, GdkEventExpose* event, SidebarPreviewPage* preview);
	static gboolean mouseButtonPressCallback(GtkWidget* widget, GdkEventButton* event, SidebarPreviewPage* preview);

	void paint();

private:
	XOJ_TYPE_ATTRIB;

	/**
	 * If this page is currently selected
	 */
	bool selected;

	/**
	 * If this preview is painted once
	 */
	bool firstPainted;

	/**
	 * The sidebar which displays the previews
	 */
	SidebarPreviews* sidebar;

	/**
	 * The page which is representated
	 */
	PageRef page;

	/**
	 * Mutex
	 */
	GMutex drawingMutex;

	/**
	 * The Widget wich is used for drawing
	 */
	GtkWidget* widget;

	/**
	 * Buffer because of performance reasons
	 */
	cairo_surface_t* crBuffer;

	friend class PreviewJob;
};
