/**************************************************************************
* Tint2 : .desktop file handling
*
* Copyright (C) 2015       (mrovi9000@gmail.com)
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License version 2
* as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**************************************************************************/

/* http://standards.freedesktop.org/desktop-entry-spec/ */

#include "apps-common.h"
#include "common.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int parse_dektop_line(char *line, char **key, char **value)
{
    if (*line == '=')
        return 0;

    char *p = strchr(line, '=');    
    if (!p)
        return 0;

    *p++ = '\0';
    if (!*p)
        return 0;

    *value = p;
    *key = line;
    return 1;
}

void expand_exec(DesktopEntry *entry, const char *path)
{
    // Expand % in exec
    // %i -> --icon Icon
    // %c -> Name
    // %k -> path
    if (entry->exec) {
        size_t buf_size = strlen(entry->exec)
                        + (entry->name ? strlen(entry->name) : 1)
                        + (entry->icon ? strlen(entry->icon) : 1) + 100;
        char *exec2 = calloc(buf_size, 1);
        char *p, *q;
        // p will never point to an escaped char
        for (p = entry->exec, q = exec2; *p; p++, q++) {
            *q = *p; // Copy
            if (*p == '\\') {
                p++, q++;
                // Copy the escaped char
                if (*p == '%') // For % we delete the backslash, i.e. write % over it
                    q--;
                *q = *p;
                if (!*p)
                    break;
                continue;
            }
            if (*p == '%') {
                p++;
                switch (*p) {
                case 'i':   if (entry->icon) {
                                snprintf(q, buf_size-1, "--icon '%s'", entry->icon);
                                char *old = q;
                                q += strlen_const("--icon ''");
                                q += strlen(entry->icon);
                                buf_size -= (size_t)(q - old);
                            }
                            break;
                case 'c':   if (entry->name) {
                                snprintf(q, buf_size-1, "'%s'", entry->name);
                                char *old = q;
                                q += strlen_const("''");
                                q += strlen(entry->name);
                                buf_size -= (size_t)(q - old);
                            } else {
                                snprintf(q, buf_size-1, "'%s'", path);
                                char *old = q;
                                q += strlen_const("''");
                                q += strlen(path);
                                buf_size -= (size_t)(q - old);
                            }
                            break;
                case 'F':
                case 'f':   snprintf(q, buf_size-1, "%c%c", '%', *p);
                            q += 2;
                            buf_size -= 2;
                            break;
                case '\0':  goto endloop;
                }
                q--; // To balance the q++ in the for
                     // Or delete the last % from q
                continue;
            }
        }
    endloop:
        *q = '\0';
        free(entry->exec);
        entry->exec = exec2;
    }
}

gboolean read_desktop_file_full_path(const char *path, DesktopEntry *entry)
{
    entry->name = entry->generic_name = entry->icon = entry->exec = entry->cwd = NULL;
    entry->hidden_from_menus = FALSE;
    entry->start_in_terminal = FALSE;
    entry->startup_notification = TRUE;

    FILE *fp = fopen(path, "rt");
    if (fp == NULL) {
        fprintf(stderr, "tint2: Could not open file %s\n", path);
        return FALSE;
    }

    const gchar **languages = (const gchar **)g_get_language_names();
    // lang_index is the index of the language for the best Name key in the language vector
    // lang_index_default is a constant that encodes the Name key without a language
    int lang_index_default = 1;
#define LANG_DBG 0
    if (LANG_DBG)
        fprintf(stderr, "tint2: Languages:");
    for (int i = 0; languages[i]; i++) {
        lang_index_default = i + 1;
        if (LANG_DBG)
            fprintf(stderr, "tint2:  %s", languages[i]);
    }
    if (LANG_DBG)
        fprintf(stderr, "tint2: \n");
    // we currently do not know about any Name key at all, so use an invalid index
    int lang_index_name = lang_index_default + 1;
    int lang_index_generic_name = lang_index_default + 1;

    gboolean inside_desktop_entry = FALSE;
    char *line = NULL;
    size_t line_size;
    ssize_t len;
    while ((len = getline(&line, &line_size, fp)) >= 0) {
        if (len == 0)
            continue;
        str_strip_newline (line, len);
        if (line[0] == '[') {
            inside_desktop_entry = str_lequal_static (line, "[Desktop Entry]", len);
        }
        char *key, *value;
        if (inside_desktop_entry && parse_dektop_line(line, &key, &value))
        {
            if (startswith_static (key, "Name")) {
                if (lang_index_name > lang_index_default && !key[strlen_const("Name")]) {
                    lang_index_name = lang_index_default;
                    entry->name = strdup(value);
                } else {
                    for (int i = 0; languages[i] && i < lang_index_name; i++) {
                        gchar *localized_key = g_strdup_printf("Name[%s]", languages[i]);
                        if (strcmp(key, localized_key) == 0) {
                            if (entry->name)
                                free(entry->name);
                            entry->name = strdup(value);
                            lang_index_name = i;
                        }
                        g_free(localized_key);
                    }
                }
            } else if (startswith_static (key, "GenericName")) {
                if (lang_index_generic_name > lang_index_default && !key[strlen_const("GenericName")]) {
                    lang_index_generic_name = lang_index_default;
                    entry->generic_name = strdup(value);
                } else {
                    for (int i = 0; languages[i] && i < lang_index_generic_name; i++) {
                        gchar *localized_key = g_strdup_printf("GenericName[%s]", languages[i]);
                        if (strcmp(key, localized_key) == 0) {
                            if (entry->generic_name)
                                free(entry->generic_name);
                            entry->generic_name = strdup(value);
                            lang_index_generic_name = i;
                        }
                        g_free(localized_key);
                    }
                }
            } else {
                switch (str_index (key, (char *[]){"Exec", "Icon", "NoDisplay", "Path", "StartupNotify", "Terminal"}, 6))
                {
                    case 0: if (!entry->exec)
                                entry->exec = strdup(value);
                            break;
                    case 1: if (!entry->icon)
                                entry->icon = strdup(value);
                            break;
                    case 2: entry->hidden_from_menus = strcasecmp(value, "true") == 0;
                            break;
                    case 3: if (!entry->cwd)
                                entry->cwd = strdup(value);
                            break;
                    case 4: entry->startup_notification = strcasecmp(value, "true") == 0;
                            break;
                    case 5: entry->start_in_terminal = strcasecmp(value, "true") == 0;
                            break;
                }
            }
        }
    }
    fclose(fp);
    // From this point:
    // entry->name, entry->generic_name, entry->icon, entry->exec will never be empty strings (can be NULL though)

    expand_exec(entry, entry->path);

    free(line);
    return entry->exec != NULL;
}

