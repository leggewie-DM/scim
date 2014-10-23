/* scimtrayicon.c
 * converted from eggtrayicon.c in libegg.
 *
 * Copyright (C) 2002 Anders Carlsson <andersca@gnu.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <string.h>
#include <libintl.h>

#include "scimtrayicon.h"

#include "scim_private.h"

#include <gdk/gdkx.h>
#include <X11/Xatom.h>

#define SYSTEM_TRAY_REQUEST_DOCK     0
#define SYSTEM_TRAY_BEGIN_MESSAGE    1
#define SYSTEM_TRAY_CANCEL_MESSAGE   2

#define SYSTEM_TRAY_ORIENTATION_HORZ 0
#define SYSTEM_TRAY_ORIENTATION_VERT 1

enum {
    PROP_0,
    PROP_ORIENTATION
};

static GtkPlugClass *parent_class = NULL;

static void scim_tray_icon_init         (ScimTrayIcon      *icon);
static void scim_tray_icon_class_init   (ScimTrayIconClass *klass);

static void scim_tray_icon_get_property (GObject           *object,
                                         guint              prop_id,
                                         GValue            *value,
                                         GParamSpec        *pspec);

static void scim_tray_icon_realize      (GtkWidget         *widget);
static void scim_tray_icon_unrealize    (GtkWidget         *widget);

static void scim_tray_icon_update_manager_window (ScimTrayIcon *icon);

GType
scim_tray_icon_get_type (void)
{
    static GType our_type = 0;

    if (our_type == 0) {
        static const GTypeInfo our_info = {
            sizeof (ScimTrayIconClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) scim_tray_icon_class_init,
            NULL, /* class_finalize */
            NULL, /* class_data */
            sizeof (ScimTrayIcon),
            0,    /* n_preallocs */
            (GInstanceInitFunc) scim_tray_icon_init,
            0
        };
        our_type = g_type_register_static (GTK_TYPE_PLUG, "ScimTrayIcon", &our_info, 0);
    }

    return our_type;
}

static void
scim_tray_icon_init (ScimTrayIcon *icon)
{
    icon->stamp = 1;
    icon->orientation = GTK_ORIENTATION_HORIZONTAL;
  
    gtk_widget_add_events (GTK_WIDGET (icon), GDK_PROPERTY_CHANGE_MASK);
}

static void
scim_tray_icon_class_init (ScimTrayIconClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

    parent_class = g_type_class_peek_parent (klass);

    gobject_class->get_property = scim_tray_icon_get_property;

    widget_class->realize   = scim_tray_icon_realize;
    widget_class->unrealize = scim_tray_icon_unrealize;

    g_object_class_install_property (gobject_class,
                                     PROP_ORIENTATION,
                                     g_param_spec_enum ("orientation",
                                     _("Orientation"),
                                     _("The orientation of the tray."),
                                     GTK_TYPE_ORIENTATION,
                                     GTK_ORIENTATION_HORIZONTAL,
                                     G_PARAM_READABLE));
}

static void
scim_tray_icon_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    ScimTrayIcon *icon = SCIM_TRAY_ICON (object);

    switch (prop_id) {
        case PROP_ORIENTATION:
            g_value_set_enum (value, icon->orientation);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
scim_tray_icon_get_orientation_property (ScimTrayIcon *icon)
{
    Display *xdisplay;
    Atom type;
    int format;
    union {
        gulong *prop;
        guchar *prop_ch;
    } prop = { NULL };
    gulong nitems;
    gulong bytes_after;
    int error, result;

    g_assert (icon->manager_window != None);

    xdisplay = GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (icon)));

    gdk_error_trap_push ();
    type = None;
    result = XGetWindowProperty (xdisplay,
                                 icon->manager_window,
                                 icon->orientation_atom,
                                 0, G_MAXLONG, FALSE,
                                 XA_CARDINAL,
                                 &type, &format, &nitems,
                                 &bytes_after, &(prop.prop_ch));

    error = gdk_error_trap_pop ();

    if (error || result != Success)
        return;

    if (type == XA_CARDINAL) {
        GtkOrientation orientation;

        orientation = (prop.prop [0] == SYSTEM_TRAY_ORIENTATION_HORZ) ?
                        GTK_ORIENTATION_HORIZONTAL :
                        GTK_ORIENTATION_VERTICAL;

        if (icon->orientation != orientation) {
            icon->orientation = orientation;
            g_object_notify (G_OBJECT (icon), "orientation");
        }
    }

    if (prop.prop)
        XFree (prop.prop);
}

