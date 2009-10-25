
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

#ifndef __RUNNING_PROJECTS_H__
#define __RUNNING_PROJECTS_H__

#include <glib-object.h>
#include <glib.h>

#include "proj.h" 



#define GTT_TYPE_RUNNING_PROJECTS (gtt_running_projects_get_type())
#define GTT_RUNNING_PROJECTS(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTT_TYPE_RUNNING_PROJECTS, GttRunningProjects))
#define GTT_RUNNING_PROJECTS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GTT_TYPE_RUNNING_PROJECTS, GttRunningProjectsClass))
#define GTT_IS_RUNNING_PROJECTS(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTT_RUNNING_PROJECTS))
#define GTT_IS_RUNNING_PROJECTS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTT_RUNNING_PROJECTS))
#define GTT_RUNNING_PROJECTS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GTT_RUNNING_PROJECTS, GttRunningProjectsClass))

typedef struct _GttRunningProjects GttRunningProjects;
typedef struct _GttRunningProjectsClass GttRunningProjectsClass;

struct _GttRunningProjects
{
	GObject parent;
};

struct _GttRunningProjectsClass
{
	GObjectClass parent_class;
};

GType gtt_running_projects_get_type (void);

GttRunningProjects *gtt_running_projects_new (void);

void gtt_running_projects_run_project(GttRunningProjects *rp, GttProject *prj);

void gtt_running_projects_stop_project(GttRunningProjects *rp, GttProject *prj);

void gtt_running_projects_stop_all(GttRunningProjects *rp);

gint gtt_running_projects_nprojects(GttRunningProjects *rp);

gboolean gtt_running_projects_contains (GttRunningProjects *rp, GttProject *prj);

void gtt_running_projects_foreach (GttRunningProjects *rp, GTraverseFunc func, gpointer user_data);

GSList *gtt_running_projects_get_project_id_list (GttRunningProjects *rp);


#endif
