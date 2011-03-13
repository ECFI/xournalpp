#include "Document.h"
#include "LinkDestination.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include <string.h>
#include "../util/Stacktrace.h"

Document::Document(DocumentHandler * handler) {
	this->handler = handler;
	this->contentsModel = NULL;
	this->pages = NULL;
	this->pageCount = 0;
	this->pagesArrayLen = 0;
	this->preview = NULL;
	this->attachPdf = false;
	this->createBackupOnSave = false;

	this->documentLock = g_mutex_new();
}

Document::~Document() {
	clearDocument(true);

	if (contentsModel) {
		g_object_unref(contentsModel);
		contentsModel = NULL;
	}

	g_mutex_free(this->documentLock);
	this->documentLock = NULL;
}

void Document::lock() {
	// TODO: real version:
	 g_mutex_lock(this->documentLock);

	// TODO: debug
//	if(tryLock()) {
//		Stacktrace::printStracktrace();
//		fprintf(stderr, "\n\n\n\n");
//	} else {
//		g_mutex_lock(this->documentLock);
//	}
}

void Document::unlock() {
	g_mutex_unlock(this->documentLock);
}

bool Document::tryLock() {
	return g_mutex_trylock(this->documentLock);
}

void Document::clearDocument(bool destroy) {
	if (this->preview) {
		cairo_surface_destroy(this->preview);
		this->preview = NULL;
	}

	if (!destroy) {
		// look aufheben
		bool lastLock = tryLock();
		unlock();
		handler->fireDocumentChanged(DOCUMENT_CHANGE_CLEARED);
		if(!lastLock) { // document was locked before
			lock();
		}
	}

	for (int i = 0; i < this->pageCount; i++) {
		this->pages[i]->unreference();
	}
	g_free(pages);
	this->pages = NULL;
	this->pageCount = 0;
	this->pagesArrayLen = 0;

	this->filename = NULL;
	this->pdfFilename = NULL;
}

/**
 * Returns the pageCount, this call don't need to be synchronized (if it's not critical, you may get wrong data)
 */
int Document::getPageCount() {
	return this->pageCount;
}

int Document::getPdfPageCount() {
	return pdfDocument.getPageCount();
}

void Document::setFilename(String filename) {
	this->filename = filename;
}

String Document::getFilename() {
	return filename;
}

String Document::getPdfFilename() {
	return pdfFilename;
}

cairo_surface_t * Document::getPreview() {
	return this->preview;
}

void Document::setPreview(cairo_surface_t * preview) {
	if (this->preview) {
		cairo_surface_destroy(this->preview);
	}
	if (preview) {
		this->preview = cairo_surface_reference(preview);
	} else {
		this->preview = NULL;
	}

}

const char * Document::getEvMetadataFilename() {
	String uri = "file://";
	if (filename.c_str() != NULL) {
		uri += filename;
		return uri.c_str();
	}
	if (pdfFilename.c_str() != NULL) {
		uri += pdfFilename;
		return uri.c_str();
	}
	return NULL;
}

bool Document::isPdfDocumentLoaded() {
	return pdfDocument.isLoaded();
}

bool Document::isAttachPdf() {
	return this->attachPdf;
}

int Document::findPdfPage(int pdfPage) {
	for (int i = 0; i < this->pageCount; i++) {
		XojPage * p = this->pages[i];
		if (p->getBackgroundType() == BACKGROUND_TYPE_PDF) {
			if (p->getPdfPageNr() == pdfPage) {
				return i;
			}
		}
	}
	return -1;
}

void Document::buildTreeContentsModel(GtkTreeIter * parent, XojPopplerIter * iter) {
	do {
		GtkTreeIter treeIter = { 0 };

		XojPopplerAction * action = iter->getAction();
		XojLinkDest * link = action->getDestination();

		if (action->getTitle().isEmpty()) {
			g_object_unref(link);
			delete action;
			continue;
		}

		link->dest->setExpand(iter->isOpen());

		gtk_tree_store_append(GTK_TREE_STORE(contentsModel), &treeIter, parent);
		char *titleMarkup = g_markup_escape_text(action->getTitle().c_str(), -1);

		gtk_tree_store_set(GTK_TREE_STORE(contentsModel), &treeIter, DOCUMENT_LINKS_COLUMN_NAME, titleMarkup, DOCUMENT_LINKS_COLUMN_LINK, link,
				DOCUMENT_LINKS_COLUMN_PAGE_NUMBER, "", -1);

		g_free(titleMarkup);
		g_object_unref(link);

		XojPopplerIter * child = iter->getChildIter();
		if (child) {
			buildTreeContentsModel(&treeIter, child);
			delete child;
		}

		delete action;

	} while (iter->next());
}

void Document::buildContentsModel() {
	if (this->contentsModel) {
		g_object_unref(this->contentsModel);
		this->contentsModel = NULL;
	}

	XojPopplerIter * iter = pdfDocument.getContentsIter();
	if (iter == NULL) {
		// No Bookmarks
		return;
	}

	this->contentsModel = (GtkTreeModel *) gtk_tree_store_new(4, G_TYPE_STRING, G_TYPE_OBJECT, G_TYPE_BOOLEAN, G_TYPE_STRING);
	g_object_ref(this->contentsModel);
	buildTreeContentsModel(NULL, iter);
	delete iter;
}

GtkTreeModel * Document::getContentsModel() {
	return this->contentsModel;
}

