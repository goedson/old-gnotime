/*   Generate gtt-parsed html output for GnoTime - a time tracker
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

#define _GNU_SOURCE
#include <glib.h>
#include <guile/gh.h>
#include <libguile.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <qof.h>

#include "app.h"
#include "ctree.h"
#include "gtt.h"
#include "ghtml.h"
#include "ghtml-deprecated.h"
#include "proj.h"
#include "query.h"
#include "util.h"


/* Design problems:
 * The way this is currently defined, there is no type safety, and
 * we could easily be slipping the wrong addresses to the wrong places.
 * We really should add a type identifier to the the project,
 * task and interval objects.  This type should be in the cdr
 * part of the object, so that it will stop recursion and list-walking.
 * (The utility routines in gtt.scm walk lists and trees; thus,
 * putting a non-pointer in the cdr is an effective way to stop the 
 * recursion.)  The 'daily-totals' object already follows this 
 * convention.
 */

/* ============================================================== */
/* Seems to me like guile screwed the pooch; we need a global! */

GttGhtml *ghtml_guile_global_hack = NULL;   

static SCM
do_ret_did_query (GttGhtml *ghtml)
{
	return SCM_BOOL (ghtml->did_query);
}

static SCM
ret_did_query (void)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_ret_did_query (ghtml);
}

/* ============================================================== */
/* This routine will reverse the order of a scheme list */

static SCM
reverse_list (SCM node_list)
{
	SCM rc, node;
	rc = SCM_EOL;

	while (FALSE == SCM_NULLP(node_list))
	{
		node = SCM_CAR (node_list);
		rc = scm_cons (node, rc);
		node_list = SCM_CDR (node_list);
	}
	return rc;
}

/* ============================================================== */
/* A routine to recursively apply a scheme form to a list of 
 * KVP key names.  It returns the result of the apply. */

typedef enum
{
	GTT_NONE=0,
	GTT_PRJ,
	GTT_TASK,
	GTT_IVL
} PtrType;

static SCM
do_apply_based_on_type (GttGhtml *ghtml, SCM node, 
             PtrType cur_type,				
             SCM (*str_func)(GttGhtml *, const char *),
             SCM (*prj_func)(GttGhtml *, GttProject *),
             SCM (*tsk_func)(GttGhtml *, GttTask *),
             SCM (*ivl_func)(GttGhtml *, GttInterval *))
{
	/* Either a 'symbol or a "quoted string" */
	if (SCM_SYMBOLP(node) || SCM_STRINGP (node))
	{
		SCM rc = SCM_EOL;
		char *str = SCM_STRING_CHARS (node);
		int len = SCM_STRING_LENGTH (node);
		if (0<len) rc = str_func (ghtml, str);
		return rc;
	}
		
	/* If its a number, its in fact a pointer to the C struct. */
	if (SCM_NUMBERP(node))
	{
		SCM rc = SCM_EOL;
		switch (cur_type)
		{
			case GTT_PRJ: {
				GttProject *prj = (GttProject *) scm_num2ulong (node, 
				             SCM_ARG1, "GnoTime::do-apply-based-on-type==project");
				if (prj_func) rc = prj_func (ghtml, prj);
				break;
			}
			case GTT_TASK: {
				GttTask *tsk = (GttTask *) scm_num2ulong (node, 
				             SCM_ARG1, "GnoTime::do-apply-based-on-type==task");
				if (tsk_func) rc = tsk_func (ghtml, tsk);
				break;
			}
			case GTT_IVL: {
				GttInterval *ivl = (GttInterval *) scm_num2ulong (node, 
				             SCM_ARG1, "GnoTime::do-apply-based-on-type==interval");
				if (ivl_func) rc = ivl_func (ghtml, ivl);
				break;
			}
			case GTT_NONE:
				rc = SCM_EOL;
				break;
		}
		return rc;
	}

	/* If its a list, then process the list */
	if (SCM_CONSP(node))
	{
		SCM rc = SCM_EOL;
		SCM node_list = node;

		/* Check to see if there's a type-label of the appropriate 
		 * type.  If so, then strip off the label, and pass back 
		 * car to ourselves, and passing the corrected type.
		 */
		if (FALSE == SCM_NULLP(node))
		{
			SCM type;
			type = SCM_CDR (node);
			if (SCM_SYMBOLP(type) || SCM_STRINGP (type))
			{
				cur_type = GTT_NONE;
				char *buff = SCM_STRING_CHARS (type);

				if ((!strncmp (buff, "gtt-project-ptr",15)) ||
				    (!strncmp (buff, "gtt-project-list",16)))
				{
					cur_type = GTT_PRJ;
				}
				else if (!strncmp (buff, "gtt-task-list", 13))
				{
					cur_type = GTT_TASK;
				}
				else if (!strncmp (buff, "gtt-interval-list", 17))
				{
					cur_type = GTT_IVL;
				}
				else
				{
					g_warning ("Unknown GTT list type\n");
					return SCM_EOL;
				}
				node_list = SCM_CAR (node);
				return do_apply_based_on_type (ghtml, node_list, cur_type, 
				               str_func, prj_func, tsk_func, ivl_func);
			}
		}
		/* Otherwise, we have just a list. Walk that list,
		 * apply recursively to it.
		 */
		while (FALSE == SCM_NULLP(node_list))
		{
			SCM evl;
			node = SCM_CAR (node_list);
			
			evl = do_apply_based_on_type (ghtml, node, cur_type, 
				               str_func, prj_func, tsk_func, ivl_func);
			
			if (FALSE == SCM_NULLP (evl))
			{
				rc = scm_cons (evl, rc);
			}
			node_list = SCM_CDR (node_list);
		}

		/* reverse the list. Ughh */
		/* gh_reverse (rc);  this doesn't work, to it manually */
		rc = reverse_list (rc);
		
		return rc;
	}

	/* If its a null list, do nothing */
	if (SCM_NULLP (node))
	{ 
		return node;
	}
	
	g_warning ("expecting a gtt data object,  got something else\n");
	return SCM_EOL;
}

