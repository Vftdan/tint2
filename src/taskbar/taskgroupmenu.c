#include "taskgroupmenu.h"
#include "task.h"
#include "server.h"
#include "panel.h"

static void group_menu_clear_background(void *obj);

TaskGroupMenuStyle g_task_group_menu_style;
GList *task_group_menus;

void init_task_group_menu_style()
{
    g_task_group_menu_style.paddingx = 0;
    g_task_group_menu_style.paddingy = 0;
    g_task_group_menu_style.spacing = 0;
    g_task_group_menu_style.horizontal = TRUE;
    g_task_group_menu_style.bg = NULL;
}

static void redraw_timer_callback(void *obj)
{
    TaskGroupMenu *group_menu = obj;
    int group_desktop = group_menu->group->desktop;
    if (group_menu->mapped && group_desktop != server.desktop && group_desktop != ALL_DESKTOPS)
        task_group_menu_hide(group_menu);
    if (group_menu->dirty)
        task_group_menu_update(group_menu);
}

TaskGroupMenu *add_task_group_menu(Task *task_group)
{
    TaskGroupMenu *group_menu = calloc(1, sizeof(TaskGroupMenu));
    group_menu->style = &g_task_group_menu_style;
    group_menu->dirty = TRUE;
    if (task_group) {
        group_menu->group = task_group;
        // TODO handle task_group->group_menu != NULL?
        task_group->group_menu = group_menu;
        group_menu->panel = task_group->area.panel;
    }
    INIT_TIMER(group_menu->redraw_timer);

    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.event_mask = StructureNotifyMask | ExposureMask | ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | PropertyChangeMask | PointerMotionMask | LeaveWindowMask;
    attr.colormap = server.colormap;
    attr.background_pixel = 0;
    attr.border_pixel = 0;
    unsigned long mask = CWEventMask | CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect;
    group_menu->window = XCreateWindow(server.display,
                                       server.root_win,
                                       0,
                                       0,
                                       100,
                                       20,
                                       0,
                                       server.depth,
                                       InputOutput,
                                       server.visual,
                                       mask,
                                       &attr);

    if (!group_menu->panel)
        group_menu->panel = panels;
    Panel *panel = group_menu->panel;

    if (!group_menu->area.bg)
        group_menu->area.bg = &g_array_index(backgrounds, Background, 0);
    group_menu->area.parent = group_menu;
    group_menu->area.panel = panel;
    snprintf(group_menu->area.name, sizeof(group_menu->area.name), "Group menu %s", task_group ? task_group->area.name : "");
    group_menu->area.on_screen = TRUE;
    group_menu->area.resize_needed = 1;
    group_menu->area.size_mode = LAYOUT_DYNAMIC;
    group_menu->area._resize = resize_group_menu;
    group_menu->area._clear = group_menu_clear_background;

    task_group_menus = g_list_prepend(task_group_menus, group_menu);

    return group_menu;
}

void remove_task_group_menu(TaskGroupMenu *group_menu)
{
    Task *group = group_menu->group;
    if (group) {
        if (group->group_menu == group_menu)
            group->group_menu = NULL;
    }

    if (group_menu->window)
        XDestroyWindow(server.display, group_menu->window);

    destroy_timer(&group_menu->redraw_timer);

    task_group_menus = g_list_remove(task_group_menus, group_menu);

    free(group_menu);
}

TaskGroupMenu *task_group_menu_from_xwin(Window win)
{
    for (GList *l = task_group_menus; l; l = l->next) {
        TaskGroupMenu *group_menu = l->data;
        if (group_menu->window == win)
            return group_menu;
    }
    return NULL;
}

void task_group_menu_update_style(TaskGroupMenu *group_menu)
{
    group_menu->area.bg = g_task_group_menu_style.bg;
    task_group_menu_update(group_menu);
}