bool Document::fillPageLabels(GtkTreeModel *treeModel, GtkTreePath *path, GtkTreeIter *iter, Document *doc) {
	XojLinkDest * link = NULL;

	gtk_tree_model_get(treeModel, iter, DOCUMENT_LINKS_COLUMN_LINK, &link, -1);

	if (!link) {
		return false;
	}

	int page = doc->findPdfPage(link->dest->getPdfPage());

	gchar * pageLabel = NULL;
	if (page != -1) {
		pageLabel = g_strdup_printf("%i", page + 1);
	}
	gtk_tree_store_set(GTK_TREE_STORE (treeModel), iter, DOCUMENT_LINKS_COLUMN_PAGE_NUMBER, pageLabel, -1);
	g_free(pageLabel);

	g_object_unref(link);
	return false;
}

void Document::updateIndexPageNumbers() {
	if (contentsModel != NULL) {
		gtk_tree_model_foreach(contentsModel, (GtkTreeModelForeachFunc) fillPageLabels, this);
	}
}

bool Document::readPdf(String filename, bool initPages, bool attachToDocument) {
	GError * popplerError = NULL;
	String uri = "file://";
	uri += filename;

	lock();

	if (!pdfDocument.load(uri.c_str(), password.c_str(), &popplerError)) {
		char * txt = g_strdup_printf("Document not loaded! (%s), %s", filename.c_str(), popplerError->message);
		lastError = txt;
		g_free(txt);
		g_error_free(popplerError);
		return false;
	}

	printf("attachToDocument: %i\n", attachToDocument);

	this->pdfFilename = filename;
	this->attachPdf = attachToDocument;

	lastError = NULL;

	if (initPages) {
		for (int i = 0; i < this->pageCount; i++) {
			this->pages[i]->unreference();
		}
		g_free(this->pages);
		this->pages = NULL;
		this->pageCount = 0;
		this->pagesArrayLen = 0;
	}

	if (initPages) {
		for (int i = 0; i < pdfDocument.getPageCount(); i++) {
			XojPopplerPage * page = pdfDocument.getPage(i);
			XojPage * p = new XojPage(page->getWidth(), page->getHeight());
			p->setBackgroundPdfPageNr(i);
			addPage(p);
		}
	}

	buildContentsModel();
	updateIndexPageNumbers();

	unlock();

	handler->fireDocumentChanged(DOCUMENT_CHANGE_PDF_BOOKMARKS);

	return true;
}

void Document::setPageSize(XojPage * p, double width, double height) {
	p->setSize(width, height);

	int id = indexOf(p);
	if (id >= 0 && id < this->pageCount) {
		firePageSizeChanged(id);
	}
}

String Document::getLastErrorMsg() {
	return lastError;
}

void Document::deletePage(int pNr) {
	this->pages[pNr]->unreference();
	for (int i = pNr; i < this->pageCount; i++) {
		this->pages[i] = this->pages[i + 1];
	}
	this->pageCount--;

	updateIndexPageNumbers();
}

void Document::insertPage(XojPage * p, int position) {
	if (this->pagesArrayLen <= this->pageCount + 1) {
		this->pagesArrayLen += 100;
		this->pages = (XojPage **) g_realloc(this->pages, sizeof(XojPage *) * this->pagesArrayLen);
	}

	for (int i = position; i < this->pageCount; i++) {
		this->pages[i + 1] = this->pages[i];
	}

	this->pages[position] = p;

	this->pageCount++;
	p->reference();

	updateIndexPageNumbers();
}

void Document::addPage(XojPage * p) {
	if (this->pagesArrayLen <= this->pageCount + 1) {
		this->pagesArrayLen += 100;
		this->pages = (XojPage **) g_realloc(this->pages, sizeof(XojPage *) * this->pagesArrayLen);
	}

	this->pages[this->pageCount++] = p;
	p->reference();

	updateIndexPageNumbers();
}

int Document::indexOf(XojPage * page) {
	for (int i = 0; i < this->pageCount; i++) {
		if (page == this->pages[i]) {
			return i;
		}
	}

	return -1;
}

XojPage * Document::getPage(int page) {
	if (getPageCount() <= page) {
		return NULL;
	}
	if (page < 0) {
		return NULL;
	}

	return this->pages[page];
}

void Document::firePageSizeChanged(int page) {
	handler->firePageSizeChanged(page);
}

XojPopplerPage * Document::getPdfPage(int page) {
	return pdfDocument.getPage(page);
}

XojPopplerDocument & Document::getPdfDocument() {
	return pdfDocument;
}

void Document::operator=(Document & doc) {
	clearDocument();

	this->pdfDocument = doc.pdfDocument;

	this->password = doc.password;
	this->createBackupOnSave = doc.createBackupOnSave;
	this->pdfFilename = doc.pdfFilename;
	this->filename = doc.filename;

	for (int i = 0; i < doc.pageCount; i++) {
		XojPage * p = doc.pages[i];
		addPage(p);
	}

	buildContentsModel();
	updateIndexPageNumbers();

	bool lastLock = tryLock();
	unlock();
	handler->fireDocumentChanged(DOCUMENT_CHANGE_COMPLETE);
	if(!lastLock) { // document was locked before
		lock();
	}
}

void Document::setCreateBackupOnSave(bool backup) {
	this->createBackupOnSave = backup;
}

bool Document::shouldCreateBackupOnSave() {
	return this->createBackupOnSave;
}

