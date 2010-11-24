/*
    This file is part of darktable,
    copyright (c) 2010 tobias ellinghaus.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "control/control.h"
#include "control/conf.h"
#include "libs/lib.h"
#include "gui/gtk.h"

DT_MODULE(1)

typedef struct dt_lib_metadata_t
{
	int imgsel;
	GtkComboBoxEntry * title;
	GtkComboBoxEntry * description;
	GtkComboBoxEntry * creator;
	GtkComboBoxEntry * publisher;
	GtkComboBoxEntry * license;
	gboolean           multi_title;
	gboolean           multi_description;
	gboolean           multi_creator;
	gboolean           multi_publisher;
	gboolean           multi_license;
}
dt_lib_metadata_t;

const char* name(){
	return _("metadata");
}

uint32_t views(){
	return DT_LIGHTTABLE_VIEW|DT_CAPTURE_VIEW;
}

static void fill_combo_box_entry(GtkComboBoxEntry **box, uint32_t count, GList **items, gboolean *multi){
	GList *iter;

	// FIXME: use gtk_combo_box_text_remove_all() in future (gtk 3.0)
	// https://bugzilla.gnome.org/show_bug.cgi?id=324899
	gtk_list_store_clear(GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(*box))));

	// FIXME: how to make a nice empty combo box without the append/remove?
	if(count == 0){
		gtk_combo_box_append_text(GTK_COMBO_BOX(*box), "");
		gtk_combo_box_set_active(GTK_COMBO_BOX(*box), 0);
		gtk_combo_box_remove_text(GTK_COMBO_BOX(*box), 0);
		
		*multi = FALSE;
		return;
	}

	if(count>1){
		gtk_combo_box_append_text(GTK_COMBO_BOX(*box), _("<leave unchanged>")); // FIXME: should be italic!
		gtk_combo_box_set_button_sensitivity(GTK_COMBO_BOX(*box), GTK_SENSITIVITY_AUTO);
		*multi = TRUE;
	} else {
		gtk_combo_box_set_button_sensitivity(GTK_COMBO_BOX(*box), GTK_SENSITIVITY_OFF);
		*multi = FALSE;
	}
	if((iter = g_list_first(*items)) != NULL){
		do{
			gtk_combo_box_append_text(GTK_COMBO_BOX(*box), iter->data); // FIXME: dt segfaults when there are illegal characters in the string.
		} while((iter=g_list_next(iter)) != NULL);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(*box), 0);
}

static GList* get_metadata_from_images_table(const char* key, int imgsel, uint32_t *count){
	sqlite3_stmt *stmt;
	GList *result = NULL;

	if(imgsel < 0){ // selected images
		char query[1024];
		snprintf(query, 1024, "select distinct %s from images where id in (select imgid from selected_images) order by %s", key, key);
		sqlite3_prepare_v2(darktable.db, query, -1, &stmt, NULL);
	} else { // single image under mouse cursor
		char query[1024];
		snprintf(query, 1024, "select distinct %s from images where id = %d order by %s", key, imgsel, key);
		sqlite3_prepare_v2(darktable.db, query, -1, &stmt, NULL);
	}
	while(sqlite3_step(stmt) == SQLITE_ROW){
		char *value = (char*)sqlite3_column_text(stmt, 0);
		if(value != NULL && value[0] != '\0'){
			(*count)++;
			result = g_list_append(result, g_strdup(value));
		}
	}
	sqlite3_finalize(stmt);
	return result;
}

static void update(dt_lib_module_t *user_data, gboolean early_bark_out){
// 	early_bark_out = FALSE; // FIXME: when barking out early we don't update on ctrl-a/ctrl-shift-a. but otherwise it's impossible to edit text
	dt_lib_module_t *self = (dt_lib_module_t *)user_data;
	dt_lib_metadata_t *d  = (dt_lib_metadata_t *)self->data;
	int imgsel = -1;
	DT_CTL_GET_GLOBAL(imgsel, lib_image_mouse_over_id);
	if(early_bark_out && imgsel == d->imgsel)
		return;

	d->imgsel = imgsel;

	int rc;
	sqlite3_stmt *stmt;

	GList *title       = NULL;   uint32_t title_count       = 0;
	GList *description = NULL;   uint32_t description_count = 0;
	GList *creator     = NULL;   uint32_t creator_count     = 0;
	GList *publisher   = NULL;   uint32_t publisher_count   = 0;
	GList *license     = NULL;   uint32_t license_count     = 0;

	// creator and publisher -- get it directly in one query
	if(imgsel < 0){ // selected images
		rc = sqlite3_prepare_v2(darktable.db, "select key, value from meta_data where id in (select imgid from selected_images) group by key, value order by value", -1, &stmt, NULL);
	} else { // single image under mouse cursor
		char query[1024];
		snprintf(query, 1024, "select key, value from meta_data where id = %d group by key, value order by value", imgsel);
		rc = sqlite3_prepare_v2(darktable.db, query, -1, &stmt, NULL);
	}
	while(sqlite3_step(stmt) == SQLITE_ROW){
		char *value = g_strdup((char *)sqlite3_column_text(stmt, 1));
		switch(sqlite3_column_int(stmt, 0)){
			case DT_IMAGE_METADATA_CREATOR:
				creator_count++;
				creator = g_list_append(creator, value);
				break;
			case DT_IMAGE_METADATA_PUBLISHER:
				publisher_count++;
				publisher = g_list_append(publisher, value);
				break;
		}
	}
	rc = sqlite3_finalize(stmt);

	// title, description and license -- they have to be fetched in single queries anyways, so these are in a separate function
	title       = get_metadata_from_images_table("caption", imgsel, &title_count);
	description = get_metadata_from_images_table("description", imgsel, &description_count);
	license     = get_metadata_from_images_table("license", imgsel, &license_count);

	fill_combo_box_entry(&(d->title), title_count, &title, &(d->multi_title));
	fill_combo_box_entry(&(d->description), description_count, &description, &(d->multi_description));
	fill_combo_box_entry(&(d->license), license_count, &license, &(d->multi_license));
	fill_combo_box_entry(&(d->creator), creator_count, &creator, &(d->multi_creator));
	fill_combo_box_entry(&(d->publisher), publisher_count, &publisher, &(d->multi_publisher));
}

static gboolean expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data){
	if(!dt_control_running())
		return FALSE;
	update((dt_lib_module_t*)user_data, TRUE);
	return FALSE;
}

static void clear_button_clicked(GtkButton *button, gpointer user_data){
	sqlite3_exec(darktable.db, "delete from meta_data where id in (select imgid from selected_images)", NULL, NULL, NULL);
	sqlite3_exec(darktable.db, "update images set caption = \"\", description = \"\", license = \"\" where id in (select imgid from selected_images)", NULL, NULL, NULL);

	update(user_data, FALSE);
}

static void apply_button_clicked_helper_1(dt_image_metadata_t key, const gchar* value, gboolean multi, gint active){
	sqlite3_stmt *stmt;
	if(value != NULL && (multi == FALSE || active != 0)){
		sqlite3_prepare_v2(darktable.db, "delete from meta_data where id in (select imgid from selected_images) and key = ?1", -1, &stmt, NULL);
		sqlite3_bind_int(stmt, 1, key);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		if(value != NULL && value[0] != '\0'){
			sqlite3_prepare_v2(darktable.db, "insert into meta_data (id, key, value) select imgid, ?1, ?2 from selected_images", -1, &stmt, NULL);
			sqlite3_bind_int(stmt, 1, key);
			sqlite3_bind_text(stmt, 2, value, -1, NULL);
			sqlite3_step(stmt);
			sqlite3_finalize(stmt);
		}
	}
}

static void apply_button_clicked_helper_2(const char* key, const gchar* value, gboolean multi, gint active){
	sqlite3_stmt *stmt;
	if(multi == FALSE || active != 0){
		char query[1024];
		snprintf(query, 1024, "update images set %s = ?1 where id in (select imgid from selected_images)", key);
		sqlite3_prepare_v2(darktable.db, query, -1, &stmt, NULL);
		sqlite3_bind_text(stmt, 1, value, -1, NULL);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}
}

static void apply_button_clicked(GtkButton *button, gpointer user_data){
	dt_lib_module_t *self = (dt_lib_module_t *)user_data;
	dt_lib_metadata_t *d  = (dt_lib_metadata_t *)self->data;

	gchar *title       = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->title));
	gchar *description = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->description));
	gchar *license     = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->license));
	gchar *creator     = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->creator));
	gchar *publisher   = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->publisher));

	// the metadata stored in table meta_data -- can change in future
	apply_button_clicked_helper_1(DT_IMAGE_METADATA_CREATOR, creator, d->multi_creator, gtk_combo_box_get_active(GTK_COMBO_BOX(d->creator)));
	apply_button_clicked_helper_1(DT_IMAGE_METADATA_PUBLISHER, publisher, d->multi_publisher, gtk_combo_box_get_active(GTK_COMBO_BOX(d->publisher)));

	// the metadata stored in table images -- fixed
	apply_button_clicked_helper_2("caption", title, d->multi_title, gtk_combo_box_get_active(GTK_COMBO_BOX(d->title)));
	apply_button_clicked_helper_2("description", description, d->multi_description, gtk_combo_box_get_active(GTK_COMBO_BOX(d->description)));
	apply_button_clicked_helper_2("license", license, d->multi_license, gtk_combo_box_get_active(GTK_COMBO_BOX(d->license)));

	if(title != NULL)
		g_free(title);
	if(description != NULL)
		g_free(description);
	if(license != NULL)
		g_free(license);
	if(creator != NULL)
		g_free(creator);
	if(publisher != NULL)
		g_free(publisher);

	update(user_data, FALSE);
}

void gui_reset(dt_lib_module_t *self){
	update(self, FALSE);
}

int position(){
	return 510;
}



void gui_init(dt_lib_module_t *self){
	GtkBox *hbox;
	GtkWidget *button;
	GtkWidget *label;
	GtkEntryCompletion *completion;
	
	dt_lib_metadata_t *d = (dt_lib_metadata_t *)malloc(sizeof(dt_lib_metadata_t));
	self->data = (void *)d;

	d->imgsel = -1;

	self->widget = gtk_table_new(6, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(self->widget), 5);

	g_signal_connect(self->widget, "expose-event", G_CALLBACK(expose), (gpointer)self);
	darktable.gui->redraw_widgets = g_list_append(darktable.gui->redraw_widgets, self->widget);

	label = gtk_label_new(_("title"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(self->widget), label, 0, 1, 0, 1, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	d->title = GTK_COMBO_BOX_ENTRY(gtk_combo_box_entry_new_text());
	dt_gui_key_accel_block_on_focus(GTK_WIDGET(gtk_bin_get_child(GTK_BIN(d->title))));
	completion = gtk_entry_completion_new();
	gtk_entry_completion_set_model(completion, gtk_combo_box_get_model(GTK_COMBO_BOX(d->title)));
	gtk_entry_completion_set_text_column(completion, 0);
	gtk_entry_completion_set_inline_completion(completion, TRUE);
	gtk_entry_set_completion(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(d->title))), completion);
	gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(d->title), 1, 2, 0, 1, GTK_EXPAND|GTK_FILL, 0, 0, 0);

	label = gtk_label_new(_("description"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(self->widget), label, 0, 1, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	d->description = GTK_COMBO_BOX_ENTRY(gtk_combo_box_entry_new_text());
	dt_gui_key_accel_block_on_focus(GTK_WIDGET(gtk_bin_get_child(GTK_BIN(d->description))));
	completion = gtk_entry_completion_new();
	gtk_entry_completion_set_model(completion, gtk_combo_box_get_model(GTK_COMBO_BOX(d->description)));
	gtk_entry_completion_set_text_column(completion, 0);
	gtk_entry_completion_set_inline_completion(completion, TRUE);
	gtk_entry_set_completion(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(d->description))), completion);
	gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(d->description), 1, 2, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);

	label = gtk_label_new(_("creator"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(self->widget), label, 0, 1, 2, 3, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	d->creator = GTK_COMBO_BOX_ENTRY(gtk_combo_box_entry_new_text());
	dt_gui_key_accel_block_on_focus(GTK_WIDGET(gtk_bin_get_child(GTK_BIN(d->creator))));
	completion = gtk_entry_completion_new();
	gtk_entry_completion_set_model(completion, gtk_combo_box_get_model(GTK_COMBO_BOX(d->creator)));
	gtk_entry_completion_set_text_column(completion, 0);
	gtk_entry_completion_set_inline_completion(completion, TRUE);
	gtk_entry_set_completion(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(d->creator))), completion);
	gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(d->creator), 1, 2, 2, 3, GTK_EXPAND|GTK_FILL, 0, 0, 0);

	label = gtk_label_new(_("publisher"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(self->widget), label, 0, 1, 3, 4, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	d->publisher = GTK_COMBO_BOX_ENTRY(gtk_combo_box_entry_new_text());
	dt_gui_key_accel_block_on_focus(GTK_WIDGET(gtk_bin_get_child(GTK_BIN(d->publisher))));
	completion = gtk_entry_completion_new();
	gtk_entry_completion_set_model(completion, gtk_combo_box_get_model(GTK_COMBO_BOX(d->publisher)));
	gtk_entry_completion_set_text_column(completion, 0);
	gtk_entry_completion_set_inline_completion(completion, TRUE);
	gtk_entry_set_completion(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(d->publisher))), completion);
	gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(d->publisher), 1, 2, 3, 4, GTK_EXPAND|GTK_FILL, 0, 0, 0);

	label = gtk_label_new(_("license"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(self->widget), label, 0, 1, 4, 5, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	d->license = GTK_COMBO_BOX_ENTRY(gtk_combo_box_entry_new_text());
	dt_gui_key_accel_block_on_focus(GTK_WIDGET(gtk_bin_get_child(GTK_BIN(d->license))));
	completion = gtk_entry_completion_new();
	gtk_entry_completion_set_model(completion, gtk_combo_box_get_model(GTK_COMBO_BOX(d->license)));
	gtk_entry_completion_set_text_column(completion, 0);
	gtk_entry_completion_set_inline_completion(completion, TRUE);
	gtk_entry_set_completion(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(d->license))), completion);
	gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(d->license), 1, 2, 4, 5, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	
	g_object_unref(completion);

	// reset/apply buttons
	hbox = GTK_BOX(gtk_hbox_new(TRUE, 5));
	
	button = gtk_button_new_with_label(_("clear"));
	gtk_object_set(GTK_OBJECT(button), "tooltip-text", _("remove metadata from selected images"), (char *)NULL);
	gtk_box_pack_start(hbox, button, FALSE, TRUE, 0);
	g_signal_connect(G_OBJECT (button), "clicked",
					G_CALLBACK (clear_button_clicked), (gpointer)self);

	button = gtk_button_new_with_label(_("apply"));
	gtk_object_set(GTK_OBJECT(button), "tooltip-text", _("write metadata to selected images"), (char *)NULL);
	g_signal_connect(G_OBJECT (button), "clicked",
					G_CALLBACK (apply_button_clicked), (gpointer)self);
	gtk_box_pack_start(hbox, button, FALSE, TRUE, 0);

	gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(hbox), 0, 2, 5, 6, GTK_EXPAND|GTK_FILL, 0, 0, 0);

	update(self, FALSE);
}

void gui_cleanup(dt_lib_module_t *self){
	darktable.gui->redraw_widgets = g_list_remove(darktable.gui->redraw_widgets, self->widget);
	free(self->data);
	self->data = NULL;
}

static void add_license_preset(dt_lib_module_t *self, char *name, char *string){
	unsigned int params_size = strlen(string)+5;

	char *params = calloc(sizeof(char), params_size);
	memcpy(params+2, string, params_size-5);
	dt_lib_presets_add(name, self->plugin_name, params, params_size);
	free(params);
}

void init_presets(dt_lib_module_t *self){

	// <title>\0<description>\0<license>\0<creator>\0<publisher>

	add_license_preset(self, _("CC-by"), _("Creative Commons Attribution (CC-BY)"));
	add_license_preset(self, _("CC-by-sa"), _("Creative Commons Attribution-ShareAlike (CC-BY-SA)"));
	add_license_preset(self, _("CC-by-nd"), _("Creative Commons Attribution-NoDerivs (CC-BY-ND)"));
	add_license_preset(self, _("CC-by-nc"), _("Creative Commons Attribution-NonCommercial (CC-BY-NC)"));
	add_license_preset(self, _("CC-by-nc-sa"), _("Creative Commons Attribution-NonCommercial-ShareAlike (CC-BY-NC-SA)"));
	add_license_preset(self, _("CC-by-nc-nd"), _("Creative Commons Attribution-NonCommercial-NoDerivs (CC-BY-NC-ND)"));
	add_license_preset(self, _("all rights reserved"), _("All rights reserved."));
}

// FIXME: Is this ever called?
void init(dt_iop_module_t *module){
// 	g_print("init\n");
}

void* get_params(dt_lib_module_t *self, int *size){
	dt_lib_metadata_t *d = (dt_lib_metadata_t *)self->data;

	char *title       = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->title));
	char *description = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->description));
	char *license     = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->license));
	char *creator     = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->creator));
	char *publisher   = gtk_combo_box_get_active_text(GTK_COMBO_BOX(d->publisher));

	int32_t title_len       = strlen(title);
	int32_t description_len = strlen(description);
	int32_t license_len     = strlen(license);
	int32_t creator_len     = strlen(creator);
	int32_t publisher_len   = strlen(publisher);

	*size = title_len + description_len + license_len + creator_len + publisher_len + 5;

	char *params = (char *)malloc(*size);
	
	int pos = 0;
	memcpy(params+pos, title, title_len+1);                 pos += title_len+1;
	memcpy(params+pos, description, description_len+1);     pos += description_len+1;
	memcpy(params+pos, license, license_len+1);             pos += license_len+1;
	memcpy(params+pos, creator, creator_len+1);             pos += creator_len+1;
	memcpy(params+pos, publisher, publisher_len+1);         pos += publisher_len+1;

	g_assert(pos == *size);

	return params;
}

static void set_params_helper_1(dt_image_metadata_t key, const gchar* value){
	sqlite3_stmt *stmt;

	if(value != NULL && value[0] != '\0'){
		sqlite3_prepare_v2(darktable.db, "delete from meta_data where id in (select imgid from selected_images) and key = ?1", -1, &stmt, NULL);
		sqlite3_bind_int(stmt, 1, key);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		sqlite3_prepare_v2(darktable.db, "insert into meta_data (id, key, value) select imgid, ?1, ?2 from selected_images", -1, &stmt, NULL);
		sqlite3_bind_int(stmt, 1, key);
		sqlite3_bind_text(stmt, 2, value, -1, NULL);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}
}

static void set_params_helper_2(const char* key, const gchar* value){
	sqlite3_stmt *stmt;

	if(value != NULL && value[0] != '\0'){
		char query[1024];
		snprintf(query, 1024, "update images set %s = ?1 where id in (select imgid from selected_images)", key);
		sqlite3_prepare_v2(darktable.db, query, -1, &stmt, NULL);
		sqlite3_bind_text(stmt, 1, value, -1, NULL);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}
}


int set_params(dt_lib_module_t *self, const void *params, int size){
	char *buf         = (char* )params;
	char *title       = buf;            buf += strlen(title) + 1;
	char *description = buf;            buf += strlen(description) + 1;
	char *license     = buf;            buf += strlen(license) + 1;
	char *creator     = buf;            buf += strlen(creator) + 1;
	char *publisher   = buf;            buf += strlen(publisher) + 1;

	if(size != strlen(title) + strlen(description) + strlen(license) + strlen(creator) + strlen(publisher) + 5) return 1;

	// the metadata stored in table meta_data -- can change in future
	set_params_helper_1(DT_IMAGE_METADATA_CREATOR, creator);
	set_params_helper_1(DT_IMAGE_METADATA_PUBLISHER, publisher);

	// the metadata stored in table images -- fixed
	set_params_helper_2("caption", title);
	set_params_helper_2("description", description);
	set_params_helper_2("license", license);

	update(self, FALSE);
	return 0;
}
