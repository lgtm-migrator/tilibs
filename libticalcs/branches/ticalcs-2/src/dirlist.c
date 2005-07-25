/* Hey EMACS -*- linux-c -*- */
/* $Id$ */

/*  libticalcs - Ti Calculator library, a part of the TiLP project
 *  Copyright (C) 1999-2005  Romain Li�vin
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Utility functions for directory list (tree mangement).
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gettext.h"
#include "ticalcs.h"
#include "logging.h"

static tboolean free_varentry(TNode* node, tpointer data)
{
#if 0
	if(node)
	{
		printf("<%p> ", node);
		if(node->data)
		{
			VarEntry* ve = node->data;

			printf("<<%p>> ", ve);
			printf("<%s>\n", tifiles_transcode_varname_static(CALC_TI84P, ve->name, ve->type));
		}
	}
#else
	/*
	if (node)
		if(node->data)
			tifiles_ve_delete(node->data);
			*/
#endif

	return FALSE;
}

/**
 * ticalcs_dirlist_destroy:
 * @tree: the tree to destroy (var or app).
 *
 * Destroy the whole tree create by #ticalcs_calc_get_dirlist.
 *
 * Return value: none.
 **/
TIEXPORT void TICALL ticalcs_dirlist_destroy(TNode** tree)
{
	if (*tree != NULL) 
	{
		t_node_traverse(*tree, T_IN_ORDER, G_TRAVERSE_LEAVES, -1, free_varentry, NULL);
		t_node_destroy(*tree);
		*tree = NULL;
	}
}

static void dirlist_display_vars(TNode* tree)
{
  TNode *vars = tree;
  TreeInfo *info = (TreeInfo *)(tree->data);
  int i, j, k;
  char trans[10];

  printf(  "+------------------+----------+----+----+----------+----------+\n");
  printf(_("| B. name          | T. name  |Attr|Type| Size     | Folder   |\n"));
  printf(  "+------------------+----------+----+----+----------+----------+\n");

  for (i = 0; i < (int)t_node_n_children(vars); i++)	// parse folders
  {
    TNode *parent = t_node_nth_child(vars, i);
    VarEntry *fe = (VarEntry *) (parent->data);

    if (fe != NULL) 
	{
	  tifiles_transcode_varname (info->model, trans, fe->name, fe->type);

      printf("| ");
      for (k = 0; k < 8; k++)
		printf("%02X", (uint8_t) (fe->name)[k]);
      printf(" | ");	
      printf("%8s", trans);
      printf(" | ");
      printf("%2i", fe->attr);
      printf(" | ");
      printf("%02X", fe->type);
      printf(" | ");
      printf("%08X", fe->size);
      printf(" | ");
      printf("%8s", fe->folder);
      printf(" |");
	  printf("\n");
    }

    for (j = 0; j < (int)t_node_n_children(parent); j++)	//parse variables
    {
      TNode *child = t_node_nth_child(parent, j);
      VarEntry *ve = (VarEntry *) (child->data);

	  tifiles_transcode_varname (info->model, trans, ve->name, ve->type);

      printf("| ");
      for (k = 0; k < 8; k++) 
		printf("%02X", (uint8_t) (ve->name)[k]);
      printf(" | ");
      printf("%8s", trans);
      printf(" | ");
      printf("%2i", ve->attr);
      printf(" | ");
      printf("%02X", ve->type);
      printf(" | ");
      printf("%08X", ve->size);
      printf(" | ");
      printf("%8s", ve->folder);
      printf(" |");
	  printf("\n");
    }
  }
  if (!i)
    printf(_("  No variables"));

  printf(_("+------------------+----------+----+----+----------+----------+"));
  printf("\n");
}

static void dirlist_display_apps(TNode* tree)
{
	TNode *apps = tree;
  TreeInfo *info = (TreeInfo *)(tree->data);
  int i, k;
  char trans[10];

  printf(  "+------------------+----------+----+----+----------+\n");
  printf(_("| B. name          | T. name  |Attr|Type| Size     |\n"));
  printf(  "+------------------+----------+----+----+----------+\n");

  for (i = 0; i < (int)t_node_n_children(apps); i++) 
  {
    TNode *child = t_node_nth_child(apps, i);
    VarEntry *ve = (VarEntry *) (child->data);

	tifiles_transcode_varname (info->model, trans, ve->name, ve->type);

    printf("| ");
    for (k = 0; k < 8; k++)
      printf("%02X", (uint8_t) (ve->name)[k]);
    printf(" | ");
    printf("%8s", trans);
    printf(" | ");
    printf("%2i", ve->attr);
    printf(" | ");
    printf("%02X", ve->type);
    printf(" | ");
    printf("%08X", ve->size);
    printf(" |");
	printf("\n");
  }
  if (!i)
  {
	printf(_("+ No applications  |          |    |    |          +"));
	printf("\n");
  }

  printf("+------------------+----------+----+----+----------+");
  printf("\n");
}

/**
 * ticalcs_dirlist_display:
 * @tree: the tree to display (var or app).
 *
 * Display to stdout the tree content formatted in a tab.
 *
 * Return value: none.
 **/
TIEXPORT void TICALL ticalcs_dirlist_display(TNode* tree)
{
	TreeInfo *info = (TreeInfo *)(tree->data);
	char *node_name = info->type;
  
	if (tree == NULL)
		return;

    if (!strcmp(node_name, VAR_NODE_NAME))
		dirlist_display_vars(tree);
    else if (!strcmp(node_name, APP_NODE_NAME))
	    dirlist_display_apps(tree);
}

