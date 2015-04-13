/* Hey EMACS -*- linux-c -*- */
/* $Id$ */

/*  libticalcs2 - hand-helds support library, a part of the TiLP project
 *  Copyright (c) 1999-2005  Romain Lievin
 *  Copyright (c) 2005  Benjamin Moody (ROM dumper)
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
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
	TI84+ support thru DirectUsb link.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ticonv.h>
#include "ticalcs.h"
#include "internal.h"
#include "gettext.h"
#include "logging.h"
#include "error.h"
#include "pause.h"
#include "macros.h"

#include "dusb_vpkt.h"
#include "dusb_cmd.h"
#include "rom84p.h"
#include "rom84pcu.h"
#include "romdump.h"
#include "keys83p.h"

// Screen coordinates of the TI83+
#define TI84P_ROWS  64
#define TI84P_COLS  96
#define TI84PC_ROWS 240
#define TI84PC_COLS 320

static int		is_ready	(CalcHandle* handle)
{
	DUSBModeSet mode = MODE_NORMAL;
	int ret;

	ret = dusb_cmd_s_mode_set(handle, mode);
	if (!ret)
	{
		// use PID_HOMESCREEN to return status ?
		ret = dusb_cmd_r_mode_ack(handle);
	}

	return ret;
}

static int		send_key	(CalcHandle* handle, uint16_t key)
{
	int ret;

	ret = dusb_cmd_s_execute(handle, "", "", EID_KEY, "", key);
	if (!ret)
	{
		ret = dusb_cmd_r_delay_ack(handle);
		if (!ret)
		{
			ret = dusb_cmd_r_data_ack(handle);
		}
	}

	return ret;
}

static int		execute		(CalcHandle* handle, VarEntry *ve, const char *args)
{
	uint8_t action;
	int ret;

	switch (ve->type)
	{
		case TI84p_ASM:  action = EID_ASM; break;
		case TI84p_APPL: action = EID_APP; break;
		default:         action = EID_PRGM; break;
	}

	ret = dusb_cmd_s_execute(handle, ve->folder, ve->name, action, args, 0);
	if (!ret)
	{
		ret = dusb_cmd_r_data_ack(handle);
	}

	return ret;
}

/* Unpack an image compressed using the 84+CSE RLE compression algorithm */
int ti84pcse_decompress_screen(uint8_t *dest, uint32_t dest_length, const uint8_t *src, uint32_t src_length)
{
	const uint8_t *palette;
	unsigned int palette_size, i, c, n;

	if (src[0] != 1)
	{
		return ERR_INVALID_SCREENSHOT;
	}
	src++;
	src_length--;

	palette_size = src[src_length - 1];
	if (src_length <= palette_size * 2 + 1)
	{
		return ERR_INVALID_SCREENSHOT;
	}

	src_length -= palette_size * 2 + 1;
	palette = src + src_length - 2;

	while (src_length > 0)
	{
		if ((src[0] & 0xf0) != 0)
		{
			for (i = 0; i < 2; i ++)
			{
				c = (i == 0 ? src[0] >> 4 : src[0] & 0x0f);
				if (c == 0)
				{
					break;
				}

				if (c > palette_size)
				{
					return ERR_INVALID_SCREENSHOT;
				}

				if (dest_length < 2)
				{
					return ERR_INVALID_SCREENSHOT;
				}

				dest[0] = palette[2 * c];
				dest[1] = palette[2 * c + 1];
				dest += 2;
				dest_length -= 2;
			}
			src++;
			src_length--;
		}
		else if (src_length >= 2 && (src[0] & 0x0f) != 0)
		{
			c = src[0];
			n = src[1];

			if (c > palette_size)
			{
				return ERR_INVALID_SCREENSHOT;
			}

			if (dest_length < 2 * n)
			{
				return ERR_INVALID_SCREENSHOT;
			}

			for (i = 0; i < n; i++)
			{
				dest[0] = palette[2 * c];
				dest[1] = palette[2 * c + 1];
				dest += 2;
				dest_length -= 2;
			}

			src += 2;
			src_length -= 2;
		}
		else if (src_length >= 2 && src[0] == 0 && src[1] == 0)
		{
			src += 2;
			src_length -= 2;
			goto byte_mode;
		}
		else
		{
			return ERR_INVALID_SCREENSHOT;
		}
	}
	goto finish;

byte_mode:
	while (src_length > 0)
	{
		if (src[0] != 0)
		{
			c = src[0];

			if (c > palette_size)
			{
				return ERR_INVALID_SCREENSHOT;
			}

			if (dest_length < 2)
			{
				return ERR_INVALID_SCREENSHOT;
			}

			dest[0] = palette[2 * c];
			dest[1] = palette[2 * c + 1];
			dest += 2;
			dest_length -= 2;

			src++;
			src_length--;
		}
		else if (src_length >= 3 && src[1] != 0)
		{
			c = src[1];
			n = src[2];

			if (c > palette_size)
			{
				return ERR_INVALID_SCREENSHOT;
			}

			if (dest_length < 2 * n)
			{
				return ERR_INVALID_SCREENSHOT;
			}

			for (i = 0; i < n; i++)
			{
				dest[0] = palette[2 * c];
				dest[1] = palette[2 * c + 1];
				dest += 2;
				dest_length -= 2;
			}

			src += 3;
			src_length -= 3;
		}
		else if (src_length >= 3 && src[0] == 0 && src[1] == 0 && src[2] == 0)
		{
			src += 3;
			src_length -= 3;
			goto word_mode;
		}
		else
		{
			return ERR_INVALID_SCREENSHOT;
		}
	}
	goto finish;

word_mode:
	while (src_length > 0)
	{
		if (src_length < 2)
		{
			return ERR_INVALID_SCREENSHOT;
		}

		if (src[0] != 0x01 || src[1] != 0x00)
		{
			if (dest_length < 2)
			{
				return ERR_INVALID_SCREENSHOT;
			}

			dest[0] = src[0];
			dest[1] = src[1];
			dest += 2;
			dest_length -= 2;
			src += 2;
			src_length -= 2;
		}
		else
		{
			if (src_length < 5)
			{
				return ERR_INVALID_SCREENSHOT;
			}

			n = src[4];

			if (dest_length < 2 * n)
			{
				return ERR_INVALID_SCREENSHOT;
			}

			for (i = 0; i < n; i++)
			{
				dest[0] = src[2];
				dest[1] = src[3];
				dest += 2;
				dest_length -= 2;
			}

			src += 5;
			src_length -= 5;
		}
	}

finish:
	if (src_length != 0 || dest_length != 0)
	{
		return ERR_INVALID_SCREENSHOT;
	}
	else
	{
		return 0;
	}
}

