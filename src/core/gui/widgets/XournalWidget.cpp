#include "XournalWidget.h"

#include <algorithm>  // for max
#include <cmath>      // for NAN
#include <optional>   // for optional
#include <vector>     // for vector

#include <cairo.h>    // for cairo_restore, cairo_save
#include <gdk/gdk.h>  // for GdkRectangle, GdkWindowAttr

#include "control/Control.h"                // for Control
#include "control/settings/Settings.h"      // for Settings
#include "control/tools/EditSelection.h"    // for EditSelection
#include "gui/Layout.h"                     // for Layout
#include "gui/PageView.h"                   // for XojPageView
#include "gui/Redrawable.h"                 // for Redrawable
#include "gui/Shadow.h"                     // for Shadow
#include "gui/XournalView.h"                // for XournalView
#include "gui/inputdevices/InputContext.h"  // for InputContext
#include "gui/scroll/ScrollHandling.h"      // for ScrollHandling
#include "util/Color.h"                     // for cairo_set_source_rgbi
#include "util/Rectangle.h"                 // for Rectangle
#include "view/SetsquareView.h"             // for SetsquareView


using xoj::util::Rectangle;

static void gtk_xournal_class_init(GtkXournalClass* klass);
static void gtk_xournal_init(GtkXournal* xournal);
static void gtk_xournal_get_preferred_width(GtkWidget* widget, gint* minimal_width, gint* natural_width);
static void gtk_xournal_get_preferred_height(GtkWidget* widget, gint* minimal_height, gint* natural_height);
static void gtk_xournal_size_allocate(GtkWidget* widget, GtkAllocation* allocation);
static void gtk_xournal_realize(GtkWidget* widget);
static auto gtk_xournal_draw(GtkWidget* widget, cairo_t* cr) -> gboolean;
static void gtk_xournal_dispose(GObject* object);

auto gtk_xournal_get_type(void) -> GType {
    static GType gtk_xournal_type = 0;

    if (!gtk_xournal_type) {
        static const GTypeInfo gtk_xournal_info = {sizeof(GtkXournalClass),
                                                   // base initialize
                                                   nullptr,
                                                   // base finalize
                                                   nullptr,
                                                   // class initialize
                                                   reinterpret_cast<GClassInitFunc>(gtk_xournal_class_init),
                                                   // class finalize
                                                   nullptr,
                                                   // class data,
                                                   nullptr,
                                                   // instance size
                                                   sizeof(GtkXournal),
                                                   // n_preallocs
                                                   0,
                                                   // instance init
                                                   reinterpret_cast<GInstanceInitFunc>(gtk_xournal_init),
                                                   // value table
                                                   nullptr};

        gtk_xournal_type =
                g_type_register_static(GTK_TYPE_WIDGET, "GtkXournal", &gtk_xournal_info, static_cast<GTypeFlags>(0));
    }

    return gtk_xournal_type;
}

auto gtk_xournal_new(XournalView* view, InputContext* inputContext) -> GtkWidget* {
    GtkXournal* xoj = GTK_XOURNAL(g_object_new(gtk_xournal_get_type(), nullptr));
    xoj->view = view;
    xoj->scrollHandling = inputContext->getScrollHandling();
    xoj->layout = new Layout(view, inputContext->getScrollHandling());
    xoj->selection = nullptr;
    xoj->setsquareView = nullptr;

    xoj->input = inputContext;

    xoj->input->connect(GTK_WIDGET(xoj));

    return GTK_WIDGET(xoj);
}

static void gtk_xournal_class_init(GtkXournalClass* cptr) {
    auto* widget_class = reinterpret_cast<GtkWidgetClass*>(cptr);

    widget_class->realize = gtk_xournal_realize;
    widget_class->get_preferred_width = gtk_xournal_get_preferred_width;
    widget_class->get_preferred_height = gtk_xournal_get_preferred_height;
    widget_class->size_allocate = gtk_xournal_size_allocate;

    widget_class->draw = gtk_xournal_draw;

    reinterpret_cast<GObjectClass*>(widget_class)->dispose = gtk_xournal_dispose;
}

