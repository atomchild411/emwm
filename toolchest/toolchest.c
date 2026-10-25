/*
 * toolchest - a Toolchest for emwm, after the IRIX Interactive Desktop.
 *
 * A small window holding a column of buttons, each of which opens a menu.
 * The menus are read from a file in emwm's own menu syntax:
 *
 *	Menu ToolChest
 *	{
 *	    "Desktop"			f.menu Desktop
 *	}
 *
 *	Menu Desktop
 *	{
 *	    "Open Unix Shell"	_O	f.exec "xterm -ls"
 *	    no-label			f.separator
 *	    "Log Out"		_L	f.logout
 *	}
 *
 * Functions:
 *	f.exec "command"	run command with /bin/sh
 *	f.menu name		a submenu
 *	f.separator		a line
 *	f.title			a label, not selectable
 *	f.logout		ask, then end the session (see below)
 *	f.restart		read the menus again
 *	f.quit			close the Toolchest
 *
 * The top-level menu is the one named "ToolChest". The file is
 * ~/.toolchestrc if it exists, else SYSTEM_CHESTRC; -f file overrides both.
 *
 * Logging out sends SIGTERM to $DESKTOP_SESSION_PID: the session script
 * exports its own pid there and then execs the window manager, so that
 * pid is the window manager and the session ends when it does.
 *
 * Copyright (c) 2026 atomchild411.
 * This file is part of emwm and is distributed under the terms of the GNU
 * Lesser General Public License, version 2.1 or later (see COPYING).
 */

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Xm/Xm.h>
#include <Xm/CascadeB.h>
#include <Xm/Label.h>
#include <Xm/MessageB.h>
#include <Xm/MwmUtil.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>

#ifndef SYSTEM_CHESTRC
#define SYSTEM_CHESTRC "/usr/local/lib/X11/system.chestrc"
#endif

#define TOP_MENU	"ToolChest"
#define MAX_TOKENS	32

enum function {
	F_EXEC, F_MENU, F_SEPARATOR, F_TITLE, F_LOGOUT, F_RESTART, F_QUIT
};

static const struct {
	const char *name;
	enum function fn;
	int needs_arg;
} functions[] = {
	{ "f.exec",	 F_EXEC,	1 },
	{ "f.menu",	 F_MENU,	1 },
	{ "f.separator", F_SEPARATOR,	0 },
	{ "f.title",	 F_TITLE,	0 },
	{ "f.logout",	 F_LOGOUT,	0 },
	{ "f.restart",	 F_RESTART,	0 },
	{ "f.quit",	 F_QUIT,	0 },
};

struct item {
	char *label;
	char mnemonic;
	enum function fn;
	char *arg;
	struct item *next;
};

struct menu {
	char *name;
	struct item *items;
	struct item **tail;
	int building;		/* set while its widgets are made: no loops */
	struct menu *next;
};

static struct menu *menus;
static char **saved_argv;
static Widget toplevel;

static String fallback_resources[] = {
	"Toolchest*fontList: -*-helvetica-bold-r-normal--12-*-*-*-p-*-iso8859-1",
	"Toolchest.geometry: +4+4",
	"Toolchest*chest.spacing: 0",
	NULL
};

static void *
xmalloc(size_t n)
{
	void *p = calloc(1, n);

	if (p == NULL) {
		perror("toolchest");
		exit(1);
	}
	return p;
}

static char *
xstrdup(const char *s)
{
	char *p = strdup(s);

	if (p == NULL) {
		perror("toolchest");
		exit(1);
	}
	return p;
}

/*
 * Split a line into tokens in place: "quoted strings" (a backslash takes
 * the next character literally), the braces { and }, and runs of other
 * non-blank characters. Returns the number of tokens.
 */
static int
tokenize(char *p, char **tok, int max)
{
	static char open_brace[] = "{", close_brace[] = "}";
	int n = 0;
	char *q;

	while (n < max) {
		while (isspace((unsigned char)*p))
			p++;
		if (*p == '\0')
			break;
		if (*p == '"') {
			tok[n++] = q = ++p;
			while (*p != '\0' && *p != '"') {
				if (*p == '\\' && p[1] != '\0')
					p++;
				*q++ = *p++;
			}
			if (*p == '"')
				p++;
			*q = '\0';
		} else if (*p == '{' || *p == '}') {
			tok[n++] = *p == '{' ? open_brace : close_brace;
			p++;
		} else {
			tok[n++] = p;
			while (*p != '\0' && !isspace((unsigned char)*p))
				p++;
			if (*p != '\0')
				*p++ = '\0';
		}
	}
	return n;
}

static struct menu *
find_menu(const char *name)
{
	struct menu *m;

	for (m = menus; m != NULL; m = m->next)
		if (strcmp(m->name, name) == 0)
			return m;
	return NULL;
}

/* A menu by name; one defined again starts over empty. */
static struct menu *
new_menu(const char *name)
{
	struct menu *m = find_menu(name);

	if (m == NULL) {
		m = xmalloc(sizeof *m);
		m->name = xstrdup(name);
		m->next = menus;
		menus = m;
	}
	m->items = NULL;
	m->tail = &m->items;
	return m;
}