static int		recv_screen	(CalcHandle* handle, CalcScreenCoord* sc, uint8_t** bitmap)
{
	static const uint16_t pid[] = { PID_SCREENSHOT };
	uint32_t size;
	uint8_t *data;
	int ret;

	ret = dusb_cmd_s_param_request(handle, 1, pid);
	if (!ret)
	{
		ret = dusb_cmd_r_screenshot(handle, &size, &data);
		if (!ret)
		{
			switch (handle->model)
			{
			case CALC_TI84P_USB:
			case CALC_TI82A_USB:
			{
				if (size == TI84P_ROWS * TI84P_COLS / 8)
				{
					*bitmap = data;
					sc->width = TI84P_COLS;
					sc->height = TI84P_ROWS;
					sc->clipped_width = TI84P_COLS;
					sc->clipped_height = TI84P_ROWS;
					sc->pixel_format = CALC_PIXFMT_MONO;
				}
				else
				{
					g_free(data);
					*bitmap = NULL;
					ret = ERR_INVALID_SCREENSHOT;
				}
				break;
			}
			case CALC_TI84PC_USB:
			{
				size -= 4;
				*bitmap = g_malloc(TI84PC_ROWS * TI84PC_COLS * 2);
				ret = ti84pcse_decompress_screen(*bitmap, TI84PC_ROWS * TI84PC_COLS * 2, data, size);
				g_free(data);
				if (ret)
				{
					g_free(*bitmap);
					*bitmap = NULL;
					ret = ERR_INVALID_SCREENSHOT;
				}
				else
				{
					sc->width = TI84PC_COLS;
					sc->height = TI84PC_ROWS;
					sc->clipped_width = TI84PC_COLS;
					sc->clipped_height = TI84PC_ROWS;
					sc->pixel_format = CALC_PIXFMT_RGB_5_6_5;
				}
				break;
			}
			case CALC_TI83PCE_USB:
			case CALC_TI84PCE_USB:
			{
				if (size == TI84PC_ROWS * TI84PC_COLS * 2)
				{
					// 83PCE, 84PCE
					*bitmap = data;
					sc->width = TI84PC_COLS;
					sc->height = TI84PC_ROWS;
					sc->clipped_width = TI84PC_COLS;
					sc->clipped_height = TI84PC_ROWS;
					sc->pixel_format = CALC_PIXFMT_RGB_5_6_5;
					break;
				}
				// else fall through.
			}
			default:
			{
				g_free(data);
				*bitmap = NULL;
				ret = ERR_INVALID_SCREENSHOT;
				break;
			}
			}
		}
	}

	return ret;
}

static int		get_dirlist	(CalcHandle* handle, GNode** vars, GNode** apps)
{
	static const uint16_t aids[] = { AID_VAR_SIZE, AID_VAR_TYPE, AID_ARCHIVED };
	const int size = sizeof(aids) / sizeof(uint16_t);
	TreeInfo *ti;
	int ret;
	DUSBCalcAttr **attr;
	GNode *folder, *root;
	char fldname[40], varname[40];
	char *utf8;

	(*apps) = g_node_new(NULL);
	ti = (TreeInfo *)g_malloc(sizeof(TreeInfo));
	ti->model = handle->model;
	ti->type = APP_NODE_NAME;
	(*apps)->data = ti;

	(*vars) = g_node_new(NULL);
	ti = (TreeInfo *)g_malloc(sizeof(TreeInfo));
	ti->model = handle->model;
	ti->type = VAR_NODE_NAME;
	(*vars)->data = ti;

	folder = g_node_new(NULL);
	g_node_append(*vars, folder);

	root = g_node_new(NULL);
	g_node_append(*apps, root);

	// Add permanent variables (Window, RclWindow, TblSet aka WINDW, ZSTO, TABLE)
	{
		GNode *node;
		VarEntry *ve;

		ve = tifiles_ve_create();
		strncpy(ve->name, "Window", sizeof(ve->name) - 1);
		ve->name[sizeof(ve->name) - 1] = 0;
		ve->type = TI84p_WINDW;
		node = g_node_new(ve);
		g_node_append(folder, node);

		ve = tifiles_ve_create();
		strncpy(ve->name, "RclWin", sizeof(ve->name) - 1);
		ve->name[sizeof(ve->name) - 1] = 0;
		ve->type = TI84p_ZSTO;
		node = g_node_new(ve);
		g_node_append(folder, node);

		ve = tifiles_ve_create();
		strncpy(ve->name, "TblSet", sizeof(ve->name) - 1);
		ve->name[sizeof(ve->name) - 1] = 0;
		ve->type = TI84p_TABLE;
		node = g_node_new(ve);
		g_node_append(folder, node);
	}

	ret = dusb_cmd_s_dirlist_request(handle, size, aids);
	if (!ret)
	{
		for (;;)
		{
			VarEntry *ve = tifiles_ve_create();
			GNode *node;

			attr = dusb_ca_new_array(size);
			ret = dusb_cmd_r_var_header(handle, fldname, varname, attr);
			if (ret)
			{
				// Not a real error.
				if (ret == ERR_EOT)
				{
					ret = 0;
				}
				dusb_ca_del_array(size, attr);
				break;
			}

			strncpy(ve->name, varname, sizeof(ve->name) - 1);
			ve->name[sizeof(ve->name) - 1] = 0;
			ve->size = (  (((uint32_t)(attr[0]->data[0])) << 24)
			            | (((uint32_t)(attr[0]->data[1])) << 16)
			            | (((uint32_t)(attr[0]->data[2])) <<  8)
			            | (((uint32_t)(attr[0]->data[3]))      ));
			ve->type = (uint32_t)(attr[1]->data[3]);
			ve->attr = attr[2]->data[0] ? ATTRB_ARCHIVED : ATTRB_NONE;
			dusb_ca_del_array(size, attr);

			node = g_node_new(ve);
			if (ve->type != TI73_APPL)
			{
				g_node_append(folder, node);
			}
			else
			{
				g_node_append(root, node);
			}

			utf8 = ticonv_varname_to_utf8(handle->model, ve->name, ve->type);
			g_snprintf(update_->text, sizeof(update_->text), _("Parsing %s"), utf8);
			g_free(utf8);
			update_label();
		}
	}

	return ret;
}

static int		get_memfree	(CalcHandle* handle, uint32_t* ram, uint32_t* flash)
{
	static const uint16_t pids[] = { PID_FREE_RAM, PID_FREE_FLASH };
	const int size = sizeof(pids) / sizeof(uint16_t);
	DUSBCalcParam **params;
	int ret;

	params = dusb_cp_new_array(size);
	ret = dusb_cmd_s_param_request(handle, size, pids);
	if (!ret)
	{
		ret = dusb_cmd_r_param_data(handle, size, params);
		if (!ret)
		{
			*ram = (  (((uint32_t)(params[0]->data[4])) << 24)
			        | (((uint32_t)(params[0]->data[5])) << 16)
			        | (((uint32_t)(params[0]->data[6])) <<  8)
			        | (((uint32_t)(params[0]->data[7]))      ));
			*flash = (  (((uint32_t)(params[1]->data[4])) << 24)
			         | (((uint32_t)(params[1]->data[5])) << 16)
			         | (((uint32_t)(params[1]->data[6])) <<  8)
			         | (((uint32_t)(params[1]->data[7]))      ));
		}
	}
	dusb_cp_del_array(size, params);

	return ret;
}

static int		send_var	(CalcHandle* handle, CalcMode mode, FileContent* content)
{
	int i;
	char *utf8;
	DUSBCalcAttr **attrs;
	const int nattrs = 3;
	int ret = 0;

	for (i = 0; i < content->num_entries; i++) 
	{
		VarEntry *ve = content->entries[i];

		if (ve->action == ACT_SKIP)
		{
			continue;
		}

		utf8 = ticonv_varname_to_utf8(handle->model, ve->name, ve->type);
		g_snprintf(update_->text, sizeof(update_->text), "%s", utf8);
		g_free(utf8);
		update_label();

		attrs = dusb_ca_new_array(nattrs);
		attrs[0] = dusb_ca_new(AID_VAR_TYPE, 4);
		attrs[0]->data[0] = 0xF0; attrs[0]->data[1] = 0x07;
		attrs[0]->data[2] = 0x00; attrs[0]->data[3] = ve->type;
		attrs[1] = dusb_ca_new(AID_ARCHIVED, 1);
		attrs[1]->data[0] = ve->attr == ATTRB_ARCHIVED ? 1 : 0;
		attrs[2] = dusb_ca_new(AID_VAR_VERSION, 4);

		/* Kludge to support 84+CSE Pic files.  Please do not rely on
		   this behavior; it will go away in the future. */
		if (ve->type == 0x07 && ve->size == 0x55bb)
		{
			attrs[2]->data[3] = 0x0a;
		}

		ret = dusb_cmd_s_rts(handle, "", ve->name, ve->size, nattrs, CA(attrs));
		dusb_ca_del_array(nattrs, attrs);
		if (ret)
		{
			break;
		}
		ret = dusb_cmd_r_data_ack(handle);
		if (ret)
		{
			break;
		}
		ret = dusb_cmd_s_var_content(handle, ve->size, ve->data);
		if (ret)
		{
			break;
		}
		ret = dusb_cmd_r_data_ack(handle);
		if (ret)
		{
			break;
		}
		ret = dusb_cmd_s_eot(handle);
		if (ret)
		{
			break;
		}
		PAUSE(50);	// needed
	}

	return ret;
}