static GdkFilterReturn
scim_tray_icon_manager_filter (GdkXEvent *xevent, GdkEvent *event, gpointer user_data)
{
    ScimTrayIcon *icon = user_data;
    XEvent *xev = (XEvent *)xevent;

    if (xev->xany.type == ClientMessage &&
        xev->xclient.message_type == icon->manager_atom &&
        xev->xclient.data.l[1] == icon->selection_atom) {
        scim_tray_icon_update_manager_window (icon);
    } else if (xev->xany.window == icon->manager_window) {
        if (xev->xany.type == PropertyNotify &&
            xev->xproperty.atom == icon->orientation_atom) {
            scim_tray_icon_get_orientation_property (icon);
        }
        if (xev->xany.type == DestroyNotify) {
            scim_tray_icon_update_manager_window (icon);
        }
    }

    return GDK_FILTER_CONTINUE;
}

static void
scim_tray_icon_unrealize (GtkWidget *widget)
{
    ScimTrayIcon *icon = SCIM_TRAY_ICON (widget);
    GdkWindow *root_window;

    if (icon->manager_window != None) {
        GdkWindow *gdkwin;

#if GTK_CHECK_VERSION(3, 0, 0)
        gdkwin = gdk_x11_window_lookup_for_display (gtk_widget_get_display (widget),
#else
        gdkwin = gdk_window_lookup_for_display (gtk_widget_get_display (widget),
#endif
                                                icon->manager_window);

        gdk_window_remove_filter (gdkwin, scim_tray_icon_manager_filter, icon);
    }

    root_window = gdk_screen_get_root_window (gtk_widget_get_screen (widget));

    gdk_window_remove_filter (root_window, scim_tray_icon_manager_filter, icon);

    if (GTK_WIDGET_CLASS (parent_class)->unrealize)
        (* GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);
}

static void
scim_tray_icon_send_manager_message (ScimTrayIcon *icon,
                                     long          message,
                                     Window        window,
                                     long          data1,
                                     long          data2,
                                     long          data3)
{
    XClientMessageEvent ev;
    Display *display;
  
    ev.type = ClientMessage;
    ev.window = window;
    ev.message_type = icon->system_tray_opcode_atom;
    ev.format = 32;
#if GTK_CHECK_VERSION(2, 14, 0)
    ev.data.l[0] = gdk_x11_get_server_time (gtk_widget_get_window (GTK_WIDGET (icon)));
#else
    ev.data.l[0] = gdk_x11_get_server_time (GTK_WIDGET (icon)->window);
#endif
    ev.data.l[1] = message;
    ev.data.l[2] = data1;
    ev.data.l[3] = data2;
    ev.data.l[4] = data3;

    display = GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (icon)));

    gdk_error_trap_push ();
    XSendEvent (display, icon->manager_window, False, NoEventMask, (XEvent *)&ev);
    XSync (display, False);
    gint error_code = gdk_error_trap_pop ();
}

static void
scim_tray_icon_send_dock_request (ScimTrayIcon *icon)
{
    scim_tray_icon_send_manager_message (icon,
        SYSTEM_TRAY_REQUEST_DOCK,
        icon->manager_window,
        gtk_plug_get_id (GTK_PLUG (icon)),
        0, 0);
}