/* ============================================================== */
/* A routine to recursively apply a scheme form to a hierarchical 
 * list of gtt projects.  It returns the result of the apply. */

static SCM
do_apply_on_string (GttGhtml *ghtml, SCM strs, 
             SCM (*func)(GttGhtml *, const char *))
{
   return do_apply_based_on_type (ghtml, strs,
             GTT_NONE, func, NULL, NULL, NULL);
}

static SCM
do_apply_on_project (GttGhtml *ghtml, SCM project, 
             SCM (*func)(GttGhtml *, GttProject *))
{
   return do_apply_based_on_type (ghtml, project,
             GTT_PRJ, NULL, func, NULL, NULL);
}

static SCM
do_apply_on_task (GttGhtml *ghtml, SCM task, 
             SCM (*func)(GttGhtml *, GttTask *))
{
   return do_apply_based_on_type (ghtml, task,
             GTT_TASK, NULL, NULL, func, NULL);
}


static SCM
do_apply_on_interval (GttGhtml *ghtml, SCM invl, 
             SCM (*func)(GttGhtml *, GttInterval *))
{
   return do_apply_based_on_type (ghtml, invl,
             GTT_IVL, NULL, NULL, NULL, func);
}

/* ============================================================== */

static SCM
kvp_cb (GttGhtml *ghtml, const char *key)
{
	KvpValue *val;
	const char * str;
	if (!ghtml->kvp) return SCM_EOL;
	val = kvp_frame_get_slot (ghtml->kvp, key);
	if (!val) return SCM_EOL;
	str = kvp_value_get_string (val);
	if (!str) return SCM_EOL;
	return scm_mem2string (str, strlen (str));
}

static SCM
ret_kvp_str (SCM key)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_apply_on_string (ghtml, key, kvp_cb);
}

/* ============================================================== */
/* This routine accepts an SCM node, and 'prints' it out.
 * (or tries to). It knows how to print numbers strings and lists.
 */

static SCM
do_show_scm (GttGhtml *ghtml, SCM node)
{
	size_t len;
	char * str = NULL;
	if (NULL == ghtml->write_stream) return SCM_EOL;

	/* Need to test for numbers first, since later tests 
	 * may core-dump guile-1.3.4 if the node was in fact a number. */
	if (SCM_NUMBERP(node))
	{
		char buf[132];
		double x;
		long  ix;

		x = scm_num2dbl (node, "GnoTime::do_show_scm");
		ix = (long) x;

		/* If the number is representable in 32 bits,
		 * and if the fractional part is so small its
		 * not representable as a double, then print it
		 * as an integer. */
		if ((INT_MAX > x) && (-INT_MAX < x) &&
			 ((x-(double)ix) < 4.0*x*DBL_EPSILON) )
		{
			sprintf (buf, "%ld", ix);
		}
		else
		{
			sprintf (buf, "%26.18g", x);
		}
		(ghtml->write_stream) (ghtml, buf, strlen(buf), ghtml->user_data);
	}
	else
	/* either a 'symbol or a "quoted string" */
	if (SCM_SYMBOLP(node) || SCM_STRINGP (node))
	{
		str = SCM_STRING_CHARS (node);
		len = SCM_STRING_LENGTH (node);
		if (0<len) (ghtml->write_stream) (ghtml, str, len, ghtml->user_data);
	}
	else
	if (SCM_CONSP(node))
	{
		SCM node_list = node;
		do
		{
			node = SCM_CAR (node_list);
			do_show_scm (ghtml, node);
			node_list = SCM_CDR (node_list);
		}
		while (SCM_CONSP(node_list));
		do_show_scm (ghtml, node_list);
	}
	else
	if (SCM_BOOLP(node))
	{
		const char *str;
		if (SCM_FALSEP(node)) str = _("False");
		else str = _("True");
		(ghtml->write_stream) (ghtml, str, strlen(str), ghtml->user_data);
	}
	else
	if (SCM_NULLP(node))  
	{
		/* No op; maybe this should be a warning? */
	}
	else
	{
		g_warning ("Don't know how to gtt-show this type\n");
	}

	/* We could return the printed string, but I'm not sure why.. */
	return SCM_EOL;
}

static SCM
show_scm (SCM node_list)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_show_scm (ghtml, node_list);
}

/* ============================================================== */
/* Cheesy hack, this returns a pointer to the currently
 * selected project as a ulong.  Its baaaad,  but acheives its 
 * purpose for now.   Its baad because the C pointer is not
 * currently checked before use.  This could lead to core dumps
 * if the scheme code was bad.  It sure would be nice to be
 * able to check that the pointer is a valid pointer to a gtt
 * project.  For example, maybe projects and tasks should be
 * GObjects, and then would could check the cast.  Later. 
 * --linas
 */

static SCM
do_ret_project (GttGhtml *ghtml, GttProject *prj)
{
	SCM node,rc;
	rc = scm_ulong2num ((unsigned long) prj);

	/* Label the pointer with a type identifier */
	node = scm_mem2string ("gtt-project-ptr", 15);
	rc = scm_cons (rc, node);
	
	return rc;
}

/* The 'selected project' is the project highlighted by the 
 * focus row in the main window.
 */