static int		recv_var	(CalcHandle* handle, CalcMode mode, FileContent* content, VarRequest* vr)
{
	static const uint16_t aids[] = { AID_ARCHIVED, AID_VAR_VERSION, AID_VAR_SIZE };
	const int naids = sizeof(aids) / sizeof(uint16_t);
	DUSBCalcAttr **attrs;
	const int nattrs = 1;
	char fldname[40], varname[40];
	uint8_t *data;
	VarEntry *ve;
	char *utf8;
	int ret;

	utf8 = ticonv_varname_to_utf8(handle->model, vr->name, vr->type);
	g_snprintf(update_->text, sizeof(update_->text), "%s", utf8);
	g_free(utf8);
	update_label();

	attrs = dusb_ca_new_array(nattrs);
	attrs[0] = dusb_ca_new(AID_VAR_TYPE2, 4);
	attrs[0]->data[0] = 0xF0; attrs[0]->data[1] = 0x07;
	attrs[0]->data[2] = 0x00; attrs[0]->data[3] = vr->type;

	ret = dusb_cmd_s_var_request(handle, "", vr->name, naids, aids, nattrs, CA(attrs));
	dusb_ca_del_array(nattrs, attrs);
	if (!ret)
	{
		attrs = dusb_ca_new_array(naids);
		ret = dusb_cmd_r_var_header(handle, fldname, varname, attrs);
		if (!ret)
		{
			ret = dusb_cmd_r_var_content(handle, NULL, &data);
			if (!ret)
			{
				content->model = handle->model;
				strncpy(content->comment, tifiles_comment_set_single(), sizeof(content->comment) - 1);
				content->comment[sizeof(content->comment) - 1] = 0;
				content->num_entries = 1;

				content->entries = tifiles_ve_create_array(1);
				ve = content->entries[0] = tifiles_ve_create();
				memcpy(ve, vr, sizeof(VarEntry));

				ve->size = (  (((uint32_t)(attrs[2]->data[0])) << 24)
					    | (((uint32_t)(attrs[2]->data[1])) << 16)
					    | (((uint32_t)(attrs[2]->data[2])) <<  8)
					    | (((uint32_t)(attrs[2]->data[3]))      ));

				ve->data = tifiles_ve_alloc_data(ve->size);
				memcpy(ve->data, data, ve->size);

				g_free(data);
			}
		}
		dusb_ca_del_array(naids, attrs);
	}

	return ret;
}

static int		send_backup	(CalcHandle* handle, BackupContent* content)
{
	return send_var(handle, MODE_BACKUP, (FileContent *)content);
}

static int		send_var_ns	(CalcHandle* handle, CalcMode mode, FileContent* content)
{
	return 0;
}

static int		recv_var_ns	(CalcHandle* handle, CalcMode mode, FileContent* content, VarEntry** ve)
{
	return 0;
}

static int		send_flash	(CalcHandle* handle, FlashContent* content)
{
	FlashContent *ptr;
	int i;
	char *utf8;
	DUSBCalcAttr **attrs;
	const int nattrs = 2;
	int ret = 0;

	uint8_t *data;
	uint32_t size;

	// search for data header
	for (ptr = content; ptr != NULL; ptr = ptr->next)
	{
		if (ptr->data_type == TI83p_AMS || ptr->data_type == TI83p_APPL)
		{
			break;
		}
	}
	if (ptr == NULL)
	{
		return -1;
	}
	if (ptr->data_type != TI83p_APPL)
	{
		return -1;
	}

#if 0
	printf("#pages: %i\n", ptr->num_pages);
	printf("type: %02x\n", ptr->data_type);
	for (i = 0; i < ptr->num_pages; i++) 
	{
		FlashPage *fp = ptr->pages[i];

		printf("page #%i: %04x %02x %02x %04x\n", i,
			fp->addr, fp->page, fp->flag, fp->size);
	}
	printf("data length: %08x\n", ptr->data_length);
#endif

	size = ptr->num_pages * FLASH_PAGE_SIZE;
	data = tifiles_fp_alloc_data(size);	// must be rounded-up

	update_->cnt2 = 0;
	update_->max2 = ptr->num_pages;

	for (i = 0; i < ptr->num_pages; i++) 
	{
		FlashPage *fp = ptr->pages[i];
		memcpy(data + i*FLASH_PAGE_SIZE, fp->data, FLASH_PAGE_SIZE);

		update_->cnt2 = i;
		update_->pbar();
	}
	{
		FlashPage *fp = ptr->pages[--i];
		memset(data + i*FLASH_PAGE_SIZE + fp->size, 0x00, FLASH_PAGE_SIZE - fp->size); 

		update_->cnt2 = i;
		update_->pbar();
	}

	// send
	utf8 = ticonv_varname_to_utf8(handle->model, ptr->name, ptr->data_type);
	g_snprintf(update_->text, sizeof(update_->text), "%s", utf8);
	g_free(utf8);
	update_label();

	attrs = dusb_ca_new_array(nattrs);
	attrs[0] = dusb_ca_new(AID_VAR_TYPE, 4);
	attrs[0]->data[0] = 0xF0; attrs[0]->data[1] = 0x07;
	attrs[0]->data[2] = 0x00; attrs[0]->data[3] = ptr->data_type;
	attrs[1] = dusb_ca_new(AID_ARCHIVED, 1);
	attrs[1]->data[0] = 0;

	ret = dusb_cmd_s_rts(handle, "", ptr->name, size, nattrs, CA(attrs));
	dusb_ca_del_array(nattrs, attrs);
	if (!ret)
	{
		ret = dusb_cmd_r_data_ack(handle);
		if (!ret)
		{
			ret = dusb_cmd_s_var_content(handle, size, data);
			if (!ret)
			{
				ret = dusb_cmd_r_data_ack(handle);
				if (!ret)
				{
					ret = dusb_cmd_s_eot(handle);
				}
			}
		}
	}

	return ret;
}