auto gtk_xournal_get_visible_area(GtkWidget* widget, const XojPageView* p) -> Rectangle<double>* {
    g_return_val_if_fail(widget != nullptr, nullptr);
    g_return_val_if_fail(GTK_IS_XOURNAL(widget), nullptr);

    GtkXournal* xournal = GTK_XOURNAL(widget);

    GtkAdjustment* vadj = xournal->scrollHandling->getVertical();
    GtkAdjustment* hadj = xournal->scrollHandling->getHorizontal();

    GdkRectangle r2;
    r2.x = static_cast<int>(gtk_adjustment_get_value(hadj));
    r2.y = static_cast<int>(gtk_adjustment_get_value(vadj));
    r2.width = static_cast<int>(gtk_adjustment_get_page_size(hadj));
    r2.height = static_cast<int>(gtk_adjustment_get_page_size(vadj));

    GdkRectangle r1;
    r1.x = p->getX();
    r1.y = p->getY();
    r1.width = p->getDisplayWidth();
    r1.height = p->getDisplayHeight();

    GdkRectangle r3 = {0, 0, 0, 0};
    gdk_rectangle_intersect(&r1, &r2, &r3);

    if (r3.width == 0 && r3.height == 0) {
        return nullptr;
    }

    r3.x -= r1.x;
    r3.y -= r1.y;

    double zoom = xournal->view->getZoom();

    if (r3.x < 0 || r3.y < 0) {
        g_warning("XournalWidget:gtk_xournal_get_visible_area: intersection rectangle coordinates are negative which "
                  "should never happen");
    }

    return new Rectangle<double>(std::max(r3.x, 0) / zoom, std::max(r3.y, 0) / zoom, r3.width / zoom, r3.height / zoom);
}

auto gtk_xournal_get_layout(GtkWidget* widget) -> Layout* {
    g_return_val_if_fail(widget != nullptr, nullptr);
    g_return_val_if_fail(GTK_IS_XOURNAL(widget), nullptr);

    GtkXournal* xournal = GTK_XOURNAL(widget);
    return xournal->layout;
}

static void gtk_xournal_init(GtkXournal* xournal) {
    GtkWidget* widget = GTK_WIDGET(xournal);

    gtk_widget_set_can_focus(widget, true);
}

static void gtk_xournal_get_preferred_width(GtkWidget* widget, gint* minimal_width, gint* natural_width) {
    GtkXournal* xournal = GTK_XOURNAL(widget);
    *minimal_width = *natural_width = xournal->layout->getMinimalWidth();
}

static void gtk_xournal_get_preferred_height(GtkWidget* widget, gint* minimal_height, gint* natural_height) {
    GtkXournal* xournal = GTK_XOURNAL(widget);
    *minimal_height = *natural_height = xournal->layout->getMinimalHeight();
}

/**
 * This method is called while scrolling or after the XournalWidget size has changed
 */
static void gtk_xournal_size_allocate(GtkWidget* widget, GtkAllocation* allocation) {
    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));
    g_return_if_fail(allocation != nullptr);

    gtk_widget_set_allocation(widget, allocation);

    if (gtk_widget_get_realized(widget)) {
        gdk_window_move_resize(gtk_widget_get_window(widget), allocation->x, allocation->y, allocation->width,
                               allocation->height);
    }

    GtkXournal* xournal = GTK_XOURNAL(widget);

    // layout the pages in the XournalWidget
    xournal->layout->layoutPages(allocation->width, allocation->height);
}