void task_group_menu_update_geometry(TaskGroupMenu *group_menu)
{
    Panel *panel = group_menu->panel;
    int screen_width = server.monitors[panel->monitor].width;
    int screen_height = server.monitors[panel->monitor].height;

    int width = 0,
        height = 0;
    TaskGroupMenuStyle style = {
        .paddingx = 0,
        .paddingy = 0,
        .spacing = 0,
        .horizontal = TRUE,
        .bg = NULL,
    };
    if (group_menu->style)
        style = *group_menu->style;
    int available_width = screen_width - style.paddingx * 2;
    int available_height = screen_height - style.paddingy * 2;
    if (panel_horizontal)
        available_height -= panel->area.height;
    else
        available_width -= panel->area.width;
    
    int fixed_children_count = 0;
    int dynamic_children_count = 0;
    gboolean first_child = TRUE;
    for (GList *l = group_menu->area.children; l; l = l->next) {
        Area *child = (Area *)l->data;
        if (!child->on_screen)
            continue;

        if (!first_child) {
            if (style.horizontal)
                width += style.spacing;
            else
                height += style.spacing;
        }
        first_child = FALSE;

        int child_width = child->width;
        int child_height = child->height;
        switch (child->size_mode) {
        case LAYOUT_FIXED:
            fixed_children_count++;
            if (style.horizontal) {
                width += child_width;
                if (height < child_height)
                    height = child_height;
            } else {
                if (width < child_width)
                    width = child_width;
                height += child_height;
            }
            break;
        case LAYOUT_DYNAMIC:
            dynamic_children_count++;
            break;
        }
    }
    int remaining_width = available_width - width;
    int remaining_height = available_height - height;
    if (remaining_width < 0)
        remaining_width = 0;
    if (remaining_height < 0)
        remaining_height = 0;
    if (dynamic_children_count > 0) {
        int remaining_size = panel_horizontal ? remaining_width : remaining_height;
        int denominator = (!!style.horizontal) == (!!panel_horizontal) ? dynamic_children_count : 1;

        int dynamic_size = remaining_size / denominator;
        int max_dynamic_size = panel_horizontal ? panel_config.g_task.maximum_width : panel_config.g_task.maximum_height;
        int max_dynamic_size_perp = panel_horizontal ? panel_config.g_task.maximum_height : panel_config.g_task.maximum_width;
        dynamic_size = MIN(dynamic_size, max_dynamic_size);
        int modulo = remaining_size - dynamic_size * denominator;

        for (GList *l = group_menu->area.children; l; l = l->next) {
            Area *child = (Area *)l->data;
            if (child->on_screen && child->size_mode == LAYOUT_DYNAMIC) {
                int *sizeptr = panel_horizontal ? &child->width : &child->height;
                int *sizeptr_perp = panel_horizontal ? &child->height : &child->width;
                int old_size = *sizeptr;
                int old_size_perp = *sizeptr_perp;
                *sizeptr = dynamic_size;
                *sizeptr_perp = MIN(old_size_perp, max_dynamic_size_perp);
                if (modulo && dynamic_size < max_dynamic_size) {
                    (*sizeptr)++;
                    modulo--;
                }
                if (*sizeptr != old_size || *sizeptr_perp != old_size_perp)
                    child->_changed = TRUE;

                int child_width = child->width;
                int child_height = child->height;
                if (style.horizontal) {
                    width += child_width;
                    if (height < child_height)
                        height = child_height;
                } else {
                    if (width < child_width)
                        width = child_width;
                    height += child_height;
                }
            }
        }

        int posx = style.paddingx;
        int posy = style.paddingy;
        for (GList *l = group_menu->area.children; l; l = l->next) {
            Area *child = (Area *)l->data;
            if (child->on_screen) {
                child->posx = posx;
                child->posy = posy;
                relayout(child);
                if (style.horizontal)
                    posx += child->width + style.spacing;
                else
                    posy += child->height + style.spacing;
            }
        }
    }

    width += 2 * style.paddingx;
    height += 2 * style.paddingy;
    group_menu->width = width;
    group_menu->height = height;

    Area *trigger_area = &group_menu->group->area;
    group_menu->x = trigger_area->posx + (trigger_area->width - width) / 2;
    group_menu->y = trigger_area->posy + (trigger_area->height - height) / 2;
    if (panel_horizontal && panel_position & BOTTOM)
        group_menu->y = panel->posy - height;
    else if (panel_horizontal && panel_position & TOP)
        group_menu->y = panel->posy + panel->area.height;
    else if (panel_position & LEFT)
        group_menu->x = panel->posx + panel->area.width;
    else
        group_menu->x = panel->posx - width;
}