static int		recv_flash	(CalcHandle* handle, FlashContent* content, VarRequest* vr)
{
	static const uint16_t aids[] = { AID_ARCHIVED, AID_VAR_VERSION };
	const int naids = sizeof(aids) / sizeof(uint16_t);
	DUSBCalcAttr **attrs;
	const int nattrs = 1;
	char fldname[40], varname[40];
	uint8_t *data;
	char *utf8;
	int page;
	uint16_t data_addr = 0x4000;
	uint16_t data_page = 0;
	int r, q;
	int ret;

	utf8 = ticonv_varname_to_utf8(handle->model, vr->name, vr->type);
	g_snprintf(update_->text, sizeof(update_->text), "%s", utf8);
	g_free(utf8);
	update_label();

	attrs = dusb_ca_new_array(nattrs);
	attrs[0] = dusb_ca_new(AID_VAR_TYPE2, 4);
	attrs[0]->data[0] = 0xF0; attrs[0]->data[1] = 0x07;
	attrs[0]->data[2] = 0x00; attrs[0]->data[3] = vr->type;

	ret = dusb_cmd_s_var_request(handle, "", vr->name, naids, aids, nattrs, CA(attrs));
	dusb_ca_del_array(nattrs, attrs);
	if (!ret)
	{
		attrs = dusb_ca_new_array(naids);
		ret = dusb_cmd_r_var_header(handle, fldname, varname, attrs);
		if (!ret)
		{
			ret = dusb_cmd_r_var_content(handle, NULL, &data);
			if (!ret)
			{

				content->model = handle->model;
				strncpy(content->name, vr->name, sizeof(content->name) - 1);
				content->name[sizeof(content->name) - 1] = 0;
				content->data_type = vr->type;
				content->device_type = DEVICE_TYPE_83P;
				content->num_pages = 2048;	// TI83+ has 512 KB of FLASH max
				content->pages = tifiles_fp_create_array(content->num_pages);

				q = vr->size / FLASH_PAGE_SIZE;
				r = vr->size % FLASH_PAGE_SIZE;

				update_->cnt2 = 0;
				update_->max2 = q;

				for(page = 0; page < q; page++)
				{
					FlashPage *fp = content->pages[page] = tifiles_fp_create();

					fp->addr = data_addr;
					fp->page = data_page++;
					fp->flag = 0x80;
					fp->size = FLASH_PAGE_SIZE;
					fp->data = tifiles_fp_alloc_data(FLASH_PAGE_SIZE);
					memcpy(fp->data, data + FLASH_PAGE_SIZE*page, FLASH_PAGE_SIZE);

					update_->cnt2 = page;
					update_->pbar();
				}
				{
					FlashPage *fp = content->pages[page] = tifiles_fp_create();

					fp->addr = data_addr;
					fp->page = data_page++;
					fp->flag = 0x80;
					fp->size = r;
					fp->data = tifiles_fp_alloc_data(FLASH_PAGE_SIZE);
					memcpy(fp->data, data + FLASH_PAGE_SIZE*page, r);

					update_->cnt2 = page;
					update_->pbar();
				}
				content->num_pages = page+1;

				g_free(data);
			}
		}
		dusb_ca_del_array(naids, attrs);
	}

	return ret;
}

static int		send_os    (CalcHandle* handle, FlashContent* content)
{
	DUSBModeSet mode = { 2, 1, 0, 0, 0x0fa0 }; //MODE_BASIC;
	uint32_t pkt_size = 266;
	uint32_t os_size = 0;
	FlashContent *ptr;
	int i, j;
	int boot = 0;
	int ret;

	// search for data header
	for (ptr = content; ptr != NULL; ptr = ptr->next)
	{
		if (ptr->data_type == TI83p_AMS || ptr->data_type == TI83p_APPL)
		{
			break;
		}
	}
	if (ptr == NULL)
	{
		return -1;
	}
	if (ptr->data_type != TI83p_AMS)
	{
		return -1;
	}

#if 0
	printf("#pages: %i\n", ptr->num_pages);
	printf("type: %02x\n", ptr->data_type);
	for (i = 0; i < ptr->num_pages; i++) 
	{
		FlashPage *fp = ptr->pages[i];

		printf("page #%i: %04x %02x %02x %04x\n", i,
			fp->addr, fp->page, fp->flag, fp->size);
		//tifiles_hexdump(fp->data, 16);
	}
	printf("data length = %08x %i\n", ptr->data_length, ptr->data_length);
#endif

	for (i = 0; i < ptr->num_pages; i++)
	{
		FlashPage *fp = ptr->pages[i];

		if (fp->size < 256)
		{
			os_size += 4;
		}
		else
		{
			os_size += 4*(fp->size / 260);
		}
	}
	printf("os_size overhead = %i\n", os_size);
	os_size += ptr->data_length;
	printf("os_size new = %i\n", os_size);

	do
	{
		static const uint16_t pids[] = { PID_OS_MODE };
		const int size = sizeof(pids) / sizeof(uint16_t);
		DUSBCalcParam **params;

		// switch to BASIC mode
		ret = dusb_cmd_s_mode_set(handle, mode);
		if (ret)
		{
			break;
		}
		ret = dusb_cmd_r_mode_ack(handle);
		if (ret)
		{
			break;
		}

		// test for boot mode
		ret = dusb_cmd_s_param_request(handle, size, pids);
		if (ret)
		{
			break;
		}
		params = dusb_cp_new_array(size);
		ret = dusb_cmd_r_param_data(handle, size, params);
		if (ret)
		{
			dusb_cp_del_array(size, params);
			break;
		}
		boot = !params[0]->data[0];
		dusb_cp_del_array(size, params);

		// start OS transfer
		ret = dusb_cmd_s_os_begin(handle, os_size);
		if (ret)
		{
			break;
		}
		if (!boot)
		{
			ret = dusb_recv_buf_size_request(handle, &pkt_size);
			if (ret)
			{
				break;
			}
			ret = dusb_send_buf_size_alloc(handle, pkt_size);
			if (ret)
			{
				break;
			}
		}
		ret = dusb_cmd_r_os_ack(handle, &pkt_size);	// this pkt_size is important
		if (ret)
		{
			break;
		}

		// send OS header/signature
		ret = dusb_cmd_s_os_header(handle, 0x4000, 0x7A, 0x80, pkt_size-4, ptr->pages[0]->data);
		if (ret)
		{
			break;
		}
		ret = dusb_cmd_r_os_ack(handle, &pkt_size);
		if (ret)
		{
			break;
		}

		// send OS data
		update_->cnt2 = 0;
		update_->max2 = ptr->num_pages;

		for (i = 0; i < ptr->num_pages; i++)
		{
			FlashPage *fp = ptr->pages[i];

			fp->addr = 0x4000;

			if (i == 0)	// need relocation
			{
				ret = dusb_cmd_s_os_data(handle, 0x4000, 0x7A, 0x80, pkt_size-4, fp->data);
				if (ret)
				{
					goto end;
				}
				ret = dusb_cmd_r_data_ack(handle);
				if (ret)
				{
					goto end;
				}
			}
			else if (i == ptr->num_pages-1)	// idem
			{
				ret = dusb_cmd_s_os_data(handle, 0x4100, 0x7A, 0x80, pkt_size-4, fp->data);
				if (ret)
				{
					goto end;
				}
				ret = dusb_cmd_r_data_ack(handle);
				if (ret)
				{
					goto end;
				}
			}
			else
			{
				for (j = 0; j < fp->size; j += 256)
				{
					ret = dusb_cmd_s_os_data(handle,
						(uint16_t)(fp->addr + j), (uint8_t)fp->page, fp->flag, 
						pkt_size-4, fp->data + j);
					if (ret)
					{
						goto end;
					}
					ret = dusb_cmd_r_data_ack(handle);
					if (ret)
					{
						goto end;
					}
				}
			}

			update_->cnt2 = i;
			update_->pbar();
		}

		ret = dusb_cmd_s_eot(handle);
		if (ret)
		{
			break;
		}
		PAUSE(500);
		ret = dusb_cmd_r_eot_ack(handle);
	} while(0);
end:

	return ret;
}

static int		recv_idlist	(CalcHandle* handle, uint8_t* id)
{
	static const uint16_t aids[] = { AID_ARCHIVED, AID_VAR_VERSION };
	const int naids = sizeof(aids) / sizeof(uint16_t);
	DUSBCalcAttr **attrs;
	const int nattrs = 1;
	char folder[40], name[40];
	uint8_t *data;
	uint32_t i, varsize;
	int ret;

	g_snprintf(update_->text, sizeof(update_->text), "ID-LIST");
	update_label();

	attrs = dusb_ca_new_array(nattrs);
	attrs[0] = dusb_ca_new(AID_VAR_TYPE2, 4);
	attrs[0]->data[0] = 0xF0; attrs[0]->data[1] = 0x07;
	attrs[0]->data[2] = 0x00; attrs[0]->data[3] = TI83p_IDLIST;

	ret = dusb_cmd_s_var_request(handle, "", "IDList", naids, aids, nattrs, CA(attrs));
	dusb_ca_del_array(nattrs, attrs);
	if (!ret)
	{
		attrs = dusb_ca_new_array(naids);
		ret = dusb_cmd_r_var_header(handle, folder, name, attrs);
		if (!ret)
		{
			ret = dusb_cmd_r_var_content(handle, &varsize, &data);
			if (!ret)
			{
				i = data[9];
				data[9] = data[10];
				data[10] = i;

				for (i = 4; i < varsize && i < 16; i++)
				{
					sprintf((char *)&id[2 * (i-4)], "%02x", data[i]);
				}
				id[7*2] = '\0';

				g_free(data);
			}
		}
		dusb_ca_del_array(naids, attrs);
	}

	return ret;
}