static SCM
do_ret_selected_project (GttGhtml *ghtml)
{
	GttProject *prj = ctree_get_focus_project (global_ptw);
	return do_ret_project (ghtml, prj);
}

static SCM
ret_selected_project (void)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_ret_selected_project (ghtml);
}

static SCM
do_ret_linked_project (GttGhtml *ghtml)
{
	return do_ret_project (ghtml, ghtml->prj);
}

static SCM
ret_linked_project (void)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_ret_linked_project (ghtml);
}

/* ============================================================== */
/* Control printing of internal links */

static SCM
do_set_links_on (GttGhtml *ghtml)
{
	if (FALSE == ghtml->really_hide_links)
	{
		ghtml->show_links = TRUE;
	}
	return SCM_EOL;
}

static SCM
set_links_on (void)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_set_links_on (ghtml);
}

static SCM
do_set_links_off (GttGhtml *ghtml)
{
	ghtml->show_links = FALSE;
	return SCM_EOL;
}

static SCM
set_links_off (void)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_set_links_off (ghtml);
}

/* ============================================================== */
/** Converts a g_list of pointers into a typed SCM list.  This is 
 *  a generic utility.  It returns rc, where (car rc) is a list of
 *  pointers, and (cdr rc) is the type-string that identifies the 
 *  type of the pointers. */

static SCM
g_list_to_scm (GList * gplist, const char * type)
{
	SCM rc, node;
	GList *n;

	/* Get a pointer to null */
	rc = SCM_EOL;
	if (gplist) 
	{
		/* Get tail of g_list */
		for (n= gplist; n->next; n=n->next) {}
		gplist = n;
		
		/* Walk backwards, creating a scheme list */
		for (n= gplist; n; n=n->prev)
		{
			node = scm_ulong2num ((unsigned long) n->data);
			rc = scm_cons (node, rc);
		}
	}
	
	/* Prepend type label */
	node = scm_mem2string (type, strlen (type));
	rc = scm_cons (rc, node);
	
	return rc;
}

/* ============================================================== */
/* Return a list of all of the projects */

static SCM
do_ret_project_list (GttGhtml *ghtml, GList *proj_list)
{
	SCM rc;
	GList *n;

	/* Get a pointer to null */
	rc = SCM_EOL;
	if (!proj_list) return rc;
	
	/* XXX should use g_list_to_scm() here */
	/* XXX should use type identifier gtt-project-list */
	/* Find the tail */
	for (n= proj_list; n->next; n=n->next) {}
	proj_list = n;
	
	/* Walk backwards, creating a scheme list */
	for (n= proj_list; n; n=n->prev)
	{
		GttProject *prj = n->data;
      SCM node;
#if 0
		GList *subprjs;
		
		/* Handle sub-projects, if any, before the project itself */
		subprjs = gtt_project_get_children (prj);
		if (subprjs)
		{
			node = do_ret_project_list (ghtml, subprjs);
			rc = scm_cons (node, rc);
		}
#endif 
		node = scm_ulong2num ((unsigned long) prj);
		rc = scm_cons (node, rc);
	}
	return rc;
}

static SCM
ret_projects (void)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	
	/* Get list of all top-level projects */
	GList *proj_list = gtt_get_project_list();
	return do_ret_project_list (ghtml, proj_list);
}

static SCM
ret_query_projects (void)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	
	return do_ret_project_list (ghtml, ghtml->query_result);
}

/* ============================================================== */
/* Return a list of all subprojects of a project */

static SCM
do_ret_subprjs (GttGhtml *ghtml, GttProject *prj)
{
	GList *proj_list;
	
	/* Get list of subprojects. */
	proj_list = gtt_project_get_children (prj);
	if (!proj_list) return SCM_EOL;
	return do_ret_project_list (ghtml, proj_list);
}

static SCM
ret_project_subprjs(SCM proj_list)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_apply_on_project (ghtml, proj_list, do_ret_subprjs);
}

/* ============================================================== */
/* Return a list of all of the tasks of a project */

static SCM
do_ret_tasks (GttGhtml *ghtml, GttProject *prj)
{
	SCM rc;
	GList *n, *task_list;

	/* Get a pointer to null */
	rc = SCM_EOL;
	if (!prj) return rc;
	
	/* Get list of tasks, then get tail */
	task_list = gtt_project_get_tasks (prj);
	if (!task_list) return rc;
	
	/* XXX should use g_list_to_scm() here */
	for (n= task_list; n->next; n=n->next) {}
	task_list = n;
	
	/* Walk backwards, creating a scheme list */
	for (n= task_list; n; n=n->prev)
	{
		GttTask *tsk = n->data;
      SCM node;
		
		node = scm_ulong2num ((unsigned long) tsk);
		rc = scm_cons (node, rc);
	}
	return rc;
}

static SCM
ret_tasks (SCM proj_list)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_apply_on_project (ghtml, proj_list, do_ret_tasks);
}

/* ============================================================== */
/* Return a list of all of the intervals of a task */

static SCM
do_ret_intervals (GttGhtml *ghtml, GttTask *tsk)
{
	SCM rc;
	GList *n, *ivl_list;

	/* Oddball hack to make interval datestamp printing work nicely */
	ghtml->last_ivl_time = 0;

	/* Get a pointer to null */
	rc = SCM_EOL;
	if (!tsk) return rc;
	
	/* XXX should use g_list_to_scm() here */
	/* Get list of intervals, then get tail */
	ivl_list = gtt_task_get_intervals (tsk);
	if (!ivl_list) return rc;
	
	for (n= ivl_list; n->next; n=n->next) {}
	ivl_list = n;
	
	/* Walk backwards, creating a scheme list */
	for (n= ivl_list; n; n=n->prev)
	{
		GttInterval *ivl = n->data;
		SCM node;
		
		node = scm_ulong2num ((unsigned long) ivl);
		rc = scm_cons (node, rc);
	}
	return rc;
}