gboolean read_desktop_file_from_dir(const char *path, const char *file_name, DesktopEntry *entry)
{
    gchar *full_path = g_build_filename(path, file_name, NULL);
    if (read_desktop_file_full_path(full_path, entry)) {
        g_free(full_path);
        return TRUE;
    }
    free_and_null(entry->name);
    free_and_null(entry->generic_name);
    free_and_null(entry->icon);
    free_and_null(entry->exec);
    free_and_null(entry->cwd);

    GList *subdirs = NULL;

    GDir *d = g_dir_open(path, 0, NULL);
    if (d) {
        const gchar *name;
        while ((name = g_dir_read_name(d))) {
            gchar *child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR)) {
                subdirs = g_list_prepend(subdirs, child);
            } else {
                g_free(child);
            }
        }
        g_dir_close(d);
    }

    subdirs = g_list_sort(subdirs, compare_strings);
    gboolean found = FALSE;
    for (GList *l = subdirs; l; l = l->next) {
        if (read_desktop_file_from_dir(l->data, file_name, entry)) {
            found = TRUE;
            break;
        }
    }

    for (GList *l = subdirs; l; l = l->next) {
        g_free(l->data);
    }
    g_list_free(subdirs);
    g_free(full_path);

    return found;
}

gboolean read_desktop_file(const char *path, DesktopEntry *entry)
{
    entry->path = strdup(path);
    entry->name = entry->generic_name = entry->icon = entry->exec = entry->cwd = NULL;

    if (strchr(path, '/'))
        return read_desktop_file_full_path(path, entry);
    for (const GSList *location = get_apps_locations(); location; location = location->next) {
        if (read_desktop_file_from_dir(location->data, path, entry))
            return TRUE;
    }
    return FALSE;
}

void free_desktop_entry(DesktopEntry *entry)
{
    free_and_null(entry->name);
    free_and_null(entry->generic_name);
    free_and_null(entry->icon);
    free_and_null(entry->exec);
    free_and_null(entry->path);
    free_and_null(entry->cwd);
}

void test_read_desktop_file()
{
    fprintf(stderr, YELLOW);
    DesktopEntry entry;
    read_desktop_file("/usr/share/applications/firefox.desktop", &entry);
    fprintf(stderr, "tint2: Name:%s GenericName:%s Icon:%s Exec:%s\n", entry.name, entry.generic_name, entry.icon, entry.exec);
    fprintf(stderr, RESET);
}

GSList *apps_locations = NULL;
// Do not free the result.
const GSList *get_apps_locations()
{
    if (apps_locations)
        return apps_locations;

    apps_locations = load_locations_from_env(apps_locations, "XDG_DATA_HOME", "applications", NULL);
    apps_locations = load_locations_from_dir(apps_locations, g_get_home_dir(), ".local/share/applications", NULL);
    apps_locations = load_locations_from_env(apps_locations, "XDG_DATA_DIRS", "applications", NULL);

    slist_append_uniq_dup(apps_locations, "/usr/local/share/applications", g_str_equal);
    slist_append_uniq_dup(apps_locations, "/usr/share/applications", g_str_equal);
    slist_append_uniq_dup(apps_locations, "/opt/share/applications", g_str_equal);

    return apps_locations;
}