static int		get_version	(CalcHandle* handle, CalcInfos* infos);

static int		dump_rom_1	(CalcHandle* handle)
{
	CalcInfos infos;
	int ret;

	ret = get_version(handle, &infos);
	if (!ret)
	{
		if (infos.model == CALC_TI84P_USB)
		{
			ret = rd_send(handle, "romdump.8Xp", romDumpSize84p, romDump84p);
		}
		else if (infos.model == CALC_TI84PC_USB)
		{
			ret = rd_send(handle, "romdump.8Xp", romDumpSize84pcu, romDump84pcu);
		}
		else
		{
			// TODO 83+CE/84+CE/84+CE-T ROM dumping support.
			ret = 0;
		}
	}

	return ret;
}
static int		dump_rom_2	(CalcHandle* handle, CalcDumpSize size, const char *filename)
{
	int ret;
#if 0
	// Old, less sophisticated and more complicated version.
	int i;
	static const uint16_t keys[] = { 
		0x40, 0x09, 0x09, 0xFC9C, /* Quit, Clear, Clear, Asm( */
		0xDA, 0xAB, 0xA8, 0xA6,   /* prgm, R, O, M */
		0x9D, 0xAE, 0xA6, 0xA9,   /* D, U, M, P */
		0x86 };                   /* ) */

	// Launch program by remote control
	PAUSE(200);
	for(i = 0; i < sizeof(keys) / sizeof(uint16_t); i++)
	{
		ret = send_key(handle, keys[i]);
		if (ret)
		{
			goto end;
		}
		PAUSE(100);
	}

	// This fixes a 100% reproducible timeout: send_key normally requests a data ACK,
	// but when the program is running, no data ACK is sent. Therefore, hit the Enter
	// key without requesting a data ACK, only the initial delay ACK.
	ret = dusb_cmd_s_execute(handle, "", "", EID_KEY, "", 0x05);
	if (!ret)
	{
		ret = dusb_cmd_r_delay_ack(handle);
		PAUSE(400);
		if (!ret)
		{
			// Get dump
			ret = rd_dump(handle, filename);
		}
	}
end:
#endif
	ret = dusb_cmd_s_execute(handle, "", "ROMDUMP", EID_PRGM, "", 0);
	if (!ret)
	{
		ret = dusb_cmd_r_data_ack(handle);
		if (!ret)
		{
			PAUSE(3000);

			// Get dump
			ret = rd_dump(handle, filename);
		}
	}

	return ret;
}

static int		set_clock	(CalcHandle* handle, CalcClock* _clock)
{
	DUSBCalcParam *param;
	uint32_t calc_time;
	struct tm ref, cur;
	time_t r, c, now;
	int ret;

	time(&now);
	memcpy(&ref, localtime(&now), sizeof(struct tm));

	ref.tm_year = 1997 - 1900;
	ref.tm_mon = 0;
	ref.tm_yday = 0;
	ref.tm_mday = 1;
	ref.tm_wday = 3;
	ref.tm_hour = 0;
	ref.tm_min = 0;
	ref.tm_sec = 0;
	//ref.tm_isdst = 1;
	r = mktime(&ref);

	cur.tm_year = _clock->year - 1900;
	cur.tm_mon = _clock->month - 1;
	cur.tm_mday = _clock->day;
	cur.tm_hour = _clock->hours;
	cur.tm_min = _clock->minutes;
	cur.tm_sec = _clock->seconds;
	cur.tm_isdst = 1;
	c = mktime(&cur);

	calc_time = (uint32_t)difftime(c, r);

	g_snprintf(update_->text, sizeof(update_->text), _("Setting clock..."));
	update_label();

	do {
		param = dusb_cp_new(PID_CLK_SEC, 4);
		param->data[0] = MSB(MSW(calc_time));
		param->data[1] = LSB(MSW(calc_time));
		param->data[2] = MSB(LSW(calc_time));
		param->data[3] = LSB(LSW(calc_time));
		ret = dusb_cmd_s_param_set(handle, param);
		dusb_cp_del(param);
		if (ret)
		{
			break;
		}

		ret = dusb_cmd_r_data_ack(handle);
		if (ret)
		{
			break;
		}

		param = dusb_cp_new(PID_CLK_DATE_FMT, 1);
		param->data[0] = _clock->date_format == 3 ? 0 : _clock->date_format;
		ret = dusb_cmd_s_param_set(handle, param);
		dusb_cp_del(param);
		if (ret)
		{
			break;
		}

		ret = dusb_cmd_r_data_ack(handle);
		if (ret)
		{
			break;
		}

		param = dusb_cp_new(PID_CLK_TIME_FMT, 1);
		param->data[0] = _clock->time_format == 24 ? 1 : 0;
		ret = dusb_cmd_s_param_set(handle, param);
		dusb_cp_del(param);
		if (ret)
		{
			break;
		}

		ret = dusb_cmd_r_data_ack(handle);
		if (ret)
		{
			break;
		}
		param = dusb_cp_new(PID_CLK_ON, 1);
		param->data[0] = _clock->state;
		ret = dusb_cmd_s_param_set(handle, param);
		dusb_cp_del(param);
		if (ret)
		{
			break;
		}

		ret = dusb_cmd_r_data_ack(handle);
	} while(0);

	return ret;
}

static int		get_clock	(CalcHandle* handle, CalcClock* _clock)
{
	static const uint16_t pids[4] = { PID_CLK_SEC, PID_CLK_DATE_FMT, PID_CLK_TIME_FMT, PID_CLK_ON };
	const int size = sizeof(pids) / sizeof(uint16_t);
	DUSBCalcParam **params;
	uint32_t calc_time;
	struct tm ref, *cur;
	time_t r, c, now;
	int ret;

	// get raw clock
	g_snprintf(update_->text, sizeof(update_->text), _("Getting clock..."));
	update_label();

	params = dusb_cp_new_array(size);
	ret = dusb_cmd_s_param_request(handle, size, pids);
	if (!ret)
	{
		ret = dusb_cmd_r_param_data(handle, size, params);
		if (!ret)
		{
			if (!params[0]->ok)
			{
				ret = ERR_INVALID_PACKET;
			}
			else
			{
				// and computes
				calc_time = (((uint32_t)params[0]->data[0]) << 24) | (((uint32_t)params[0]->data[1]) << 16) | (((uint32_t)params[0]->data[2]) <<  8) | (params[0]->data[3] <<  0);

				time(&now);	// retrieve current DST setting
				memcpy(&ref, localtime(&now), sizeof(struct tm));;
				ref.tm_year = 1997 - 1900;
				ref.tm_mon = 0;
				ref.tm_yday = 0;
				ref.tm_mday = 1;
				ref.tm_wday = 3;
				ref.tm_hour = 0;
				ref.tm_min = 0;
				ref.tm_sec = 0;
				//ref.tm_isdst = 1;
				r = mktime(&ref);

				c = r + calc_time;
				cur = localtime(&c);

				_clock->year = cur->tm_year + 1900;
				_clock->month = cur->tm_mon + 1;
				_clock->day = cur->tm_mday;
				_clock->hours = cur->tm_hour;
				_clock->minutes = cur->tm_min;
				_clock->seconds = cur->tm_sec;

				_clock->date_format = params[1]->data[0] == 0 ? 3 : params[1]->data[0];
				_clock->time_format = params[2]->data[0] ? 24 : 12;
				_clock->state = params[3]->data[0];
			}
		}
	}
	dusb_cp_del_array(size, params);

	return ret;
}