static void gtk_xournal_realize(GtkWidget* widget) {
    GdkWindowAttr attributes;

    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));

    gtk_widget_set_realized(widget, true);

    gtk_widget_set_hexpand(widget, true);
    gtk_widget_set_vexpand(widget, true);

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;

    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.event_mask = gtk_widget_get_events(widget) | GDK_EXPOSURE_MASK;

    gint attributes_mask = GDK_WA_X | GDK_WA_Y;

    gtk_widget_set_window(widget, gdk_window_new(gtk_widget_get_parent_window(widget), &attributes, attributes_mask));
    gdk_window_set_user_data(gtk_widget_get_window(widget), widget);
}

static void gtk_xournal_draw_shadow(GtkXournal* xournal, cairo_t* cr, int left, int top, int width, int height,
                                    bool selected) {
    if (selected) {
        Shadow::drawShadow(cr, left - 2, top - 2, width + 4, height + 4);

        Settings* settings = xournal->view->getControl()->getSettings();

        // Draw border
        Util::cairo_set_source_rgbi(cr, settings->getBorderColor());
        cairo_set_line_width(cr, 4.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);

        cairo_rectangle(cr, left, top, width, height);
        cairo_stroke(cr);
    } else {
        Shadow::drawShadow(cr, left, top, width, height);
    }
}

void gtk_xournal_repaint_area(GtkWidget* widget, int x1, int y1, int x2, int y2) {
    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));

    if (x2 < 0 || y2 < 0) {
        return;  // outside visible area
    }

    GtkAllocation alloc = {0};
    gtk_widget_get_allocation(widget, &alloc);

    if (x1 > alloc.width || y1 > alloc.height) {
        return;  // outside visible area
    }

    gtk_widget_queue_draw_area(widget, x1, y1, x2 - x1, y2 - y1);
}

static auto gtk_xournal_draw(GtkWidget* widget, cairo_t* cr) -> gboolean {
    g_return_val_if_fail(widget != nullptr, false);
    g_return_val_if_fail(GTK_IS_XOURNAL(widget), false);

    GtkXournal* xournal = GTK_XOURNAL(widget);

    double x1 = NAN, x2 = NAN, y1 = NAN, y2 = NAN;

    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);

    // Draw background
    Settings* settings = xournal->view->getControl()->getSettings();
    Util::cairo_set_source_rgbi(cr, settings->getBackgroundColor());
    cairo_paint(cr);

    // Add a padding for the shadow of the pages
    Rectangle clippingRect(x1 - 10, y1 - 10, x2 - x1 + 20, y2 - y1 + 20);

    for (auto&& pv: xournal->view->getViewPages()) {
        int px = pv->getX();
        int py = pv->getY();
        int pw = pv->getDisplayWidth();
        int ph = pv->getDisplayHeight();

        if (!clippingRect.intersects(pv->getRect())) {
            continue;
        }

        gtk_xournal_draw_shadow(xournal, cr, px, py, pw, ph, pv->isSelected());

        cairo_save(cr);
        cairo_translate(cr, px, py);

        pv->paintPage(cr, nullptr);
        cairo_restore(cr);
    }

    if (xournal->selection) {
        cairo_save(cr);
        double zoom = xournal->view->getZoom();

        Redrawable* red = xournal->selection->getView();
        cairo_translate(cr, red->getX(), red->getY());

        xournal->selection->paint(cr, zoom);
        cairo_restore(cr);
    }

    if (xournal->setsquareView) {
        auto&& pv = xournal->setsquareView->getView();
        int px = pv->getX();
        int py = pv->getY();

        if (clippingRect.intersects(pv->getRect())) {
            cairo_save(cr);
            cairo_translate(cr, px, py);
            xournal->setsquareView->paint(cr);
            cairo_restore(cr);
        }
    }

    return true;
}

static void gtk_xournal_dispose(GObject* object) {
    g_return_if_fail(object != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(object));
    GtkXournal* xournal = GTK_XOURNAL(object);

    delete xournal->selection;
    xournal->selection = nullptr;

    xournal->setsquareView = nullptr;

    delete xournal->layout;
    xournal->layout = nullptr;

    delete xournal->input;
    xournal->input = nullptr;
}