static void
add_item(struct menu *m, char **tok, int n, const char *path, int line)
{
	struct item *it;
	size_t len;
	int i = 1, j, f;

	it = xmalloc(sizeof *it);
	it->label = xstrdup(tok[0]);
	if (i < n && tok[i][0] == '_' && tok[i][1] != '\0' && tok[i][2] == '\0')
		it->mnemonic = tok[i++][1];
	/* Skip anything else before the function, such as an accelerator. */
	while (i < n && strncmp(tok[i], "f.", 2) != 0)
		i++;
	if (i == n) {
		fprintf(stderr, "toolchest: %s:%d: no function for \"%s\"\n",
		    path, line, it->label);
		goto bad;
	}
	for (f = 0; f < (int)(sizeof functions / sizeof functions[0]); f++)
		if (strcmp(tok[i], functions[f].name) == 0)
			break;
	if (f == (int)(sizeof functions / sizeof functions[0])) {
		fprintf(stderr, "toolchest: %s:%d: unknown function %s\n",
		    path, line, tok[i]);
		goto bad;
	}
	it->fn = functions[f].fn;
	/* The argument is the rest of the line, as one string. */
	for (len = 0, j = i + 1; j < n; j++)
		len += strlen(tok[j]) + 1;
	if (len > 0) {
		it->arg = xmalloc(len);
		for (j = i + 1; j < n; j++) {
			if (j > i + 1)
				strcat(it->arg, " ");
			strcat(it->arg, tok[j]);
		}
	}
	if (functions[f].needs_arg && it->arg == NULL) {
		fprintf(stderr, "toolchest: %s:%d: %s needs an argument\n",
		    path, line, tok[i]);
		goto bad;
	}
	*m->tail = it;
	m->tail = &it->next;
	return;
bad:
	free(it->label);
	free(it);
}

static int
read_menus(const char *path)
{
	FILE *f;
	char line[2048], *tok[MAX_TOKENS], *s;
	struct menu *cur = NULL;
	int lineno = 0, want_brace = 0, n;

	if ((f = fopen(path, "r")) == NULL)
		return -1;
	while (fgets(line, sizeof line, f) != NULL) {
		lineno++;
		for (s = line; isspace((unsigned char)*s); s++)
			;
		if (*s == '\0' || *s == '#' || *s == '!')
			continue;
		if ((n = tokenize(s, tok, MAX_TOKENS)) == 0)
			continue;
		if (cur == NULL) {
			if (n >= 2 && strcmp(tok[0], "Menu") == 0) {
				cur = new_menu(tok[1]);
				want_brace = !(n >= 3 && strcmp(tok[2], "{") == 0);
			} else
				fprintf(stderr, "toolchest: %s:%d: expected "
				    "\"Menu name\"\n", path, lineno);
			continue;
		}
		if (want_brace) {
			if (strcmp(tok[0], "{") != 0) {
				fprintf(stderr, "toolchest: %s:%d: expected {\n",
				    path, lineno);
				cur = NULL;
			}
			want_brace = 0;
			continue;
		}
		if (strcmp(tok[0], "}") == 0) {
			cur = NULL;
			continue;
		}
		add_item(cur, tok, n, path, lineno);
	}
	if (cur != NULL)
		fprintf(stderr, "toolchest: %s: menu %s has no closing }\n",
		    path, cur->name);
	fclose(f);
	return 0;
}