static int		del_var		(CalcHandle* handle, VarRequest* vr)
{
	DUSBCalcAttr **attr;
	const int size = 2;
	char *utf8;
	int ret;

	utf8 = ticonv_varname_to_utf8(handle->model, vr->name, vr->type);
	g_snprintf(update_->text, sizeof(update_->text), _("Deleting %s..."), utf8);
	g_free(utf8);
	update_label();

	attr = dusb_ca_new_array(size);
	attr[0] = dusb_ca_new(0x0011, 4);
	attr[0]->data[0] = 0xF0; attr[0]->data[1] = 0x0B;
	attr[0]->data[2] = 0x00; attr[0]->data[3] = vr->type;
	attr[1] = dusb_ca_new(0x0013, 1);
	attr[1]->data[0] = vr->attr == ATTRB_ARCHIVED ? 1 : 0;

	ret = dusb_cmd_s_var_delete(handle, "", vr->name, size, CA(attr));
	dusb_ca_del_array(size, attr);
	if (!ret)
	{
		ret = dusb_cmd_r_data_ack(handle);
	}

	return ret;
}

static int		rename_var	(CalcHandle* handle, VarRequest* oldname, VarRequest* newname)
{
	DUSBCalcAttr **attrs;
	const int size = 1;
	int ret;

	attrs = dusb_ca_new_array(size);
	attrs[0] = dusb_ca_new(AID_VAR_TYPE2, 4);
	attrs[0]->data[0] = 0xF0; attrs[0]->data[1] = 0x07;
	attrs[0]->data[2] = 0x00; attrs[0]->data[3] = oldname->type;

	ret = dusb_cmd_s_var_modify(handle, "", oldname->name, 1, CA(attrs), "", newname->name, 0, NULL);
	dusb_ca_del_array(size, attrs);
	if (!ret)
	{
		ret = dusb_cmd_r_data_ack(handle);
	}

	return ret;
}

static int		change_attr	(CalcHandle* handle, VarRequest* vr, FileAttr attr)
{
	DUSBCalcAttr **srcattrs;
	DUSBCalcAttr **dstattrs;
	int ret;

	srcattrs = dusb_ca_new_array(1);
	srcattrs[0] = dusb_ca_new(AID_VAR_TYPE2, 4);
	srcattrs[0]->data[0] = 0xF0; srcattrs[0]->data[1] = 0x07;
	srcattrs[0]->data[2] = 0x00; srcattrs[0]->data[3] = vr->type;

	dstattrs = dusb_ca_new_array(1);
	dstattrs[0] = dusb_ca_new(AID_ARCHIVED, 1);
	/* use 0xff here rather than 0x01 to work around an OS bug */
	dstattrs[0]->data[0] = (attr == ATTRB_ARCHIVED ? 0xff : 0x00);

	ret = dusb_cmd_s_var_modify(handle, "", vr->name, 1, CA(srcattrs), "", vr->name, 1, CA(dstattrs));
	dusb_ca_del_array(1, dstattrs);
	dusb_ca_del_array(1, srcattrs);
	if (!ret)
	{
		ret = dusb_cmd_r_data_ack(handle);
	}

	return ret;
}

static int		new_folder  (CalcHandle* handle, VarRequest* vr)
{
	return 0;
}

