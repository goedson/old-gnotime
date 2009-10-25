/*********************************************************************
 *                
 * Copyright (C) 2009,  Goedson Teixeira Paixao
 *                
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 ********************************************************************/

#include <glib.h>
#include "running-projects.h"

#define GTT_RUNNING_PROJECTS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GTT_TYPE_RUNNING_PROJECTS, GttRunningProjectsPrivate))

static void gtt_running_projects_finalize (GObject *obj);

typedef struct _GttRunningProjectsPrivate GttRunningProjectsPrivate;

struct _GttRunningProjectsPrivate {
	GTree *projects;
};

enum {
	PROJECT_STARTED,
	PROJECT_STOPED,
	LAST_SIGNAL
};

static guint running_projects_signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE(GttRunningProjects, gtt_running_projects, G_TYPE_OBJECT)


GttRunningProjects *
gtt_running_projects_new (void)
{
	return g_object_new (gtt_running_projects_get_type(), NULL);
}

static void
gtt_running_projects_class_init (GttRunningProjectsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gtt_running_projects_finalize;
	g_type_class_add_private (object_class,
							  sizeof (GttRunningProjectsPrivate));
	running_projects_signals[PROJECT_STARTED] =
		g_signal_new ("project_started",
					  G_TYPE_FROM_CLASS (object_class),
					  G_SIGNAL_RUN_LAST,
					  0,
					  NULL,
					  NULL,
					  g_cclosure_marshal_VOID__VOID,
					  G_TYPE_NONE,
					  1,
					  GTT_TYPE_RUNNING_PROJECTS);

	running_projects_signals[PROJECT_STOPED] =
		g_signal_new ("project_stoped",
					  G_TYPE_FROM_CLASS (object_class),
					  G_SIGNAL_RUN_LAST,
					  0,
					  NULL,
					  NULL,
					  g_cclosure_marshal_VOID__VOID,
					  G_TYPE_NONE,
					  1,
					  GTT_TYPE_RUNNING_PROJECTS);
}

static void
gtt_running_projects_init (GttRunningProjects *rp)
{
	GttRunningProjectsPrivate *priv = GTT_RUNNING_PROJECTS_GET_PRIVATE (rp);
	priv->projects = g_tree_new_full (gtt_project_cmp, NULL, NULL, NULL);
}

static void
gtt_running_projects_finalize(GObject *rp)
{
	GttRunningProjectsPrivate *priv = GTT_RUNNING_PROJECTS_GET_PRIVATE (rp);
	if (priv->projects != NULL)
	{
		g_tree_destroy (priv->projects);
		priv->projects = NULL;
	}
}

void
gtt_running_projects_run_project(GttRunningProjects *rp, GttProject *prj)
{
	GttRunningProjectsPrivate *priv = GTT_RUNNING_PROJECTS_GET_PRIVATE (rp);

	gpointer value = g_tree_lookup (priv->projects, prj);
	g_return_if_fail (value == NULL);

	g_tree_insert (priv->projects, prj, prj);
	gtt_project_timer_start (prj);

	g_signal_emit (rp, running_projects_signals[PROJECT_STARTED], 0, prj);
}

void
gtt_running_projects_stop_project (GttRunningProjects *rp, GttProject *prj)
{
	GttRunningProjectsPrivate *priv = GTT_RUNNING_PROJECTS_GET_PRIVATE (rp);

	gpointer value = g_tree_lookup (priv->projects, prj);

	g_return_if_fail (value != NULL);

	g_tree_remove (priv->projects, prj);
	gtt_project_timer_stop (prj);

	g_signal_emit (rp, running_projects_signals[PROJECT_STOPED], 0, prj);
}


static gboolean
emit_stoped_signal (gpointer key, gpointer value, gpointer data)
{
	GttProject *prj = (GttProject *) key;
	GttRunningProjects *rp = GTT_RUNNING_PROJECTS (data);

	g_signal_emit (rp, running_projects_signals[PROJECT_STOPED], 0, prj);
}

static gboolean
update_project_timer (gpointer key, gpointer value, gpointer data)
{
	GttProject * prj = (GttProject *) key;
	gtt_project_timer_update (prj);
	return FALSE;
}

void
gtt_running_projects_stop_all(GttRunningProjects *rp)
{
	GttRunningProjectsPrivate *priv = GTT_RUNNING_PROJECTS_GET_PRIVATE (rp);

	g_tree_foreach (priv->projects, update_project_timer, NULL);
	g_tree_foreach (priv->projects, emit_stoped_signal, rp);
	g_tree_destroy (priv->projects);
	priv->projects = g_tree_new_full (gtt_project_cmp, NULL, NULL, NULL);
}

gint
gtt_running_projects_nprojects(GttRunningProjects *rp)
{
	GttRunningProjectsPrivate *priv = GTT_RUNNING_PROJECTS_GET_PRIVATE (rp);

	return g_tree_nnodes(priv->projects);
}

gboolean
gtt_running_projects_contains(GttRunningProjects *rp, GttProject *prj)
{
	GttRunningProjectsPrivate *priv = GTT_RUNNING_PROJECTS_GET_PRIVATE (rp);

	return g_tree_lookup (priv->projects, prj) != NULL;
}

void
gtt_running_projects_foreach (GttRunningProjects *rp, GTraverseFunc func, gpointer user_data)
{
	GttRunningProjectsPrivate *priv = GTT_RUNNING_PROJECTS_GET_PRIVATE (rp);

	g_tree_foreach (priv->projects, func, user_data);
}


static gboolean
list_project_ids (gpointer key, gpointer value, gpointer data)
{
	GttProject *proj = (GttProject *)key;
	*(GSList **)data = (gpointer) g_slist_prepend (*(GSList **)data, GINT_TO_POINTER(gtt_project_get_id (proj)));
	return FALSE;
}

GSList *
gtt_running_projects_get_project_id_list (GttRunningProjects *rp)
{
	GttRunningProjectsPrivate *priv = GTT_RUNNING_PROJECTS_GET_PRIVATE (rp);

	GSList *list = NULL;

	g_tree_foreach (priv->projects, list_project_ids, &list); 

	return list;
}