/**
 * ticalcs_dirlist_var_exist:
 * @tree: the tree to display (var or app).
 * @full_name: the full name of var to search for.
 *
 * Parse the tree for the given varname & folder.
 *
 * Return value: a pointer on the #VarEntry found or NULL if not found.
 **/
TIEXPORT VarEntry *TICALL ticalcs_dirlist_var_exist(TNode* tree, char *full_name)
{
	int i, j;
	TNode *vars = tree;
	char fldname[18];
	char varname[18];
	TreeInfo *info = (TreeInfo *)(tree->data);
	char *node_name = info->type;

	strcpy(fldname, tifiles_get_fldname(full_name));
	strcpy(varname, tifiles_get_varname(full_name));

	if (tree == NULL)
		return NULL;

	if (strcmp(node_name, VAR_NODE_NAME))
		return NULL;

	for (i = 0; i < (int)t_node_n_children(vars); i++)	// parse folders
	{
		TNode *parent = t_node_nth_child(vars, i);
		VarEntry *fe = (VarEntry *) (parent->data);

		if ((fe != NULL) && strcmp(fe->name, fldname))
			continue;

		for (j = 0; j < (int)t_node_n_children(parent); j++)	//parse variables
		{
			TNode *child = t_node_nth_child(parent, j);
			VarEntry *ve = (VarEntry *) (child->data);

			if (!strcmp(ve->name, varname))
				return ve;
		}
	}

	return NULL;
}

/**
 * ticalcs_dirlist_app_exist:
 * @tree: the tree to display (var or app).
 * @app_name: the name of app to search for.
 *
 * Parse the tree for the given application name.
 *
 * Return value: a pointer on the #VarEntry found or NULL if not found.
 **/
TIEXPORT VarEntry *TICALL ticalcs_dirlist_app_exist(TNode* tree, char *appname)
{
	int i;
	TNode *apps = tree;
	TreeInfo *info = (TreeInfo *)(apps->data);
	char *node_name = info->type;

	if (tree == NULL)
		return NULL;

	if (strcmp(node_name, APP_NODE_NAME))
		return NULL;

	for (i = 0; i < (int)t_node_n_children(apps); i++) 
	{
		TNode *child = t_node_nth_child(apps, i);
		VarEntry *ve = (VarEntry *) (child->data);

		if (!strcmp(ve->name, appname))
			return ve;
	}

	return NULL;
}

/**
 * ticalcs_dirlist_num_vars:
 * @tree: a tree (var or app).
 *
 * Count how many variables are listed in the tree.
 *
 * Return value: the number of variables.
 **/
TIEXPORT int TICALL ticalcs_dirlist_num_vars(TNode* tree)
{
	int i, j;
	TNode *vars = tree;
	int nvars = 0;
	TreeInfo *info = (TreeInfo *)(tree->data);
	char *node_name = info->type;

	if (tree == NULL)
		return 0;

	if (strcmp(node_name, VAR_NODE_NAME))
		return 0;

	for (i = 0; i < (int)t_node_n_children(vars); i++)	// parse folders
	{
		TNode *parent = t_node_nth_child(vars, i);

		for (j = 0; j < (int)t_node_n_children(parent); j++)	//parse variables
			nvars++;
	}

	return nvars;
}

/**
 * ticalcs_dirlist_mem_used:
 * @tree: a tree (var only).
 *
 * Count how much memory is used by variables listed in the tree.
 *
 * Return value: size of all variables in bytes.
 **/
TIEXPORT int TICALL ticalcs_dirlist_mem_used(TNode* tree)
{
	int i, j;
	TNode *vars = tree;
	uint32_t mem = 0;

	if (tree == NULL)
		return 0;

	if (strcmp(((TreeInfo *)(vars->data))->type, VAR_NODE_NAME))
		return 0;

	for (i = 0; i < (int)t_node_n_children(vars); i++)	// parse folders
	{
	    TNode *parent = t_node_nth_child(vars, i);

		for (j = 0; j < (int)t_node_n_children(parent); j++)	//parse variables
		{
			TNode *child = t_node_nth_child(parent, j);
			VarEntry *ve = (VarEntry *) (child->data);

			mem += ve->size;
		}
	}

	return mem;
}

// Reminder of new format...
/*
int tixx_directorylist2(TNode** vars, TNode** apps, uint32_t * memory)
{
  TNode *tree;
  TNode *var_node, *app_node;
  int err;

  // Get old directory list
  err = tcf->directorylist(&tree, memory);
  if (err) {
    *vars = *apps = NULL;
    return err;
  }

  // Get Vars tree
  var_node = t_node_nth_child(tree, 0);
  var_node->data = strdup(VAR_NODE_NAME); // so that it can be freed !

  // Get Apps tree
  app_node = t_node_nth_child(tree, 1);
  app_node->data = strdup(APP_NODE_NAME);

  // Split trees
  t_node_unlink(var_node);
  t_node_unlink(app_node);
  t_node_destroy(tree);

  // Returns new trees
  *vars = var_node;
  *apps = app_node;

  return 0;
}
*/


/* Dirlist format */
/*

  top = NULL (data = TreeInfo)
  |
  + folder (= NULL if TI8x, data = VarEntry if TI9x)
	  |
	  +- var1 (data = VarEntry)
	  +- var2 (data = VarEntry)

*/
