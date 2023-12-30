#ifndef TASKGROUPMENU_H
#define TASKGROUPMENU_H

#include "common.h"
#include "timer.h"

struct Task;

typedef struct {
    int paddingx;
    int paddingy;
    int spacing;
    gboolean horizontal;
    Background *bg;
} TaskGroupMenuStyle;

extern TaskGroupMenuStyle g_task_group_menu_style;

void init_task_group_menu_style();

typedef struct TaskGroupMenu {
    Area area;
    struct Task *group;
    struct Panel *panel;
    Window window;
    Timer redraw_timer;
    Bool mapped;
    gboolean dirty;
    TaskGroupMenuStyle *style;
    int x, y, width, height;
} TaskGroupMenu;

extern GList *task_group_menus;

TaskGroupMenu *add_task_group_menu(struct Task *task_group);
void remove_task_group_menu(TaskGroupMenu *group_menu);

TaskGroupMenu *task_group_menu_from_xwin(Window win);

void task_group_menu_update_style(TaskGroupMenu *group_menu);
void task_group_menu_update_geometry(TaskGroupMenu *group_menu);
void task_group_menu_adjust_geometry(TaskGroupMenu *group_menu);
void task_group_menu_update(TaskGroupMenu *group_menu);

void task_group_menu_show(TaskGroupMenu *group_menu);
void task_group_menu_hide(TaskGroupMenu *group_menu);
void task_group_menu_hide_all();

gboolean resize_group_menu(void *obj);
gboolean group_menu_task_is_under_mouse(void *obj, int x, int y);

#endif
