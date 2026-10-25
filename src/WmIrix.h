/*
 * WmIrix.h: IRIX 4Dwm-style window frames (the irixStyle resource).
 *
 * This file is part of emwm and is distributed under the terms of the GNU
 * Lesser General Public License, version 2.1 or later (see COPYING).
 */

#ifndef WM_IRIX_H
#define WM_IRIX_H

void IrixDrawFrame (ClientData *pcd);
Boolean IrixDepressGadget (ClientData *pcd, int gadget, Boolean depressed);

#endif