static int		get_version	(CalcHandle* handle, CalcInfos* infos)
{
	static const uint16_t pids[] = { 
		PID_PRODUCT_NAME, PID_MAIN_PART_ID,
		PID_HW_VERSION, PID_LANGUAGE_ID, PID_SUBLANG_ID, PID_DEVICE_TYPE,
		PID_BOOT_VERSION, PID_OS_VERSION, 
		PID_PHYS_RAM, PID_USER_RAM, PID_FREE_RAM,
		PID_PHYS_FLASH, PID_USER_FLASH, PID_FREE_FLASH,
		PID_LCD_WIDTH, PID_LCD_HEIGHT, PID_BATTERY, PID_OS_MODE
	};
	const int size = sizeof(pids) / sizeof(uint16_t);
	DUSBCalcParam **params;
	int i = 0;
	int ret;

	g_snprintf(update_->text, sizeof(update_->text), _("Getting version..."));
	update_label();

	memset(infos, 0, sizeof(CalcInfos));
	params = dusb_cp_new_array(size);

	// TODO rewrite this function to ask for parameters in multiple phases, starting with 0x0001 and 0x000A, then
	// model-dependent sets of parameters. That's how TI-Connect CE 5.x does.
	ret = dusb_cmd_s_param_request(handle, size, pids);
	if (!ret)
	{
		ret = dusb_cmd_r_param_data(handle, size, params);
		if (!ret)
		{
			uint8_t product_id;

			strncpy(infos->product_name, (char*)params[i]->data, params[i]->size);
			infos->mask |= INFOS_PRODUCT_NAME;
			i++;

			product_id = params[i]->data[0];
			g_snprintf(infos->main_calc_id, 11, "%02X%02X%02X%02X%02X",
				product_id, params[i]->data[1], params[i]->data[2], params[i]->data[3], params[i]->data[4]);
			infos->mask |= INFOS_MAIN_CALC_ID;
			strncpy(infos->product_id, infos->main_calc_id, sizeof(infos->product_id) - 1);
			infos->product_id[sizeof(infos->product_id) - 1] = 0;
			infos->mask |= INFOS_PRODUCT_ID;
			i++;

			infos->hw_version = (((uint16_t)params[i]->data[0]) << 8) | params[i]->data[1];
			infos->mask |= INFOS_HW_VERSION;
			i++;

			infos->language_id = params[i]->data[0];
			infos->mask |= INFOS_LANG_ID;
			i++;

			infos->sub_lang_id = params[i]->data[0];
			infos->mask |= INFOS_SUB_LANG_ID;
			i++;

			infos->device_type = params[i]->data[1];
			infos->mask |= INFOS_DEVICE_TYPE;
			i++;

			g_snprintf(infos->boot_version, 5, "%1i.%02i", params[i]->data[1], params[i]->data[2]);
			infos->mask |= INFOS_BOOT_VERSION;
			i++;

			g_snprintf(infos->os_version, 5, "%1i.%02i", params[i]->data[1], params[i]->data[2]);
			infos->mask |= INFOS_OS_VERSION;
			i++;

			infos->ram_phys = (  (((uint64_t)(params[i]->data[ 0])) << 56)
					   | (((uint64_t)(params[i]->data[ 1])) << 48)
					   | (((uint64_t)(params[i]->data[ 2])) << 40)
					   | (((uint64_t)(params[i]->data[ 3])) << 32)
					   | (((uint64_t)(params[i]->data[ 4])) << 24)
					   | (((uint64_t)(params[i]->data[ 5])) << 16)
					   | (((uint64_t)(params[i]->data[ 6])) <<  8)
					   | (((uint64_t)(params[i]->data[ 7]))      ));
			infos->mask |= INFOS_RAM_PHYS;
			i++;
			infos->ram_user = (  (((uint64_t)(params[i]->data[ 0])) << 56)
					   | (((uint64_t)(params[i]->data[ 1])) << 48)
					   | (((uint64_t)(params[i]->data[ 2])) << 40)
					   | (((uint64_t)(params[i]->data[ 3])) << 32)
					   | (((uint64_t)(params[i]->data[ 4])) << 24)
					   | (((uint64_t)(params[i]->data[ 5])) << 16)
					   | (((uint64_t)(params[i]->data[ 6])) <<  8)
					   | (((uint64_t)(params[i]->data[ 7]))      ));
			infos->mask |= INFOS_RAM_USER;
			i++;
			infos->ram_free = (  (((uint64_t)(params[i]->data[ 0])) << 56)
					   | (((uint64_t)(params[i]->data[ 1])) << 48)
					   | (((uint64_t)(params[i]->data[ 2])) << 40)
					   | (((uint64_t)(params[i]->data[ 3])) << 32)
					   | (((uint64_t)(params[i]->data[ 4])) << 24)
					   | (((uint64_t)(params[i]->data[ 5])) << 16)
					   | (((uint64_t)(params[i]->data[ 6])) <<  8)
					   | (((uint64_t)(params[i]->data[ 7]))      ));
			infos->mask |= INFOS_RAM_FREE;
			i++;

			infos->flash_phys = (  (((uint64_t)(params[i]->data[ 0])) << 56)
					     | (((uint64_t)(params[i]->data[ 1])) << 48)
					     | (((uint64_t)(params[i]->data[ 2])) << 40)
					     | (((uint64_t)(params[i]->data[ 3])) << 32)
					     | (((uint64_t)(params[i]->data[ 4])) << 24)
					     | (((uint64_t)(params[i]->data[ 5])) << 16)
					     | (((uint64_t)(params[i]->data[ 6])) <<  8)
					     | (((uint64_t)(params[i]->data[ 7]))      ));
			infos->mask |= INFOS_FLASH_PHYS;
			i++;
			infos->flash_user = (  (((uint64_t)(params[i]->data[ 0])) << 56)
					     | (((uint64_t)(params[i]->data[ 1])) << 48)
					     | (((uint64_t)(params[i]->data[ 2])) << 40)
					     | (((uint64_t)(params[i]->data[ 3])) << 32)
					     | (((uint64_t)(params[i]->data[ 4])) << 24)
					     | (((uint64_t)(params[i]->data[ 5])) << 16)
					     | (((uint64_t)(params[i]->data[ 6])) <<  8)
					     | (((uint64_t)(params[i]->data[ 7]))      ));
			infos->mask |= INFOS_FLASH_USER;
			i++;
			infos->flash_free = (  (((uint64_t)(params[i]->data[ 0])) << 56)
					     | (((uint64_t)(params[i]->data[ 1])) << 48)
					     | (((uint64_t)(params[i]->data[ 2])) << 40)
					     | (((uint64_t)(params[i]->data[ 3])) << 32)
					     | (((uint64_t)(params[i]->data[ 4])) << 24)
					     | (((uint64_t)(params[i]->data[ 5])) << 16)
					     | (((uint64_t)(params[i]->data[ 6])) <<  8)
					     | (((uint64_t)(params[i]->data[ 7]))      ));
			infos->mask |= INFOS_FLASH_FREE;
			i++;

			infos->lcd_width = (  (((uint16_t)(params[i]->data[ 0])) <<  8)
					    | (((uint16_t)(params[i]->data[ 1]))      ));
			infos->mask |= INFOS_LCD_WIDTH;
			i++;
			infos->lcd_height = (  (((uint16_t)(params[i]->data[ 0])) <<  8)
					     | (((uint16_t)(params[i]->data[ 1]))      ));
			infos->mask |= INFOS_LCD_HEIGHT;
			i++;

			infos->bits_per_pixel = 1;
			infos->mask |= INFOS_BPP;

			infos->battery = params[i]->data[0];
			infos->mask |= INFOS_BATTERY;
			i++;

			infos->run_level = params[i]->data[0];
			infos->mask |= INFOS_RUN_LEVEL;
			i++;

			switch (product_id)
			{
				case PRODUCT_ID_TI84P:
				{
					infos->model = CALC_TI84P_USB;
					if (infos->hw_version >= 4)
					{
						ticalcs_warning(_("Unhandled 84+ family member with product_id=%d hw_version=%d"), product_id, infos->hw_version);
					}
					break;
				}
				case PRODUCT_ID_TI82A:
				{
					infos->model = CALC_TI82A_USB;
					if (infos->hw_version >= 4)
					{
						ticalcs_warning(_("Unhandled 84+ family member with product_id=%d hw_version=%d"), product_id, infos->hw_version);
					}
					break;
				}
				case PRODUCT_ID_TI84PCSE:
				{
					infos->model = CALC_TI84PC_USB;
					if (infos->hw_version < 4)
					{
						ticalcs_warning(_("Unhandled 84+ family member with product_id=%d hw_version=%d"), product_id, infos->hw_version);
					}
					break;
				}
				case PRODUCT_ID_TI83PCE: // and case PRODUCT_ID_TI84PCE:
				{
					// TODO handle the field which indicates 83PCE / 84+CE.
					infos->model = CALC_TI83PCE_USB;
					if (infos->hw_version < 6)
					{
						ticalcs_warning(_("Unhandled 84+ family member with product_id=%d hw_version=%d"), product_id, infos->hw_version);
					}
					break;
				}
				default:
				{
					// Default to generic 84+(SE).
					infos->model = CALC_TI84P_USB;
					ticalcs_warning(_("Unhandled 84+ family member with product_id=%d hw_version=%d"), product_id, infos->hw_version);
					break;
				}
			}
			infos->mask |= INFOS_CALC_MODEL;
		}
	}
	dusb_cp_del_array(size, params);

	return ret;
}

static int		send_cert	(CalcHandle* handle, FlashContent* content)
{
	return 0;
}

static int		recv_cert	(CalcHandle* handle, FlashContent* content)
{
	return 0;
}

const CalcFncts calc_84p_usb =
{
	CALC_TI84P_USB,
	"TI84+",
	"TI-84 Plus",
	N_("TI-84 Plus thru DirectLink"),
	OPS_ISREADY | OPS_SCREEN | OPS_DIRLIST | OPS_VARS | OPS_FLASH | OPS_OS |
	OPS_IDLIST | OPS_ROMDUMP | OPS_CLOCK | OPS_DELVAR | OPS_VERSION | OPS_BACKUP | OPS_KEYS |
	OPS_RENAME | OPS_CHATTR |
	FTS_SILENT | FTS_MEMFREE | FTS_FLASH,
	{"",     /* is_ready */
	 "",     /* send_key */
	 "",     /* execute */
	 "1P",   /* recv_screen */
	 "1L",   /* get_dirlist */
	 "",     /* get_memfree */
	 "2P",   /* send_backup */
	 "2P",   /* recv_backup */
	 "2P1L", /* send_var */
	 "1P1L", /* recv_var */
	 "2P1L", /* send_var_ns */
	 "1P1L", /* recv_var_ns */
	 "2P1L", /* send_app */
	 "2P1L", /* recv_app */
	 "2P",   /* send_os */
	 "1L",   /* recv_idlist */
	 "2P",   /* dump_rom1 */
	 "2P",   /* dump_rom2 */
	 "",     /* set_clock */
	 "",     /* get_clock */
	 "1L",   /* del_var */
	 "1L",   /* new_folder */
	 "",     /* get_version */
	 "1L",   /* send_cert */
	 "1L",   /* recv_cert */
	 "",     /* rename */
	 ""      /* chattr */ },
	&is_ready,
	&send_key,
	&execute,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&tixx_recv_backup,
	&send_var,
	&recv_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&send_os,
	&recv_idlist,
	&dump_rom_1,
	&dump_rom_2,
	&set_clock,
	&get_clock,
	&del_var,
	&new_folder,
	&get_version,
	&send_cert,
	&recv_cert,
	&rename_var,
	&change_attr
};

