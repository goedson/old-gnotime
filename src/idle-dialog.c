/*   Keyboard inactivity timout dialog for GTimeTracker - a time tracker
 *   Copyright (C) 2001,2002,2003 Linas Vepstas <linas@linas.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <glade/glade.h>
#include <gnome.h>
#include <string.h>

#include "ctree.h"
#include "ctree-gnome2.h"
#include "cur-proj.h"
#include "idle-dialog.h"
#include "idle-timer.h"
#include "proj.h"
#include "util.h"


int config_idle_timeout = -1;

struct GttInactiveDialog_s 
{
	GladeXML    *gtxml;
	GtkDialog   *dlg;
	GtkButton   *yes_btn;
	GtkButton   *no_btn;
	GtkLabel    *idle_label;
	GtkLabel    *credit_label;
	GtkLabel    *time_label;
	GtkRange    *scale;
	
	GttProject  *prj;
	IdleTimeout *idt;
	time_t      idle_time;
	time_t      previous_credit;
};


/* =========================================================== */

static void
dialog_close (GObject *obj, GttInactiveDialog *dlg)
{
	dlg->dlg = NULL;
	dlg->gtxml = NULL;
}

/* =========================================================== */

static void
dialog_kill (GObject *obj, GttInactiveDialog *dlg)
{
	gtk_widget_destroy (GTK_WIDGET(dlg->dlg));
	dlg->dlg = NULL;
	dlg->gtxml = NULL;
}

/* =========================================================== */

static void
restart_proj (GObject *obj, GttInactiveDialog *dlg)
{
	ctree_start_timer (dlg->prj);
	dialog_kill (obj, dlg);
}

/* =========================================================== */

static void
adjust_timer (GttInactiveDialog *dlg, time_t adjustment)
{
	GttInterval *ivl;
	time_t stop;
	
	ivl = gtt_project_get_first_interval (dlg->prj);
	stop = gtt_interval_get_stop (ivl);
	stop -= dlg->previous_credit;
	stop += adjustment;
	gtt_interval_set_stop (ivl, stop);

	dlg->previous_credit = adjustment;
}

/* =========================================================== */

static void
display_value (GttInactiveDialog *dlg, time_t credit)
{
	char tbuff [30];
	char mbuff [130];
	char * msg;
	
	/* Set a value for the thingy under the slider */
	if (3600 > credit)
	{
		print_minutes_elapsed (tbuff, 30, credit, TRUE);
		g_snprintf (mbuff, 130, _("%s minutes"), tbuff);
	}
	else 
	{
		print_hours_elapsed (tbuff, 30, credit, FALSE);
		g_snprintf (mbuff, 130, _("%s hours"), tbuff);
	}
	gtk_label_set_text (dlg->time_label, mbuff);
	
	/* Set a value in the main message; show hours,
	 * or minutes, as is more appropriate.
	 */
	if (3600 > credit)
	{
		msg = g_strdup_printf (
		         _("The timer has been credited "
		           "with %s minutes since the last keyboard/mouse "
		           "activity.  If you want to change the amount "
		           "of time credited, use the slider below to "
		           "adjust the value."),
		           tbuff);
	}
	else
	{
		msg = g_strdup_printf (
		         _("The timer has been credited "
		           "with %s hours since the last keyboard/mouse "
		           "activity.  If you want to change the amount "
		           "of time credited, use the slider below to "
		           "adjust the value."),
		           tbuff);
	}
	gtk_label_set_text (dlg->credit_label, msg);
	g_free (msg);

}

/* =========================================================== */

static void
value_changed (GObject *obj, GttInactiveDialog *dlg)
{
	double slider_value;
	time_t credit;

	slider_value = gtk_range_get_value (dlg->scale);
	slider_value /= 90.0;
	slider_value *= dlg->idle_time;

	credit = (time_t) slider_value;
	
	display_value (dlg, credit);  /* display value in GUI */
	adjust_timer (dlg, credit);   /* change value in data store */
}