static void
run(const char *cmd)
{
	pid_t pid;

	if ((pid = fork()) == -1) {
		perror("toolchest: fork");
		return;
	}
	if (pid == 0) {
		/* Its own session, and children reaped as usual. */
		setsid();
		signal(SIGCHLD, SIG_DFL);
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
}

static void
logout_ok(Widget w, XtPointer client, XtPointer call)
{
	const char *s = getenv("DESKTOP_SESSION_PID");
	long pid = s != NULL ? strtol(s, NULL, 10) : 0;

	if (pid > 1 && kill((pid_t)pid, SIGTERM) == -1)
		perror("toolchest: kill");
	exit(0);
}

static void
logout(void)
{
	static Widget dialog;
	XmString msg;
	Arg args[4];
	int n = 0;

	if (dialog == NULL) {
		msg = XmStringCreateLocalized(
		    "Log out?\nAll your windows will be closed.");
		XtSetArg(args[n], XmNmessageString, msg); n++;
		XtSetArg(args[n], XmNdialogStyle,
		    XmDIALOG_FULL_APPLICATION_MODAL); n++;
		dialog = XmCreateQuestionDialog(toplevel, "logout", args, n);
		XmStringFree(msg);
		XtUnmanageChild(XmMessageBoxGetChild(dialog,
		    XmDIALOG_HELP_BUTTON));
		XtAddCallback(dialog, XmNokCallback, logout_ok, NULL);
		XtVaSetValues(XtParent(dialog), XmNtitle, "Log Out", NULL);
	}
	XtManageChild(dialog);
}

static void
activate(Widget w, XtPointer client, XtPointer call)
{
	struct item *it = client;

	switch (it->fn) {
	case F_EXEC:
		run(it->arg);
		break;
	case F_LOGOUT:
		logout();
		break;
	case F_RESTART:
		execvp(saved_argv[0], saved_argv);
		perror("toolchest: restart");
		break;
	case F_QUIT:
		exit(0);
	default:
		break;
	}
}

static void
fill(Widget parent, struct menu *m)
{
	struct item *it;
	struct menu *sub;
	Widget w, pulldown;
	XmString xs;

	m->building = 1;
	for (it = m->items; it != NULL; it = it->next) {
		switch (it->fn) {
		case F_SEPARATOR:
			XtManageChild(XmCreateSeparator(parent, "separator",
			    NULL, 0));
			continue;
		case F_MENU:
			sub = find_menu(it->arg);
			if (sub == NULL || sub->building) {
				fprintf(stderr, "toolchest: menu %s: %s\n",
				    it->arg, sub == NULL ? "not defined" :
				    "contains itself");
				continue;
			}
			pulldown = XmCreatePulldownMenu(parent, sub->name,
			    NULL, 0);
			fill(pulldown, sub);
			xs = XmStringCreateLocalized(it->label);
			w = XtVaCreateManagedWidget("item",
			    xmCascadeButtonWidgetClass, parent,
			    XmNlabelString, xs, XmNsubMenuId, pulldown, NULL);
			break;
		case F_TITLE:
			xs = XmStringCreateLocalized(it->label);
			w = XtVaCreateManagedWidget("title",
			    xmLabelWidgetClass, parent,
			    XmNlabelString, xs, NULL);
			break;
		default:
			xs = XmStringCreateLocalized(it->label);
			w = XtVaCreateManagedWidget("item",
			    xmPushButtonWidgetClass, parent,
			    XmNlabelString, xs, NULL);
			XtAddCallback(w, XmNactivateCallback, activate, it);
			break;
		}
		XmStringFree(xs);
		if (it->mnemonic != '\0')
			XtVaSetValues(w, XmNmnemonic, (KeySym)it->mnemonic,
			    NULL);
	}
	m->building = 0;
}

/* Without a menu file: a shell and a way out. */
static void
default_menus(void)
{
	static char *shell[] = { "Open Unix Shell", "f.exec", "xterm -ls" };
	static char *sep[] = { "no-label", "f.separator" };
	static char *out[] = { "Log Out", "f.logout" };
	static char *top[] = { "Desktop", "f.menu", "Desktop" };
	struct menu *m;

	m = new_menu("Desktop");
	add_item(m, shell, 3, "(built in)", 0);
	add_item(m, sep, 2, "(built in)", 0);
	add_item(m, out, 2, "(built in)", 0);
	m = new_menu(TOP_MENU);
	add_item(m, top, 3, "(built in)", 0);
}

int
main(int argc, char **argv)
{
	XtAppContext app;
	Widget chest;
	struct menu *top;
	char path[1024];
	const char *home, *file = NULL;
	Arg args[4];
	int n = 0;

	saved_argv = argv;
	XtSetLanguageProc(NULL, NULL, NULL);
	toplevel = XtVaOpenApplication(&app, "Toolchest", NULL, 0, &argc, argv,
	    fallback_resources, applicationShellWidgetClass,
	    XmNtitle, "Toolchest",
	    XmNiconName, "Toolchest",
	    XmNallowShellResize, True,
	    XmNmwmDecorations, MWM_DECOR_BORDER | MWM_DECOR_TITLE |
		MWM_DECOR_MENU | MWM_DECOR_MINIMIZE,
	    XmNmwmFunctions, MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE,
	    NULL);
	if (argc == 3 && strcmp(argv[1], "-f") == 0)
		file = argv[2];
	else if (argc != 1) {
		fprintf(stderr, "usage: toolchest [X options] [-f menufile]\n");
		return 2;
	}

	/* Programs it starts must not inherit the X connection. */
	fcntl(ConnectionNumber(XtDisplay(toplevel)), F_SETFD, FD_CLOEXEC);
	signal(SIGCHLD, SIG_IGN);

	if (file != NULL) {
		if (read_menus(file) == -1)
			fprintf(stderr, "toolchest: %s: %s\n", file,
			    strerror(errno));
	} else {
		home = getenv("HOME");
		snprintf(path, sizeof path, "%s/.toolchestrc",
		    home != NULL ? home : "");
		if (home == NULL || read_menus(path) == -1) {
			if (read_menus(SYSTEM_CHESTRC) == -1)
				fprintf(stderr, "toolchest: %s: %s\n",
				    SYSTEM_CHESTRC, strerror(errno));
		}
	}
	if ((top = find_menu(TOP_MENU)) == NULL) {
		fprintf(stderr, "toolchest: no menu named %s; using a minimal "
		    "one\n", TOP_MENU);
		default_menus();
		top = find_menu(TOP_MENU);
	}

	XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
	XtSetArg(args[n], XmNpacking, XmPACK_COLUMN); n++;
	chest = XmCreateMenuBar(toplevel, "chest", args, n);
	fill(chest, top);
	XtManageChild(chest);

	XtRealizeWidget(toplevel);
	XtAppMainLoop(app);
	return 0;
}