const CalcFncts calc_84pcse_usb =
{
	CALC_TI84PC_USB,
	"TI84+CSE",
	"TI-84 Plus C Silver Edition",
	N_("TI-84 Plus C Silver Edition thru DirectLink"),
	OPS_ISREADY | OPS_SCREEN | OPS_DIRLIST | OPS_VARS | OPS_FLASH | OPS_OS |
	OPS_IDLIST | OPS_ROMDUMP | OPS_CLOCK | OPS_DELVAR | OPS_VERSION | OPS_BACKUP | OPS_KEYS |
	OPS_RENAME | OPS_CHATTR |
	FTS_SILENT | FTS_MEMFREE | FTS_FLASH,
	{"",     /* is_ready */
	 "",     /* send_key */
	 "",     /* execute */
	 "1P",   /* recv_screen */
	 "1L",   /* get_dirlist */
	 "",     /* get_memfree */
	 "2P",   /* send_backup */
	 "2P",   /* recv_backup */
	 "2P1L", /* send_var */
	 "1P1L", /* recv_var */
	 "2P1L", /* send_var_ns */
	 "1P1L", /* recv_var_ns */
	 "2P1L", /* send_app */
	 "2P1L", /* recv_app */
	 "2P",   /* send_os */
	 "1L",   /* recv_idlist */
	 "2P",   /* dump_rom1 */
	 "2P",   /* dump_rom2 */
	 "",     /* set_clock */
	 "",     /* get_clock */
	 "1L",   /* del_var */
	 "1L",   /* new_folder */
	 "",     /* get_version */
	 "1L",   /* send_cert */
	 "1L",   /* recv_cert */
	 "",     /* rename */
	 ""      /* chattr */ },
	&is_ready,
	&send_key,
	&execute,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&tixx_recv_backup,
	&send_var,
	&recv_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&send_os,
	&recv_idlist,
	&dump_rom_1,
	&dump_rom_2,
	&set_clock,
	&get_clock,
	&del_var,
	&new_folder,
	&get_version,
	&send_cert,
	&recv_cert,
	&rename_var,
	&change_attr
};

const CalcFncts calc_83pce_usb =
{
	CALC_TI83PCE_USB,
	"TI83PCE",
	"TI-83 Premium CE",
	N_("TI-83 Premium CE thru DirectLink"),
	OPS_ISREADY | OPS_SCREEN | OPS_DIRLIST | OPS_VARS | /*OPS_FLASH |*/ /*OPS_OS |*/
	OPS_IDLIST | /*OPS_ROMDUMP |*/ OPS_CLOCK | OPS_DELVAR | OPS_VERSION | OPS_BACKUP | /*OPS_KEYS |*/
	OPS_RENAME | OPS_CHATTR |
	FTS_SILENT | FTS_MEMFREE | FTS_FLASH,
	{"",     /* is_ready */
	 "",     /* send_key */
	 "",     /* execute */
	 "1P",   /* recv_screen */
	 "1L",   /* get_dirlist */
	 "",     /* get_memfree */
	 "2P",   /* send_backup */
	 "2P",   /* recv_backup */
	 "2P1L", /* send_var */
	 "1P1L", /* recv_var */
	 "2P1L", /* send_var_ns */
	 "1P1L", /* recv_var_ns */
	 "2P1L", /* send_app */
	 "2P1L", /* recv_app */
	 "2P",   /* send_os */
	 "1L",   /* recv_idlist */
	 "2P",   /* dump_rom1 */
	 "2P",   /* dump_rom2 */
	 "",     /* set_clock */
	 "",     /* get_clock */
	 "1L",   /* del_var */
	 "1L",   /* new_folder */
	 "",     /* get_version */
	 "1L",   /* send_cert */
	 "1L",   /* recv_cert */
	 "",     /* rename */
	 ""      /* chattr */ },
	&is_ready,
	&send_key,
	&execute,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&tixx_recv_backup,
	&send_var,
	&recv_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&send_os,
	&recv_idlist,
	&dump_rom_1,
	&dump_rom_2,
	&set_clock,
	&get_clock,
	&del_var,
	&new_folder,
	&get_version,
	&send_cert,
	&recv_cert,
	&rename_var,
	&change_attr
};

const CalcFncts calc_84pce_usb =
{
	CALC_TI84PCE_USB,
	"TI84+CE",
	"TI-84 Plus CE",
	N_("TI-84 Plus CE thru DirectLink"),
	OPS_ISREADY | OPS_SCREEN | OPS_DIRLIST | OPS_VARS | /*OPS_FLASH |*/ /*OPS_OS |*/
	OPS_IDLIST | /*OPS_ROMDUMP |*/ OPS_CLOCK | OPS_DELVAR | OPS_VERSION | OPS_BACKUP | /*OPS_KEYS |*/
	OPS_RENAME | OPS_CHATTR |
	FTS_SILENT | FTS_MEMFREE | FTS_FLASH,
	{"",     /* is_ready */
	 "",     /* send_key */
	 "",     /* execute */
	 "1P",   /* recv_screen */
	 "1L",   /* get_dirlist */
	 "",     /* get_memfree */
	 "2P",   /* send_backup */
	 "2P",   /* recv_backup */
	 "2P1L", /* send_var */
	 "1P1L", /* recv_var */
	 "2P1L", /* send_var_ns */
	 "1P1L", /* recv_var_ns */
	 "2P1L", /* send_app */
	 "2P1L", /* recv_app */
	 "2P",   /* send_os */
	 "1L",   /* recv_idlist */
	 "2P",   /* dump_rom1 */
	 "2P",   /* dump_rom2 */
	 "",     /* set_clock */
	 "",     /* get_clock */
	 "1L",   /* del_var */
	 "1L",   /* new_folder */
	 "",     /* get_version */
	 "1L",   /* send_cert */
	 "1L",   /* recv_cert */
	 "",     /* rename */
	 ""      /* chattr */ },
	&is_ready,
	&send_key,
	&execute,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&tixx_recv_backup,
	&send_var,
	&recv_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&send_os,
	&recv_idlist,
	&dump_rom_1,
	&dump_rom_2,
	&set_clock,
	&get_clock,
	&del_var,
	&new_folder,
	&get_version,
	&send_cert,
	&recv_cert,
	&rename_var,
	&change_attr
};

const CalcFncts calc_82a_usb =
{
	CALC_TI82A_USB,
	"TI82A",
	"TI-82 Advanced",
	N_("TI-82 Advanced thru DirectLink"),
	OPS_ISREADY | OPS_SCREEN | OPS_DIRLIST | OPS_VARS | /*OPS_FLASH |*/ OPS_OS |
	OPS_IDLIST | /*OPS_ROMDUMP |*/ OPS_CLOCK | OPS_DELVAR | OPS_VERSION | OPS_BACKUP | OPS_KEYS |
	OPS_RENAME | OPS_CHATTR |
	FTS_SILENT | FTS_MEMFREE | FTS_FLASH,
	{"",     /* is_ready */
	 "",     /* send_key */
	 "",     /* execute */
	 "1P",   /* recv_screen */
	 "1L",   /* get_dirlist */
	 "",     /* get_memfree */
	 "2P",   /* send_backup */
	 "2P",   /* recv_backup */
	 "2P1L", /* send_var */
	 "1P1L", /* recv_var */
	 "2P1L", /* send_var_ns */
	 "1P1L", /* recv_var_ns */
	 "2P1L", /* send_app */
	 "2P1L", /* recv_app */
	 "2P",   /* send_os */
	 "1L",   /* recv_idlist */
	 "2P",   /* dump_rom1 */
	 "2P",   /* dump_rom2 */
	 "",     /* set_clock */
	 "",     /* get_clock */
	 "1L",   /* del_var */
	 "1L",   /* new_folder */
	 "",     /* get_version */
	 "1L",   /* send_cert */
	 "1L",   /* recv_cert */
	 "",     /* rename */
	 ""      /* chattr */ },
	&is_ready,
	&send_key,
	&execute,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&tixx_recv_backup,
	&send_var,
	&recv_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&send_os,
	&recv_idlist,
	&dump_rom_1,
	&dump_rom_2,
	&set_clock,
	&get_clock,
	&del_var,
	&new_folder,
	&get_version,
	&send_cert,
	&recv_cert,
	&rename_var,
	&change_attr
};