void task_group_menu_adjust_geometry(TaskGroupMenu *group_menu)
{
    Panel *panel = group_menu->panel;
    int screen_width = server.monitors[panel->monitor].width;
    int screen_height = server.monitors[panel->monitor].height;
    int x = group_menu->x;
    int y = group_menu->y;
    int width = group_menu->width;
    int height = group_menu->height;

    if (x + width <= screen_width && y + height <= screen_height && x >= server.monitors[panel->monitor].x &&
        y >= server.monitors[panel->monitor].y)
        return; // no adjustment needed

    int min_x, min_y, max_width, max_height;
    if (panel_horizontal) {
        min_x = 0;
        max_width = server.monitors[panel->monitor].width;
        max_height = server.monitors[panel->monitor].height - panel->area.height;
        if (panel_position & BOTTOM)
            min_y = 0;
        else
            min_y = panel->area.height;
    } else {
        max_width = server.monitors[panel->monitor].width - panel->area.width;
        min_y = 0;
        max_height = server.monitors[panel->monitor].height;
        if (panel_position & LEFT)
            min_x = panel->area.width;
        else
            min_x = 0;
    }

    if (x + width > server.monitors[panel->monitor].x + server.monitors[panel->monitor].width)
        x = server.monitors[panel->monitor].x + server.monitors[panel->monitor].width - width;
    if (y + height > server.monitors[panel->monitor].y + server.monitors[panel->monitor].height)
        y = server.monitors[panel->monitor].y + server.monitors[panel->monitor].height - height;

    if (x < min_x)
        x = min_x;
    if (width > max_width)
        width = max_width;
    if (y < min_y)
        y = min_y;
    if (height > max_height)
        height = max_height;

    group_menu->x = x;
    group_menu->y = y;
    group_menu->width = width;
    group_menu->height = height;
}

void task_group_menu_update(TaskGroupMenu *group_menu)
{
    Panel *panel = group_menu->panel;

    task_group_menu_update_geometry(group_menu);
    task_group_menu_adjust_geometry(group_menu);
    group_menu->area.width = group_menu->width;
    group_menu->area.height = group_menu->height;
    XMoveResizeWindow(server.display, group_menu->window, group_menu->x, group_menu->y, group_menu->width, group_menu->height);

    Pixmap panel_pmap = panel->temp_pmap;
    Pixmap menu_pmap = XCreatePixmap(server.display,
                                     server.root_win,
                                     group_menu->area.width,
                                     group_menu->area.height,
                                     server.depth);
    panel->temp_pmap = menu_pmap;
    draw_tree(&group_menu->area);
    panel->temp_pmap = panel_pmap;
    XCopyArea(server.display,
              menu_pmap,
              group_menu->window,
              server.gc,
              0,
              0,
              group_menu->area.width,
              group_menu->area.height,
              0,
              0);
    XFreePixmap(server.display, menu_pmap);

    group_menu->dirty = FALSE;
}

void task_group_menu_show(TaskGroupMenu *group_menu)
{
    group_menu->mapped = True;
    XMapWindow(server.display, group_menu->window);
    task_group_menu_update_style(group_menu);
    int redraw_timer_period = 1000 / 60;
    change_timer(&group_menu->redraw_timer, true, redraw_timer_period, redraw_timer_period, redraw_timer_callback, group_menu);
    XFlush(server.display);
}

void task_group_menu_hide(TaskGroupMenu *group_menu)
{
    if (group_menu->mapped) {
        group_menu->mapped = False;
        stop_timer(&group_menu->redraw_timer);
        XUnmapWindow(server.display, group_menu->window);
        XFlush(server.display);
    }
}

void task_group_menu_hide_all()
{
    for (GList *l = task_group_menus; l; l = l->next) {
        TaskGroupMenu *group_menu = l->data;
        if (group_menu->mapped)
            task_group_menu_hide(group_menu);
    }
}

gboolean resize_group_menu(void *obj)
{
    TaskGroupMenu *group_menu = obj;
    task_group_menu_update_geometry(group_menu);
    return FALSE;
}

gboolean group_menu_task_is_under_mouse(void *obj, int x, int y)
{
    Area *a = obj;
    if (!a->on_screen)
        return FALSE;

    if (a->_is_under_mouse && a->_is_under_mouse != group_menu_task_is_under_mouse)
        return a->_is_under_mouse(a, x, y);

    TaskGroupMenu *group_menu = a->parent;
    if (!g_list_find(task_group_menus, group_menu)) {
        gboolean (*handler)(void *obj, int x, int y) = a->_is_under_mouse;
        a->_is_under_mouse = NULL;
        gboolean result = area_is_under_mouse(a, x, y);
        a->_is_under_mouse = handler;
        return result;
    }

    if (group_menu->style->horizontal)
        return (x >= a->posx) && (x <= a->posx + a->width);
    else
        return (y >= a->posy) && (y <= a->posy + a->height);
}

void group_menu_clear_background(void *obj)
{
    TaskGroupMenu *group_menu = obj;
    clear_pixmap(group_menu->area.pix, 0, 0, group_menu->area.width, group_menu->area.height);
    if (!server.real_transparency) {
        get_root_pixmap();
        Window dummy;
        int x, y;
        XTranslateCoordinates(server.display, group_menu->window, server.root_win, 0, 0, &x, &y, &dummy);

        XSetTSOrigin(server.display, server.gc, -x, -y);
        XFillRectangle(server.display, group_menu->area.pix, server.gc, 0, 0, group_menu->area.width, group_menu->area.height);
    }
}