static void
scim_tray_icon_update_manager_window (ScimTrayIcon *icon)
{
    Display *xdisplay;
  
    xdisplay = GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (icon)));
  
    if (icon->manager_window != None) {
        GdkWindow *gdkwin;

#if GTK_CHECK_VERSION(3, 0, 0)
        gdkwin = gdk_x11_window_lookup_for_display (
#else
        gdkwin = gdk_window_lookup_for_display (
#endif
                            gtk_widget_get_display (GTK_WIDGET (icon)),
                            icon->manager_window);

        gdk_window_remove_filter (gdkwin, scim_tray_icon_manager_filter, icon);
    }
  
    XGrabServer (xdisplay);
  
    icon->manager_window = XGetSelectionOwner (xdisplay, icon->selection_atom);

    if (icon->manager_window != None)
        XSelectInput (xdisplay,
                      icon->manager_window,
                      StructureNotifyMask|PropertyChangeMask);

    XUngrabServer (xdisplay);
    XFlush (xdisplay);

    if (icon->manager_window != None) {
        GdkWindow *gdkwin;

#if GTK_CHECK_VERSION(3, 0, 0)
        gdkwin = gdk_x11_window_lookup_for_display (
#else
        gdkwin = gdk_window_lookup_for_display (
#endif
                            gtk_widget_get_display (GTK_WIDGET (icon)),
                            icon->manager_window);
        
        gdk_window_add_filter (gdkwin, scim_tray_icon_manager_filter, icon);
 
        /* Send a request that we'd like to dock */
        scim_tray_icon_send_dock_request (icon);
 
        scim_tray_icon_get_orientation_property (icon);
    }
}

static gboolean
transparent_expose_event (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
#if GTK_CHECK_VERSION(3, 0, 0)
#else
#if GTK_CHECK_VERSION(2, 14, 0)
    gdk_window_clear_area (gtk_widget_get_window (widget), event->area.x, event->area.y,
#else
    gdk_window_clear_area (widget->window, event->area.x, event->area.y,
#endif
                           event->area.width, event->area.height);
#endif
    return FALSE;
}

static void
make_transparent_again (GtkWidget *widget, GtkStyle *previous_style,
                        gpointer user_data)
{
#if GTK_CHECK_VERSION(3, 0, 0)
#elif GTK_CHECK_VERSION(2, 14, 0)
    gdk_window_set_back_pixmap (gtk_widget_get_window (widget), NULL, TRUE);
#else
    gdk_window_set_back_pixmap (widget->window, NULL, TRUE);
#endif
}

static void
make_transparent (GtkWidget *widget, gpointer user_data)
{
#if GTK_CHECK_VERSION(2, 18, 0)
    if (!gtk_widget_get_has_window (widget) || gtk_widget_get_app_paintable (widget))
#else
    if (GTK_WIDGET_NO_WINDOW (widget) || GTK_WIDGET_APP_PAINTABLE (widget))
#endif
        return;

    gtk_widget_set_app_paintable (widget, TRUE);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_widget_set_double_buffered (widget, FALSE);
    G_GNUC_END_IGNORE_DEPRECATIONS
#if GTK_CHECK_VERSION(3, 0, 0)
#elif GTK_CHECK_VERSION(2, 14, 0)
    gdk_window_set_back_pixmap (gtk_widget_get_window (widget), NULL, TRUE);
#else
    gdk_window_set_back_pixmap (widget->window, NULL, TRUE);
#endif
    g_signal_connect (widget, "expose_event",
                      G_CALLBACK (transparent_expose_event), NULL);
    g_signal_connect_after (widget, "style_set",
                            G_CALLBACK (make_transparent_again), NULL);
}

static void
scim_tray_icon_realize (GtkWidget *widget)
{
    ScimTrayIcon *icon = SCIM_TRAY_ICON (widget);
    GdkScreen *screen;
    GdkDisplay *display;
    Display *xdisplay;
    char buffer[256];
    GdkWindow *root_window;

    if (GTK_WIDGET_CLASS (parent_class)->realize)
        GTK_WIDGET_CLASS (parent_class)->realize (widget);

    make_transparent (widget, NULL);

    screen = gtk_widget_get_screen (widget);
    display = gdk_screen_get_display (screen);
    xdisplay = gdk_x11_display_get_xdisplay (display);

    /* Now see if there's a manager window around */
    g_snprintf (buffer, sizeof (buffer),
                "_NET_SYSTEM_TRAY_S%d",
                gdk_screen_get_number (screen));

    icon->selection_atom = XInternAtom (xdisplay, buffer, False);
  
    icon->manager_atom = XInternAtom (xdisplay, "MANAGER", False);
  
    icon->system_tray_opcode_atom = XInternAtom (xdisplay,
                                                 "_NET_SYSTEM_TRAY_OPCODE",
                                                 False);

    icon->orientation_atom = XInternAtom (xdisplay,
                                          "_NET_SYSTEM_TRAY_ORIENTATION",
                                          False);

    scim_tray_icon_update_manager_window (icon);

    root_window = gdk_screen_get_root_window (screen);
  
    /* Add a root window filter so that we get changes on MANAGER */
    gdk_window_add_filter (root_window, scim_tray_icon_manager_filter, icon);
}

ScimTrayIcon *
scim_tray_icon_new_for_xscreen (Screen *xscreen, const char *name)
{
    GdkDisplay *display;
    GdkScreen *screen;

    display = gdk_x11_lookup_xdisplay (DisplayOfScreen (xscreen));
    screen = gdk_display_get_screen (display, XScreenNumberOfScreen (xscreen));

    return scim_tray_icon_new_for_screen (screen, name);
}

ScimTrayIcon *
scim_tray_icon_new_for_screen (GdkScreen *screen, const char *name)
{
    g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

    return g_object_new (SCIM_TYPE_TRAY_ICON, "screen", screen, "title", name, NULL);
}

ScimTrayIcon*
scim_tray_icon_new (const gchar *name)
{
    return g_object_new (SCIM_TYPE_TRAY_ICON, "title", name, NULL);
}

guint
scim_tray_icon_send_message (ScimTrayIcon *icon,
                             gint          timeout,
                             const gchar  *message,
                             gint          len)
{
    guint stamp;
  
    g_return_val_if_fail (SCIM_IS_TRAY_ICON (icon), 0);
    g_return_val_if_fail (timeout >= 0, 0);
    g_return_val_if_fail (message != NULL, 0);
               
    if (icon->manager_window == None)
        return 0;
 
    if (len < 0)
        len = strlen (message);
 
    stamp = icon->stamp++;
    
    /* Get ready to send the message */
    scim_tray_icon_send_manager_message (icon,
                SYSTEM_TRAY_BEGIN_MESSAGE,
                (Window)gtk_plug_get_id (GTK_PLUG (icon)),
                timeout, len, stamp);
 
    /* Now to send the actual message */
    gdk_error_trap_push ();
    while (len > 0) {
        XClientMessageEvent ev;
        Display *xdisplay;
 
        xdisplay = GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (icon)));
        
        ev.type = ClientMessage;
        ev.window = (Window)gtk_plug_get_id (GTK_PLUG (icon));
        ev.format = 8;
        ev.message_type = XInternAtom (xdisplay,
                       "_NET_SYSTEM_TRAY_MESSAGE_DATA", False);
        if (len > 20) {
            memcpy (&ev.data, message, 20);
            len -= 20;
            message += 20;
        } else {
            memcpy (&ev.data, message, len);
            len = 0;
        }
 
        XSendEvent (xdisplay, icon->manager_window,
                    False, StructureNotifyMask, (XEvent *)&ev);

        XSync (xdisplay, False);
    }
    gint error_code = gdk_error_trap_pop ();
 
    return stamp;
}

void
scim_tray_icon_cancel_message (ScimTrayIcon *icon,
                               guint         id)
{
    g_return_if_fail (SCIM_IS_TRAY_ICON (icon));
    g_return_if_fail (id > 0);
  
    scim_tray_icon_send_manager_message (icon,
                        SYSTEM_TRAY_CANCEL_MESSAGE,
                        (Window)gtk_plug_get_id (GTK_PLUG (icon)),
                        id, 0, 0);
}

GtkOrientation
scim_tray_icon_get_orientation (ScimTrayIcon *icon)
{
    g_return_val_if_fail (SCIM_IS_TRAY_ICON (icon), GTK_ORIENTATION_HORIZONTAL);

    return icon->orientation;
}

/*
vi:ts=4:nowrap:ai:expandtab
*/