static SCM
ret_intervals (SCM task_list)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_apply_on_task (ghtml, task_list, do_ret_intervals);
}


/* ============================================================== */
/* Return a list of date handles for accessing daily totals */

static SCM
do_ret_daily_totals (GttGhtml *ghtml, GttProject *prj)
{
	SCM rc, rpt;
	int i;
	GArray *arr;  
	time_t earliest;
	struct tm tday;
	
	/* Get a pointer to null */
	rc = SCM_EOL;
	if (!prj) return rc;
	
	/* Get the project data */
	arr = gtt_project_get_daily_buckets (prj, TRUE);
	earliest = gtt_project_get_earliest_start (prj, TRUE);
	
	/* Format the start date */
	localtime_r (&earliest, &tday);
	tday.tm_mday --;

	for (i=0; i< arr->len; i++)
	{
		GttBucket *bu;
		char buff[100];
		SCM node;
		time_t rptdate, secs;
		
		tday.tm_mday ++;
		bu = & g_array_index (arr, GttBucket, i);
		secs = bu->total;

		/* Skip days for which no time has been spent */
		if (0 == secs) continue;

		rpt = SCM_EOL;
		/* Append the list of tasks and intervals for this day */
		node = g_list_to_scm (bu->intervals, "gtt-interval-list");
		rpt = scm_cons (node, rpt);
		node = g_list_to_scm (bu->tasks, "gtt-task-list");
		rpt = scm_cons (node, rpt);
		
		/* XXX should use time_t, and srfi-19 to print, and have a type label */
		/* Print time spent on project this day */
		qof_print_hours_elapsed_buff (buff, 100, secs, TRUE);
		node = scm_mem2string (buff, strlen (buff));
		rpt = scm_cons (node, rpt);
		
		/* XXX report date should be time_t in the middle of the interval */
		/* Print date */
		rptdate = mktime (&tday);
		qof_print_date_buff (buff, 100, rptdate);
		node = scm_mem2string (buff, strlen (buff));
		rpt = scm_cons (node, rpt);

		/* Put a data type in the cdr slot */
		node = scm_mem2string ("gtt-daily", 9);
		rpt = scm_cons (rpt, node);
		
		rc = scm_cons (rpt, rc);
	}
	g_array_free (arr, TRUE);

	return rc;
}

static SCM
ret_daily_totals (SCM proj_list)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_apply_on_project (ghtml, proj_list, do_ret_daily_totals);
}


/* ============================================================== */
/* Define a set of subroutines that accept a scheme list of projects,
 * applies the gtt_project function on each, and then returns a 
 * scheme list containing the results.   
 *
 * For example, ret_project_title() takes a scheme list of gtt
 * projects, gets the project title for each, and then returns
 * a scheme list of the project titles.
 */

#define RET_PROJECT_SIMPLE(RET_FUNC,DO_SIMPLE)                      \
static SCM                                                          \
RET_FUNC (SCM proj_list)                                            \
{                                                                   \
	GttGhtml *ghtml = ghtml_guile_global_hack;                       \
	return do_apply_on_project (ghtml, proj_list, DO_SIMPLE);        \
}


