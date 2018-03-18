/*
 * Copyright 2018, Jaroslaw Pelczar <jarek@jpelczar.com>
 * Distributed under the terms of the MIT License.
 */

#include <boot/menu.h>
#include <boot/platform/generic/text_menu.h>
#include <safemode.h>


void
platform_add_menus(Menu *menu)
{

}


void
platform_update_menu_item(Menu *menu, MenuItem *item)
{
	platform_generic_update_text_menu_item(menu, item);
}


void
platform_run_menu(Menu *menu)
{
	platform_generic_run_text_menu(menu);
}


void
platform_get_user_input_text(Menu *menu, MenuItem *item, char *buffer,
	size_t bufferSize)
{
	platform_generic_get_user_input_text(menu, item, buffer, bufferSize);
}