/* =========================================================== */
/* XXX the new GtkDialog is broken; it can't hide-on-close,
 * unlike to old, deprecated GnomeDialog.  Thus, we have to
 * do a heavyweight re-initialization each time.  Urgh.
 */

static void
inactive_dialog_realize (GttInactiveDialog * id)
{
	GladeXML *gtxml;

	id->prj = NULL;

	gtxml = gtt_glade_xml_new ("glade/idle.glade", "Idle Dialog");
	id->gtxml = gtxml;

	id->dlg = GTK_DIALOG (glade_xml_get_widget (gtxml, "Idle Dialog"));

	id->yes_btn = GTK_BUTTON(glade_xml_get_widget (gtxml, "yes button"));
	id->no_btn  = GTK_BUTTON(glade_xml_get_widget (gtxml, "no button"));
	id->idle_label = GTK_LABEL (glade_xml_get_widget (gtxml, "idle label"));
	id->credit_label = GTK_LABEL (glade_xml_get_widget (gtxml, "credit label"));
	id->time_label = GTK_LABEL (glade_xml_get_widget (gtxml, "time label"));
	id->scale = GTK_RANGE (glade_xml_get_widget (gtxml, "scale"));

	g_signal_connect(G_OBJECT(id->dlg), "destroy",
	          G_CALLBACK(dialog_close), id);

	g_signal_connect(G_OBJECT(id->yes_btn), "clicked",
	          G_CALLBACK(restart_proj), id);

	g_signal_connect(G_OBJECT(id->no_btn), "clicked",
	          G_CALLBACK(dialog_kill), id);

	g_signal_connect(G_OBJECT(id->scale), "value_changed",
	          G_CALLBACK(value_changed), id);

}

/* =========================================================== */

GttInactiveDialog *
inactive_dialog_new (void)
{
	GttInactiveDialog *id;
	GladeXML *gtxml;

	id = g_new0 (GttInactiveDialog, 1);
	id->idt = idle_timeout_new ();
	id->prj = NULL;

	id->gtxml = NULL;

	return id;
}

/* =========================================================== */

void 
show_inactive_dialog (GttInactiveDialog *id)
{
	time_t now;
	char *msg;
	GttProject *prj = cur_proj;

	if (!id) return;
	if (0 > config_idle_timeout) return;

	now = time(0);
	id->idle_time = now - poll_last_activity (id->idt);
	if (id->idle_time <= config_idle_timeout) return;

	/* Due to GtkDialog broken-ness, re-realize the GUI */
	if (NULL == id->gtxml)
	{
		inactive_dialog_realize (id);
	}

	/* Stop the timer on the current project */
	ctree_stop_timer (cur_proj);
	id->prj = prj;

	/* The idle timer can trip because gtt was left running
	 * on a laptop, which was them put in suspend mode (i.e.
	 * by closing the cover).  When the laptop is resumed,
	 * the poll_last_activity will return the many hours/days
	 * that the laptop has been shut down, and merely stoping
	 * the timer (as above) will credit hours/days to the 
	 * current active project.  We don't want this, we need
	 * to undo this damage.
	 */
	id->previous_credit = id->idle_time;
	adjust_timer (id, config_idle_timeout);

	display_value (id, config_idle_timeout);

	/* warn the user */
	msg = g_strdup_printf (
		_("The keyboard and mouse have been idle "
		  "for %d minutes.  The currently running "
		  "project (%s - %s) "
		  "has been stopped. "
		  "Do you want to restart it?"),
		(id->idle_time+30)/60,
		gtt_project_get_title(prj),
		gtt_project_get_desc(prj));
	
	gtk_label_set_text (id->idle_label, msg);
	g_free (msg);

	gtk_widget_show (GTK_WIDGET(id->dlg));
}

/* =========================== END OF FILE ============================== */