#define RET_PROJECT_STR(RET_FUNC,GTT_GETTER)                        \
static SCM                                                          \
GTT_GETTER##_scm (GttGhtml *ghtml, GttProject *prj)                 \
{                                                                   \
	const char * str = GTT_GETTER (prj);                             \
	if (NULL == str) return SCM_EOL;                                 \
	return scm_mem2string (str, strlen (str));                       \
}                                                                   \
RET_PROJECT_SIMPLE(RET_FUNC,GTT_GETTER##_scm)
		  

#define RET_PROJECT_LONG(RET_FUNC,GTT_GETTER)                       \
static SCM                                                          \
GTT_GETTER##_scm (GttGhtml *ghtml, GttProject *prj)                 \
{                                                                   \
	long i = GTT_GETTER (prj);                                       \
	return scm_long2num (i);                                         \
}                                                                   \
RET_PROJECT_SIMPLE(RET_FUNC,GTT_GETTER##_scm)

                                                                    \
#define RET_PROJECT_ULONG(RET_FUNC,GTT_GETTER)                      \
static SCM                                                          \
GTT_GETTER##_scm (GttGhtml *ghtml, GttProject *prj)                 \
{                                                                   \
	unsigned long i = GTT_GETTER (prj);                              \
	return scm_ulong2num (i);                                        \
}                                                                   \
RET_PROJECT_SIMPLE(RET_FUNC,GTT_GETTER##_scm)



RET_PROJECT_STR   (ret_project_title, gtt_project_get_title)
RET_PROJECT_STR   (ret_project_desc,  gtt_project_get_desc)
RET_PROJECT_STR   (ret_project_notes, gtt_project_get_notes)
RET_PROJECT_ULONG (ret_project_est_start, gtt_project_get_estimated_start)
RET_PROJECT_ULONG (ret_project_est_end,   gtt_project_get_estimated_end)
RET_PROJECT_ULONG (ret_project_due_date,  gtt_project_get_due_date)

RET_PROJECT_LONG  (ret_project_sizing,  gtt_project_get_sizing)
RET_PROJECT_LONG  (ret_project_percent, gtt_project_get_percent_complete)


/* ============================================================== */
/* Handle ret_project_title_link in the almost-standard way,
 * i.e. as
 * RET_PROJECT_STR (ret_project_title, gtt_project_get_title) 
 * except that we need to handle url links as well. 
 */
static SCM
get_project_title_link_scm (GttGhtml *ghtml, GttProject *prj)
{
	if (ghtml->show_links)
	{
		GString *str;
		str = g_string_new (NULL);
		g_string_append_printf (str, "<a href=\"gtt:proj:0x%lx\">", (long) prj);
		g_string_append (str, gtt_project_get_title (prj)); 
		g_string_append (str, "</a>");
		return scm_mem2string (str->str, str->len);
	}
	else 
	{
		const char * str = gtt_project_get_title (prj); 
		return scm_mem2string (str, strlen (str));
	}
}

RET_PROJECT_SIMPLE (ret_project_title_link, get_project_title_link_scm)

/* ============================================================== */

static const char * 
get_urgency (GttProject *prj) 
{
	GttRank rank = gtt_project_get_urgency (prj);
	switch (rank)
	{
		case GTT_LOW:       return _("Low"); 
		case GTT_MEDIUM:    return _("Normal");
		case GTT_HIGH:      return _("Urgent"); 
		case GTT_UNDEFINED: return _("Undefined");
	}
	return _("Undefined");
}

static const char * 
get_importance (GttProject *prj) 
{
	GttRank rank = gtt_project_get_importance (prj);
	switch (rank)
	{
		case GTT_LOW:       return _("Low"); 
		case GTT_MEDIUM:    return _("Medium");
		case GTT_HIGH:      return _("Important"); 
		case GTT_UNDEFINED: return _("Undefined");
	}
	return _("Undefined");
}

static const char * 
get_status (GttProject *prj) 
{
	GttProjectStatus status = gtt_project_get_status (prj);
	switch (status)
	{
		case GTT_NOT_STARTED:  return _("Not Started"); 
		case GTT_IN_PROGRESS:  return _("In Progress"); 
		case GTT_ON_HOLD:      return _("On Hold"); 
		case GTT_CANCELLED:    return _("Cancelled"); 
		case GTT_COMPLETED:    return _("Completed"); 
	}
	return _("Undefined");
}

RET_PROJECT_STR (ret_project_urgency,    get_urgency)
RET_PROJECT_STR (ret_project_importance, get_importance)
RET_PROJECT_STR (ret_project_status,     get_status)
		  
/* ============================================================== */
/* Define a set of subroutines that accept a scheme list of tasks,
 * applies the gtt_task function on each, and then returns a 
 * scheme list containing the results.   
 *
 * For example, ret_task_memo() takes a scheme list of gtt
 * tasks, gets the task memo for each, and then returns
 * a scheme list of the task memos.
 */

#define RET_TASK_SIMPLE(RET_FUNC,GTT_GETTER)                        \
static SCM                                                          \
RET_FUNC (SCM task_list)                                            \
{                                                                   \
	GttGhtml *ghtml = ghtml_guile_global_hack;                       \
	return do_apply_on_task (ghtml, task_list, GTT_GETTER##_scm);    \
}

#define RET_TASK_STR(RET_FUNC,GTT_GETTER)                           \
static SCM                                                          \
GTT_GETTER##_scm (GttGhtml *ghtml, GttTask *tsk)                    \
{                                                                   \
	const char * str = GTT_GETTER (tsk);                             \
	return scm_mem2string (str, strlen (str));                           \
}                                                                   \
                                                                    \
static SCM                                                          \
RET_FUNC (SCM task_list)                                            \
{                                                                   \
	GttGhtml *ghtml = ghtml_guile_global_hack;                       \
	return do_apply_on_task (ghtml, task_list, GTT_GETTER##_scm);    \
}

RET_TASK_STR (ret_task_notes,   gtt_task_get_notes)
		  
/* ============================================================== */
/* Handle ret_task_memo in the almost-standard way,
 * i.e. as
 * RET_TASK_STR (ret_task_memo, gtt_task_get_memo) 
 * except that we need to handle url links as well. 
 */
static SCM
get_task_memo_scm (GttGhtml *ghtml, GttTask *tsk)
{
	if (ghtml->show_links)
	{
		GString *str;
		str = g_string_new (NULL);
		g_string_append_printf (str, "<a href=\"gtt:task:0x%lx\">", (long)tsk);
		g_string_append (str, gtt_task_get_memo (tsk)); 
		g_string_append (str, "</a>");
		return scm_mem2string (str->str, str->len);
	}
	else 
	{
		const char * str = gtt_task_get_memo (tsk); 
		return scm_mem2string (str, strlen (str));
	}
}

static SCM
ret_task_memo (SCM task_list)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_apply_on_task (ghtml, task_list, get_task_memo_scm);
}

/* ============================================================== */
/* Handle ret_task_parent in the almost-standard way,
 * except that we return a pointer object.
 */
static SCM
get_task_parent_scm (GttGhtml *ghtml, GttTask *tsk)
{
	GttProject *prj = gtt_task_get_parent (tsk);
	return do_ret_project (ghtml, prj);
}

static SCM
ret_task_parent (SCM task_list)
{
	GttGhtml *ghtml = ghtml_guile_global_hack;
	return do_apply_on_task (ghtml, task_list, get_task_parent_scm);
}

/* ============================================================== */

static const char *
task_get_billstatus (GttTask *tsk)
{
	switch (gtt_task_get_billstatus (tsk))
	{
		case GTT_HOLD: return _("Hold");
		case GTT_BILL: return _("Bill");
		case GTT_PAID: return _("Paid");
		default: return "";
	}
	return "";
};

static const char *
task_get_billable (GttTask *tsk)
{
	switch (gtt_task_get_billable (tsk))
	{
		case GTT_BILLABLE: return _("Billable");
		case GTT_NOT_BILLABLE: return _("Not Billable");
		case GTT_NO_CHARGE: return _("No Charge");
		default: return "";
	}
	return "";
};

static const char *
task_get_billrate (GttTask *tsk)
{
	switch (gtt_task_get_billrate (tsk))
	{
		case GTT_REGULAR: return _("Regular");
		case GTT_OVERTIME: return _("Overtime");
		case GTT_OVEROVER: return _("Double Overtime");
		case GTT_FLAT_FEE: return _("Flat Fee");
		default: return "";
	}
	return "";
};

static SCM
task_get_time_str_scm (GttGhtml *ghtml, GttTask *tsk)
{
	time_t task_secs;
	char buff[100];

	task_secs = gtt_task_get_secs_ever(tsk);
	qof_print_hours_elapsed_buff (buff, 100, task_secs, TRUE);
	return scm_mem2string (buff, strlen (buff));
}

static SCM
task_get_value_str_scm (GttGhtml *ghtml, GttTask *tsk)
{
	time_t task_secs;
	char buff[100];
	double value;
	GttProject *prj;

	task_secs = gtt_task_get_secs_ever(tsk);
	value = ((double) task_secs) / 3600.0;
	
	prj = gtt_task_get_parent (tsk);
	switch (gtt_task_get_billrate (tsk))
	{
		case GTT_REGULAR: value *= gtt_project_get_billrate (prj); break;
		case GTT_OVERTIME: value *= gtt_project_get_overtime_rate (prj); break;
		case GTT_OVEROVER: value *= gtt_project_get_overover_rate (prj); break;
		case GTT_FLAT_FEE: value = gtt_project_get_flat_fee (prj); break;
		default: value = 0.0;
	}
	/* hack alert should use i18n currency/monetary printing */
	snprintf (buff, 100, "$%.2f", value+0.0049);
						
	return scm_mem2string (buff, strlen (buff));
}

RET_TASK_STR (ret_task_billstatus, task_get_billstatus)
RET_TASK_STR (ret_task_billable,   task_get_billable)
RET_TASK_STR (ret_task_billrate,   task_get_billrate)
RET_TASK_SIMPLE (ret_task_time_str,  task_get_time_str)
RET_TASK_SIMPLE (ret_task_value_str, task_get_value_str)

/* ============================================================== */

#define RET_IVL_SIMPLE(RET_FUNC,GTT_GETTER)                         \
static SCM                                                          \
RET_FUNC (SCM ivl_list)                                             \
{                                                                   \
	GttGhtml *ghtml = ghtml_guile_global_hack;                       \
	return do_apply_on_interval (ghtml, ivl_list, GTT_GETTER##_scm); \
}


#define RET_IVL_STR(RET_FUNC,GTT_GETTER)                            \
static SCM                                                          \
GTT_GETTER##_scm (GttGhtml *ghtml, GttInterval *ivl)                \
{                                                                   \
	const char * str = GTT_GETTER (ivl);                             \
	return scm_mem2string (str, strlen (str));                       \
}                                                                   \
RET_IVL_SIMPLE(RET_FUNC,GTT_GETTER)


#define RET_IVL_ULONG(RET_FUNC,GTT_GETTER)                          \
static SCM                                                          \
GTT_GETTER##_scm (GttGhtml *ghtml, GttInterval *ivl)                \
{                                                                   \
	unsigned long i = GTT_GETTER (ivl);                              \
	return scm_ulong2num (i);                                        \
}                                                                   \
RET_IVL_SIMPLE(RET_FUNC,GTT_GETTER)


RET_IVL_ULONG (ret_ivl_start, gtt_interval_get_start)
RET_IVL_ULONG (ret_ivl_stop,  gtt_interval_get_stop)
RET_IVL_ULONG (ret_ivl_fuzz,  gtt_interval_get_fuzz)

static SCM
get_ivl_elapsed_str_scm (GttGhtml *ghtml, GttInterval *ivl)
{
	char buff[100];
	time_t elapsed;
	elapsed = gtt_interval_get_stop (ivl);
	elapsed -= gtt_interval_get_start (ivl);
	qof_print_hours_elapsed_buff (buff, 100, elapsed, TRUE);
	return scm_mem2string (buff, strlen (buff));
}

RET_IVL_SIMPLE (ret_ivl_elapsed_str, get_ivl_elapsed_str);

static SCM
get_ivl_start_stop_common_str_scm (GttGhtml *ghtml, GttInterval *ivl, 
					 time_t starp, gboolean prt_date)
{
	char buff[100];

	if (prt_date) {
		qof_print_date_time_buff (buff, 100, starp);
	} else {
		qof_print_time_buff (buff, 100, starp);
	}

	if (ghtml->show_links)
	{
		GString *str;
		str = g_string_new (NULL);
		g_string_append_printf (str, "<a href=\"gtt:interval:0x%lx\">", (long) ivl);
		g_string_append (str, buff); 
		g_string_append (str, "</a>");
		return scm_mem2string (str->str, str->len);
	}
	else
	{
		return scm_mem2string (buff, strlen (buff));
	}
	
	return SCM_EOL; /* Not reached */
}

static SCM
get_ivl_start_str_scm (GttGhtml *ghtml, GttInterval *ivl)
{
	gboolean prt_date = TRUE;
	time_t start, prev_stop;
	
	/* Caution! use of the last_ivl_time thing makes this 
	 * stateful in a way that may be surprising ! 
	 */
	start = gtt_interval_get_start (ivl);
	prev_stop = ghtml->last_ivl_time;
	ghtml->last_ivl_time = start;

	if (0 != prev_stop)
	{
		prt_date = qof_is_same_day(start, prev_stop);
	}
	return get_ivl_start_stop_common_str_scm (ghtml, ivl, start, prt_date);
}
RET_IVL_SIMPLE (ret_ivl_start_str, get_ivl_start_str);

static SCM
get_ivl_stop_str_scm (GttGhtml *ghtml, GttInterval *ivl)
{
	gboolean prt_date = TRUE;
	time_t stop, prev_start;
	
	/* Caution! use of the last_ivl_time thing makes this 
	 * stateful in a way that may be surprising ! 
	 */
	stop = gtt_interval_get_stop (ivl);
	prev_start = ghtml->last_ivl_time;
	ghtml->last_ivl_time = stop;
	if (0 != prev_start)
	{
		prt_date = qof_is_same_day(prev_start, stop);
	}
	return get_ivl_start_stop_common_str_scm (ghtml, ivl, stop, prt_date);
}
RET_IVL_SIMPLE (ret_ivl_stop_str, get_ivl_stop_str);


static SCM
get_ivl_fuzz_str_scm (GttGhtml *ghtml, GttInterval *ivl)
{
	char buff[100];

	qof_print_hours_elapsed_buff (buff, 100, gtt_interval_get_fuzz (ivl), TRUE);
	return scm_mem2string (buff, strlen (buff));
}

RET_IVL_SIMPLE (ret_ivl_fuzz_str, get_ivl_fuzz_str);

/* ============================================================== */

static SCM 
my_catch_handler (void *data, SCM tag, SCM throw_args)
{
	printf ("Error: GnoTime caught error during scheme parse\n");
	if (SCM_SYMBOLP(tag))
	{
		char * str  = SCM_SYMBOL_CHARS (tag);
		printf ("\tScheme error was: %s\n", str);
	}
	scm_backtrace(); 

	SCM fmt = scm_makfrom0str ("~S");
	SCM s_str = scm_simple_format (SCM_BOOL_F, fmt, SCM_LIST1(throw_args));
	printf ("\tthrow_args=%s\n", SCM_STRING_CHARS (s_str));

	return SCM_EOL;
}


void
gtt_ghtml_display (GttGhtml *ghtml, const char *filepath,
                   GttProject *prj)
{
	FILE *ph;
	GString *template;
	char *start, *end, *scmstart, *comstart, *comend;
	size_t nr;

	if (!ghtml) return;
	ghtml->prj = prj;

	if (!filepath)
	{
		if (ghtml->error)
		{
			(ghtml->error) (ghtml, 404, NULL, ghtml->user_data);
		}
		return;
	}

	/* Try to get the ghtml file ... */
	ph = fopen (filepath, "r");
	if (!ph)
	{
		if (ghtml->error)
		{
			(ghtml->error) (ghtml, 404, filepath, ghtml->user_data);
		}
		return;
	}

	/* Read in the whole file.  Hopefully its not huge */
	template = g_string_new (NULL);
	while (!feof (ph))
	{
#define BUFF_SIZE 4000
		char buff[BUFF_SIZE+1];
		nr = fread (buff, 1, BUFF_SIZE, ph);
		if (0 >= nr) break;  /* EOF I presume */
		buff[nr] = 0x0;
		g_string_append (template, buff);
	}
	fclose (ph);
		
	
	/* ugh. gag. choke. puke. */
	ghtml_guile_global_hack = ghtml;

	/* Load predefined scheme forms */
	scm_c_primitive_load (gtt_ghtml_resolve_path("gtt.scm", NULL));
	
	/* Now open the output stream for writing */
	if (ghtml->open_stream)
	{
		(ghtml->open_stream) (ghtml, ghtml->user_data);
	}

	/* Loop over input text, looking for scheme markup and 
	 * sgml comments. */
	start = template->str;
	while (start)
	{
		scmstart = NULL;
		
		/* look for scheme markup */
		end = strstr (start, "<?scm");

		/* look for comments, and blow past them. */
		comstart = strstr (start, "<!--");
		if (comstart && comstart < end)
		{
			comend = strstr (comstart, "-->");
			if (comend)
			{
				nr = comend - start;
				end = comend;
			}
			else
			{
				nr = strlen (start);
				end = NULL;
			}
			
			/* write everything that we got before the markup */
			if (ghtml->write_stream)
			{
				(ghtml->write_stream) (ghtml, start, nr, ghtml->user_data);
			}
			start = end;
			continue;
		}

		/* look for  termination of scm markup */
		if (end)
		{
			nr = end - start;
			*end = 0x0;
			scmstart = end+5;
			end = strstr (scmstart, "?>");
			if (end)
			{
				*end = 0;
				end += 2;
			}
		}
		else
		{
			nr = strlen (start);
		}
		
		/* Write everything that we got before the markup */
		if (ghtml->write_stream)
		{
			(ghtml->write_stream) (ghtml, start, nr, ghtml->user_data);
		}

		/* If there is markup, then dispatch */
		if (scmstart)
		{
			// scm_c_eval_string (scmstart);
			gh_eval_str_with_catch (scmstart, my_catch_handler);
			scmstart = NULL;
		}
		start = end;
	}

	if (ghtml->close_stream)
	{
		(ghtml->close_stream) (ghtml, ghtml->user_data);
	}
}

/* ============================================================== */
/* Register callback handlers for various internally defined 
 * scheme forms. 
 */

static int is_inited = 0;

static void
register_procs (void)
{
	scm_c_define_gsubr("gtt-show",               1, 0, 0, show_scm);
	scm_c_define_gsubr("gtt-kvp-str",            1, 0, 0, ret_kvp_str);
	scm_c_define_gsubr("gtt-linked-project",     0, 0, 0, ret_linked_project);
	scm_c_define_gsubr("gtt-selected-project",   0, 0, 0, ret_selected_project);
	scm_c_define_gsubr("gtt-projects",           0, 0, 0, ret_projects);
	scm_c_define_gsubr("gtt-query-results",      0, 0, 0, ret_query_projects);
	scm_c_define_gsubr("gtt-did-query",          0, 0, 0, ret_did_query);

	scm_c_define_gsubr("gtt-tasks",              1, 0, 0, ret_tasks);
	scm_c_define_gsubr("gtt-intervals",          1, 0, 0, ret_intervals);
	scm_c_define_gsubr("gtt-daily-totals",       1, 0, 0, ret_daily_totals);
	
	scm_c_define_gsubr("gtt-links-on",           0, 0, 0, set_links_on);
	scm_c_define_gsubr("gtt-links-off",          0, 0, 0, set_links_off);
	
	scm_c_define_gsubr("gtt-project-subprojects", 1, 0, 0, ret_project_subprjs);
	scm_c_define_gsubr("gtt-project-title",      1, 0, 0, ret_project_title);
	scm_c_define_gsubr("gtt-project-title-link", 1, 0, 0, ret_project_title_link);
	scm_c_define_gsubr("gtt-project-desc",       1, 0, 0, ret_project_desc);
	scm_c_define_gsubr("gtt-project-notes",      1, 0, 0, ret_project_notes);
	scm_c_define_gsubr("gtt-project-urgency",    1, 0, 0, ret_project_urgency);
	scm_c_define_gsubr("gtt-project-importance", 1, 0, 0, ret_project_importance);
	scm_c_define_gsubr("gtt-project-status",     1, 0, 0, ret_project_status);
	scm_c_define_gsubr("gtt-project-estimated-start", 1, 0, 0, ret_project_est_start);
	scm_c_define_gsubr("gtt-project-estimated-end", 1, 0, 0, ret_project_est_end);
	scm_c_define_gsubr("gtt-project-due-date",   1, 0, 0, ret_project_due_date);
	scm_c_define_gsubr("gtt-project-sizing",     1, 0, 0, ret_project_sizing);
	scm_c_define_gsubr("gtt-project-percent-complete", 1, 0, 0, ret_project_percent);
	
	scm_c_define_gsubr("gtt-task-memo",          1, 0, 0, ret_task_memo);
	scm_c_define_gsubr("gtt-task-notes",         1, 0, 0, ret_task_notes);
	scm_c_define_gsubr("gtt-task-billstatus",    1, 0, 0, ret_task_billstatus);
	scm_c_define_gsubr("gtt-task-billable",      1, 0, 0, ret_task_billable);
	scm_c_define_gsubr("gtt-task-billrate",      1, 0, 0, ret_task_billrate);
	scm_c_define_gsubr("gtt-task-time-str",      1, 0, 0, ret_task_time_str);
	scm_c_define_gsubr("gtt-task-value-str",     1, 0, 0, ret_task_value_str);
	scm_c_define_gsubr("gtt-task-parent",        1, 0, 0, ret_task_parent);
	
	scm_c_define_gsubr("gtt-interval-start",     1, 0, 0, ret_ivl_start);
	scm_c_define_gsubr("gtt-interval-stop",      1, 0, 0, ret_ivl_stop);
	scm_c_define_gsubr("gtt-interval-fuzz",      1, 0, 0, ret_ivl_fuzz);
	scm_c_define_gsubr("gtt-interval-elapsed-str", 1, 0, 0, ret_ivl_elapsed_str);
	scm_c_define_gsubr("gtt-interval-start-str", 1, 0, 0, ret_ivl_start_str);
	scm_c_define_gsubr("gtt-interval-stop-str",  1, 0, 0, ret_ivl_stop_str);
	scm_c_define_gsubr("gtt-interval-fuzz-str",  1, 0, 0, ret_ivl_fuzz_str);
	
}


/* ============================================================== */

GttGhtml *
gtt_ghtml_new (void)
{
	GttGhtml *p;

	if (!is_inited)
	{
		is_inited = 1;
		register_procs();

		/* I think I neeed to do this, not sure */
		scm_init_debug();
		scm_init_backtrace();
	}

	p = g_new0 (GttGhtml, 1);

	p->kvp = NULL;
	p->prj = NULL;
	p->query_result = NULL;
	p->did_query = FALSE;
	p->show_links = TRUE;
	p->really_hide_links = FALSE;
	p->last_ivl_time = 0;

	gtt_ghtml_deprecated_init (p);

	return p;
}

void 
gtt_ghtml_destroy (GttGhtml *p)
{
	if (!p) return;

	if (p->query_result) g_list_free (p->query_result);
	// XXX memory leak, but otherwise mystery coredump due to this g_free
	// g_free (p);
}

void 
gtt_ghtml_set_stream (GttGhtml *p, gpointer ud,
                                   GttGhtmlOpenStream op, 
                                   GttGhtmlWriteStream wr,
                                   GttGhtmlCloseStream cl, 
                                   GttGhtmlError er)
{
	if (!p) return;
	p->user_data = ud;
	p->open_stream = op;
	p->write_stream = wr;
	p->close_stream = cl;
	p->error = er;
}

/* This sets the over-ride flag, so that no internal links are shown, 
 * really really for real, when printing out to file. 
 */
void 
gtt_ghtml_show_links (GttGhtml *p, gboolean sl)
{
	if (!p) return;
	p->really_hide_links = (FALSE == sl);
	p->show_links = sl;
}

/* ===================== END OF FILE ==============================  */
