/**
 * @file
 * GUI display the mailboxes in a side panel
 *
 * @authors
 * Copyright (C) 2004 Justin Hibbits <jrh29@po.cwru.edu>
 * Copyright (C) 2004 Thomer M. Gil <mutt@thomer.com>
 * Copyright (C) 2015-2016 Richard Russon <rich@flatcap.org>
 * Copyright (C) 2016-2017 Kevin J. McCarthy <kevin@8t8.us>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @page sidebar GUI display the mailboxes in a side panel
 *
 * GUI display the mailboxes in a side panel
 */

#include "config.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mutt/mutt.h"
#include "config/lib.h"
#include "sidebar.h"
#include "context.h"
#include "curs_lib.h"
#include "format_flags.h"
#include "globals.h"
#include "mailbox.h"
#include "mutt_curses.h"
#include "mutt_menu.h"
#include "mutt_window.h"
#include "muttlib.h"
#include "mx.h"
#include "opcodes.h"
#include "sort.h"

/* These Config Variables are only used in sidebar.c */
short SidebarComponentDepth; ///< Config: (sidebar) Strip leading path components from sidebar folders
char *SidebarDelimChars; ///< Config: (sidebar) Characters that separate nested folders
char *SidebarDividerChar; ///< Config: (sidebar) Character to draw between the sidebar and index
bool SidebarFolderIndent; ///< Config: (sidebar) Indent nested folders
char *SidebarFormat; ///< Config: (sidebar) printf-like format string for the sidebar panel
char *SidebarIndentString; ///< Config: (sidebar) Indent nested folders using this string
bool SidebarNewMailOnly; ///< Config: (sidebar) Only show folders with new/flagged mail
bool SidebarNextNewWrap; ///< Config: (sidebar) Wrap around when searching for the next mailbox with new mail
bool SidebarShortPath; ///< Config: (sidebar) Abbreviate the paths using the Folder variable
short SidebarSortMethod; ///< Config: (sidebar) Method to sort the sidebar

/* Previous values for some sidebar config */
static short PreviousSort = SORT_ORDER; /* sidebar_sort_method */

/**
 * struct SbEntry - Info about folders in the sidebar
 */
struct SbEntry
{
  char box[STRING];        /**< formatted mailbox name */
  struct Mailbox *mailbox; /**< Mailbox this represents */
  bool is_hidden;          /**< Don't show, e.g. $sidebar_new_mail_only */
};

static int EntryCount = 0;
static int EntryLen = 0;
static struct SbEntry **Entries = NULL;

static int TopIndex = -1; /**< First mailbox visible in sidebar */
static int OpnIndex = -1; /**< Current (open) mailbox */
static int HilIndex = -1; /**< Highlighted mailbox */
static int BotIndex = -1; /**< Last mailbox visible in sidebar */

/**
 * enum DivType - Source of the sidebar divider character
 */
enum DivType
{
  SB_DIV_USER,  ///< User configured using $sidebar_divider_char
  SB_DIV_ASCII, ///< An ASCII vertical bar (pipe)
  SB_DIV_UTF8,  ///< A unicode line-drawing character
};

/**
 * sidebar_format_str - Format a string for the sidebar - Implements ::format_t
 *
 * | Expando | Description
 * |:--------|:--------------------------------------------------------
 * | \%B     | Name of the mailbox
 * | \%d     | Number of deleted messages
 * | \%D     | Description of the mailbox
 * | \%F     | Number of Flagged messages in the mailbox
 * | \%L     | Number of messages after limiting
 * | \%n     | N if mailbox has new mail, blank otherwise
 * | \%N     | Number of unread messages in the mailbox
 * | \%S     | Size of mailbox (total number of messages)
 * | \%t     | Number of tagged messages
 * | \%!     | 'n!' Flagged messages
 */
static const char *sidebar_format_str(char *buf, size_t buflen, size_t col, int cols,
                                      char op, const char *src, const char *prec,
                                      const char *if_str, const char *else_str,
                                      unsigned long data, enum FormatFlag flags)
{
  struct SbEntry *sbe = (struct SbEntry *) data;
  unsigned int optional;
  char fmt[STRING];

  if (!sbe || !buf)
    return src;

  buf[0] = 0; /* Just in case there's nothing to do */

  struct Mailbox *m = sbe->mailbox;
  if (!m)
    return src;

  int c = Context && (mutt_str_strcmp(Context->mailbox->realpath, m->realpath) == 0);

  optional = flags & MUTT_FORMAT_OPTIONAL;

  switch (op)
  {
    case 'B':
      mutt_format_s(buf, buflen, prec, sbe->box);
      break;

    case 'd':
      if (!optional)
      {
        snprintf(fmt, sizeof(fmt), "%%%sd", prec);
        snprintf(buf, buflen, fmt, c ? Context->mailbox->msg_deleted : 0);
      }
      else if ((c && (Context->mailbox->msg_deleted == 0)) || !c)
        optional = 0;
      break;

    case 'D':
      if (sbe->mailbox->desc)
        mutt_format_s(buf, buflen, prec, sbe->mailbox->desc);
      else
        mutt_format_s(buf, buflen, prec, sbe->box);
      break;

    case 'F':
      if (!optional)
      {
        snprintf(fmt, sizeof(fmt), "%%%sd", prec);
        snprintf(buf, buflen, fmt, m->msg_flagged);
      }
      else if (m->msg_flagged == 0)
        optional = 0;
      break;

    case 'L':
      if (!optional)
      {
        snprintf(fmt, sizeof(fmt), "%%%sd", prec);
        snprintf(buf, buflen, fmt, c ? Context->mailbox->vcount : m->msg_count);
      }
      else if ((c && Context->mailbox->vcount == m->msg_count) || !c)
        optional = 0;
      break;

    case 'N':
      if (!optional)
      {
        snprintf(fmt, sizeof(fmt), "%%%sd", prec);
        snprintf(buf, buflen, fmt, m->msg_unread);
      }
      else if (m->msg_unread == 0)
        optional = 0;
      break;

    case 'n':
      if (!optional)
      {
        snprintf(fmt, sizeof(fmt), "%%%sc", prec);
        snprintf(buf, buflen, fmt, m->has_new ? 'N' : ' ');
      }
      else if (m->has_new == false)
        optional = 0;
      break;

    case 'S':
      if (!optional)
      {
        snprintf(fmt, sizeof(fmt), "%%%sd", prec);
        snprintf(buf, buflen, fmt, m->msg_count);
      }
      else if (m->msg_count == 0)
        optional = 0;
      break;

    case 't':
      if (!optional)
      {
        snprintf(fmt, sizeof(fmt), "%%%sd", prec);
        snprintf(buf, buflen, fmt, c ? Context->mailbox->msg_tagged : 0);
      }
      else if ((c && (Context->mailbox->msg_tagged == 0)) || !c)
        optional = 0;
      break;

    case '!':
      if (m->msg_flagged == 0)
        mutt_format_s(buf, buflen, prec, "");
      else if (m->msg_flagged == 1)
        mutt_format_s(buf, buflen, prec, "!");
      else if (m->msg_flagged == 2)
        mutt_format_s(buf, buflen, prec, "!!");
      else
      {
        snprintf(fmt, sizeof(fmt), "%d!", m->msg_flagged);
        mutt_format_s(buf, buflen, prec, fmt);
      }
      break;
  }

  if (optional)
  {
    mutt_expando_format(buf, buflen, col, SidebarWidth, if_str,
                        sidebar_format_str, (unsigned long) sbe, flags);
  }
  else if (flags & MUTT_FORMAT_OPTIONAL)
  {
    mutt_expando_format(buf, buflen, col, SidebarWidth, else_str,
                        sidebar_format_str, (unsigned long) sbe, flags);
  }

  /* We return the format string, unchanged */
  return src;
}

/**
 * make_sidebar_entry - Turn mailbox data into a sidebar string
 * @param[out] buf     Buffer in which to save string
 * @param[in]  buflen  Buffer length
 * @param[in]  width   Desired width in screen cells
 * @param[in]  box     Mailbox name
 * @param[in]  sbe     Mailbox object
 *
 * Take all the relevant mailbox data and the desired screen width and then get
 * mutt_expando_format to do the actual work. mutt_expando_format will callback to
 * us using sidebar_format_str() for the sidebar specific formatting characters.
 */
static void make_sidebar_entry(char *buf, size_t buflen, int width, char *box,
                               struct SbEntry *sbe)
{
  if (!buf || !box || !sbe)
    return;

  mutt_str_strfcpy(sbe->box, box, sizeof(sbe->box));

  mutt_expando_format(buf, buflen, 0, width, NONULL(SidebarFormat),
                      sidebar_format_str, (unsigned long) sbe, 0);

  /* Force string to be exactly the right width */
  int w = mutt_strwidth(buf);
  int s = mutt_str_strlen(buf);
  width = MIN(buflen, width);
  if (w < width)
  {
    /* Pad with spaces */
    memset(buf + s, ' ', width - w);
    buf[s + width - w] = 0;
  }
  else if (w > width)
  {
    /* Truncate to fit */
    size_t len = mutt_wstr_trunc(buf, buflen, width, NULL);
    buf[len] = 0;
  }
}

/**
 * cb_qsort_sbe - qsort callback to sort SbEntry's
 * @param a First  SbEntry to compare
 * @param b Second SbEntry to compare
 * @retval -1 a precedes b
 * @retval  0 a and b are identical
 * @retval  1 b precedes a
 */
static int cb_qsort_sbe(const void *a, const void *b)
{
  const struct SbEntry *sbe1 = *(const struct SbEntry **) a;
  const struct SbEntry *sbe2 = *(const struct SbEntry **) b;
  struct Mailbox *m1 = sbe1->mailbox;
  struct Mailbox *m2 = sbe2->mailbox;

  int result = 0;

  switch ((SidebarSortMethod & SORT_MASK))
  {
    case SORT_COUNT:
      if (m2->msg_count == m1->msg_count)
        result = mutt_str_strcoll(m1->path, m2->path);
      else
        result = (m2->msg_count - m1->msg_count);
      break;
    case SORT_UNREAD:
      if (m2->msg_unread == m1->msg_unread)
        result = mutt_str_strcoll(m1->path, m2->path);
      else
        result = (m2->msg_unread - m1->msg_unread);
      break;
    case SORT_DESC:
      result = mutt_str_strcmp(m1->desc, m2->desc);
      break;
    case SORT_FLAGGED:
      if (m2->msg_flagged == m1->msg_flagged)
        result = mutt_str_strcoll(m1->path, m2->path);
      else
        result = (m2->msg_flagged - m1->msg_flagged);
      break;
    case SORT_PATH:
    {
      result = mutt_inbox_cmp(m1->path, m2->path);
      if (result == 0)
        result = mutt_str_strcoll(m1->path, m2->path);
      break;
    }
  }

  if (SidebarSortMethod & SORT_REVERSE)
    result = -result;

  return result;
}

/**
 * update_entries_visibility - Should a sidebar_entry be displayed in the sidebar
 *
 * For each SbEntry in the Entries array, check whether we should display it.
 * This is determined by several criteria.  If the Mailbox:
 * * is the currently open mailbox
 * * is the currently highlighted mailbox
 * * has unread messages
 * * has flagged messages
 * * is whitelisted
 */
static void update_entries_visibility(void)
{
  short new_only = SidebarNewMailOnly;
  struct SbEntry *sbe = NULL;
  for (int i = 0; i < EntryCount; i++)
  {
    sbe = Entries[i];

    sbe->is_hidden = false;

    if (!new_only)
      continue;

    if ((i == OpnIndex) || (sbe->mailbox->msg_unread > 0) ||
        sbe->mailbox->has_new || (sbe->mailbox->msg_flagged > 0))
    {
      continue;
    }

    if (Context && (mutt_str_strcmp(sbe->mailbox->realpath, Context->mailbox->realpath) == 0))
    {
      /* Spool directory */
      continue;
    }

    if (mutt_list_find(&SidebarWhitelist, sbe->mailbox->path) ||
        mutt_list_find(&SidebarWhitelist, sbe->mailbox->desc))
    {
      /* Explicitly asked to be visible */
      continue;
    }

    sbe->is_hidden = true;
  }
}

/**
 * unsort_entries - Restore Entries array order to match Mailbox list order
 */
static void unsort_entries(void)
{
  int i = 0;

  struct MailboxNode *np = NULL;
  STAILQ_FOREACH(np, &AllMailboxes, entries)
  {
    if (i >= EntryCount)
      break;

    int j = i;
    while ((j < EntryCount) && (Entries[j]->mailbox != np->mailbox))
      j++;
    if (j < EntryCount)
    {
      if (j != i)
      {
        struct SbEntry *tmp = Entries[i];
        Entries[i] = Entries[j];
        Entries[j] = tmp;
      }
      i++;
    }
  }
}

/**
 * sort_entries - Sort Entries array
 *
 * Sort the Entries array according to the current sort config
 * option "sidebar_sort_method". This calls qsort to do the work which calls our
 * callback function "cb_qsort_sbe".
 *
 * Once sorted, the prev/next links will be reconstructed.
 */
static void sort_entries(void)
{
  short ssm = (SidebarSortMethod & SORT_MASK);

  /* These are the only sort methods we understand */
  if ((ssm == SORT_COUNT) || (ssm == SORT_UNREAD) || (ssm == SORT_FLAGGED) || (ssm == SORT_PATH))
    qsort(Entries, EntryCount, sizeof(*Entries), cb_qsort_sbe);
  else if ((ssm == SORT_ORDER) && (SidebarSortMethod != PreviousSort))
    unsort_entries();
}

/**
 * select_next - Selects the next unhidden mailbox
 * @retval true  Success
 * @retval false Failure
 */
static bool select_next(void)
{
  int entry = HilIndex;

  if (!EntryCount || HilIndex < 0)
    return false;

  do
  {
    entry++;
    if (entry == EntryCount)
      return false;
  } while (Entries[entry]->is_hidden);

  HilIndex = entry;
  return true;
}

/**
 * select_next_new - Selects the next new mailbox
 * @retval true  Success
 * @retval false Failure
 *
 * Search down the list of mail folders for one containing new mail.
 */
static int select_next_new(void)
{
  int entry = HilIndex;

  if (!EntryCount || HilIndex < 0)
    return false;

  do
  {
    entry++;
    if (entry == EntryCount)
    {
      if (SidebarNextNewWrap)
        entry = 0;
      else
        return false;
    }
    if (entry == HilIndex)
      return false;
  } while (!Entries[entry]->mailbox->has_new && (Entries[entry]->mailbox->msg_unread == 0));

  HilIndex = entry;
  return true;
}

/**
 * select_prev - Selects the previous unhidden mailbox
 * @retval true  Success
 * @retval false Failure
 */
static bool select_prev(void)
{
  int entry = HilIndex;

  if (!EntryCount || HilIndex < 0)
    return false;

  do
  {
    entry--;
    if (entry < 0)
      return false;
  } while (Entries[entry]->is_hidden);

  HilIndex = entry;
  return true;
}

/**
 * select_prev_new - Selects the previous new mailbox
 * @retval true  Success
 * @retval false Failure
 *
 * Search up the list of mail folders for one containing new mail.
 */
static bool select_prev_new(void)
{
  int entry = HilIndex;

  if (!EntryCount || HilIndex < 0)
    return false;

  do
  {
    entry--;
    if (entry < 0)
    {
      if (SidebarNextNewWrap)
        entry = EntryCount - 1;
      else
        return false;
    }
    if (entry == HilIndex)
      return false;
  } while (!Entries[entry]->mailbox->has_new && (Entries[entry]->mailbox->msg_unread == 0));

  HilIndex = entry;
  return true;
}

/**
 * select_page_down - Selects the first entry in the next page of mailboxes
 * @retval true  Success
 * @retval false Failure
 */
static int select_page_down(void)
{
  int orig_hil_index = HilIndex;

  if (!EntryCount || BotIndex < 0)
    return 0;

  HilIndex = BotIndex;
  select_next();
  /* If the rest of the entries are hidden, go up to the last unhidden one */
  if (Entries[HilIndex]->is_hidden)
    select_prev();

  return orig_hil_index != HilIndex;
}

/**
 * select_page_up - Selects the last entry in the previous page of mailboxes
 * @retval true  Success
 * @retval false Failure
 */
static int select_page_up(void)
{
  int orig_hil_index = HilIndex;

  if (!EntryCount || TopIndex < 0)
    return 0;

  HilIndex = TopIndex;
  select_prev();
  /* If the rest of the entries are hidden, go down to the last unhidden one */
  if (Entries[HilIndex]->is_hidden)
    select_next();

  return orig_hil_index != HilIndex;
}

/**
 * prepare_sidebar - Prepare the list of SbEntry's for the sidebar display
 * @param page_size  The number of lines on a page
 * @retval false No, don't draw the sidebar
 * @retval true  Yes, draw the sidebar
 *
 * Before painting the sidebar, we determine which are visible, sort
 * them and set up our page pointers.
 *
 * This is a lot of work to do each refresh, but there are many things that
 * can change outside of the sidebar that we don't hear about.
 */
static bool prepare_sidebar(int page_size)
{
  if (!EntryCount || (page_size <= 0))
    return false;

  const struct SbEntry *opn_entry = (OpnIndex >= 0) ? Entries[OpnIndex] : NULL;
  const struct SbEntry *hil_entry = (HilIndex >= 0) ? Entries[HilIndex] : NULL;

  update_entries_visibility();
  sort_entries();

  for (int i = 0; i < EntryCount; i++)
  {
    if (opn_entry == Entries[i])
      OpnIndex = i;
    if (hil_entry == Entries[i])
      HilIndex = i;
  }

  if ((HilIndex < 0) || Entries[HilIndex]->is_hidden || (SidebarSortMethod != PreviousSort))
  {
    if (OpnIndex >= 0)
      HilIndex = OpnIndex;
    else
    {
      HilIndex = 0;
      if (Entries[HilIndex]->is_hidden)
        select_next();
    }
  }

  /* Set the Top and Bottom to frame the HilIndex in groups of page_size */

  /* If SidebarNewMailOnly is set, some entries may be hidden so we
   * need to scan for the framing interval */
  if (SidebarNewMailOnly)
  {
    TopIndex = -1;
    BotIndex = -1;
    while (BotIndex < HilIndex)
    {
      TopIndex = BotIndex + 1;
      int page_entries = 0;
      while (page_entries < page_size)
      {
        BotIndex++;
        if (BotIndex >= EntryCount)
          break;
        if (!Entries[BotIndex]->is_hidden)
          page_entries++;
      }
    }
  }
  /* Otherwise we can just calculate the interval */
  else
  {
    TopIndex = (HilIndex / page_size) * page_size;
    BotIndex = TopIndex + page_size - 1;
  }

  if (BotIndex > (EntryCount - 1))
    BotIndex = EntryCount - 1;

  PreviousSort = SidebarSortMethod;
  return true;
}

/**
 * draw_divider - Draw a line between the sidebar and the rest of neomutt
 * @param num_rows   Height of the Sidebar
 * @param num_cols   Width of the Sidebar
 * @retval 0   Empty string
 * @retval num Character occupies n screen columns
 *
 * Draw a divider using characters from the config option "sidebar_divider_char".
 * This can be an ASCII or Unicode character.
 * We calculate these characters' width in screen columns.
 *
 * If the user hasn't set $sidebar_divider_char we pick a character for them,
 * respecting the value of $ascii_chars.
 */
static int draw_divider(int num_rows, int num_cols)
{
  if ((num_rows < 1) || (num_cols < 1))
    return 0;

  int delim_len;
  enum DivType altchar = SB_DIV_UTF8;

  /* Calculate the width of the delimiter in screen cells */
  delim_len = mutt_strwidth(SidebarDividerChar);
  if (delim_len < 0)
  {
    delim_len = 1; /* Bad character */
  }
  else if (delim_len == 0)
  {
    if (SidebarDividerChar)
      return 0; /* User has set empty string */

    delim_len = 1; /* Unset variable */
  }
  else
  {
    altchar = SB_DIV_USER; /* User config */
  }

  if (AsciiChars && (altchar != SB_DIV_ASCII))
  {
    /* $ascii_chars overrides Unicode divider chars */
    if (altchar == SB_DIV_UTF8)
    {
      altchar = SB_DIV_ASCII;
    }
    else if (SidebarDividerChar)
    {
      for (int i = 0; i < delim_len; i++)
      {
        if (SidebarDividerChar[i] & ~0x7F) /* high-bit is set */
        {
          altchar = SB_DIV_ASCII;
          delim_len = 1;
          break;
        }
      }
    }
  }

  if (delim_len > num_cols)
    return 0;

  SETCOLOR(MT_COLOR_DIVIDER);

  int col = SidebarOnRight ? 0 : (SidebarWidth - delim_len);

  for (int i = 0; i < num_rows; i++)
  {
    mutt_window_move(MuttSidebarWindow, i, col);

    switch (altchar)
    {
      case SB_DIV_USER:
        addstr(NONULL(SidebarDividerChar));
        break;
      case SB_DIV_ASCII:
        addch('|');
        break;
      case SB_DIV_UTF8:
        addch(ACS_VLINE);
        break;
    }
  }

  return delim_len;
}

/**
 * fill_empty_space - Wipe the remaining Sidebar space
 * @param first_row  Window line to start (0-based)
 * @param num_rows   Number of rows to fill
 * @param div_width  Width in screen characters taken by the divider
 * @param num_cols   Number of columns to fill
 *
 * Write spaces over the area the sidebar isn't using.
 */
static void fill_empty_space(int first_row, int num_rows, int div_width, int num_cols)
{
  /* Fill the remaining rows with blank space */
  NORMAL_COLOR;

  if (!SidebarOnRight)
    div_width = 0;
  for (int r = 0; r < num_rows; r++)
  {
    mutt_window_move(MuttSidebarWindow, first_row + r, div_width);

    for (int i = 0; i < num_cols; i++)
      addch(' ');
  }
}

/**
 * draw_sidebar - Write out a list of mailboxes, in a panel
 * @param num_rows   Height of the Sidebar
 * @param num_cols   Width of the Sidebar
 * @param div_width  Width in screen characters taken by the divider
 *
 * Display a list of mailboxes in a panel on the left.  What's displayed will
 * depend on our index markers: TopMailbox, OpnMailbox, HilMailbox, BotMailbox.
 * On the first run they'll be NULL, so we display the top of NeoMutt's list
 * (AllMailboxes).
 *
 * * TopMailbox - first visible mailbox
 * * BotMailbox - last  visible mailbox
 * * OpnMailbox - mailbox shown in NeoMutt's Index Panel
 * * HilMailbox - Unselected mailbox (the paging follows this)
 *
 * The entries are formatted using "sidebar_format" and may be abbreviated:
 * "sidebar_short_path", indented: "sidebar_folder_indent",
 * "sidebar_indent_string" and sorted: "sidebar_sort_method".  Finally, they're
 * trimmed to fit the available space.
 */
static void draw_sidebar(int num_rows, int num_cols, int div_width)
{
  struct SbEntry *entry = NULL;
  struct Mailbox *m = NULL;
  if (TopIndex < 0)
    return;

  int w = MIN(num_cols, (SidebarWidth - div_width));
  int row = 0;
  for (int entryidx = TopIndex; (entryidx < EntryCount) && (row < num_rows); entryidx++)
  {
    entry = Entries[entryidx];
    if (entry->is_hidden)
      continue;
    m = entry->mailbox;

    if (entryidx == OpnIndex)
    {
      if ((ColorDefs[MT_COLOR_SB_INDICATOR] != 0))
        SETCOLOR(MT_COLOR_SB_INDICATOR);
      else
        SETCOLOR(MT_COLOR_INDICATOR);
    }
    else if (entryidx == HilIndex)
      SETCOLOR(MT_COLOR_HIGHLIGHT);
    else if ((m->msg_unread > 0) || (m->has_new))
      SETCOLOR(MT_COLOR_NEW);
    else if (m->msg_flagged > 0)
      SETCOLOR(MT_COLOR_FLAGGED);
    else if ((ColorDefs[MT_COLOR_SB_SPOOLFILE] != 0) &&
             (mutt_str_strcmp(m->path, Spoolfile) == 0))
    {
      SETCOLOR(MT_COLOR_SB_SPOOLFILE);
    }
    else
    {
      if (ColorDefs[MT_COLOR_ORDINARY] != 0)
        SETCOLOR(MT_COLOR_ORDINARY);
      else
        NORMAL_COLOR;
    }

    int col = 0;
    if (SidebarOnRight)
      col = div_width;

    mutt_window_move(MuttSidebarWindow, row, col);
    if (Context && (Context->mailbox->realpath[0] != '\0') &&
        (mutt_str_strcmp(m->realpath, Context->mailbox->realpath) == 0))
    {
      m->msg_unread = Context->mailbox->msg_unread;
      m->msg_count = Context->mailbox->msg_count;
      m->msg_flagged = Context->mailbox->msg_flagged;
    }

    /* compute length of Folder without trailing separator */
    size_t maildirlen = mutt_str_strlen(Folder);
    if (maildirlen && SidebarDelimChars && strchr(SidebarDelimChars, Folder[maildirlen - 1]))
      maildirlen--;

    /* check whether Folder is a prefix of the current folder's path */
    bool maildir_is_prefix = false;
    if ((mutt_str_strlen(m->path) > maildirlen) &&
        (mutt_str_strncmp(Folder, m->path, maildirlen) == 0) &&
        SidebarDelimChars && strchr(SidebarDelimChars, m->path[maildirlen]))
    {
      maildir_is_prefix = true;
    }

    /* calculate depth of current folder and generate its display name with indented spaces */
    int sidebar_folder_depth = 0;
    char *sidebar_folder_name = NULL;
    if (SidebarShortPath)
    {
      /* disregard a trailing separator, so strlen() - 2 */
      sidebar_folder_name = m->path;
      for (int i = mutt_str_strlen(sidebar_folder_name) - 2; i >= 0; i--)
      {
        if (SidebarDelimChars && strchr(SidebarDelimChars, sidebar_folder_name[i]))
        {
          sidebar_folder_name += (i + 1);
          break;
        }
      }
    }
    else if ((SidebarComponentDepth > 0) && SidebarDelimChars)
    {
      sidebar_folder_name = m->path + maildir_is_prefix * (maildirlen + 1);
      for (int i = 0; i < SidebarComponentDepth; i++)
      {
        char *chars_after_delim = strpbrk(sidebar_folder_name, SidebarDelimChars);
        if (!chars_after_delim)
          break;
        else
          sidebar_folder_name = chars_after_delim + 1;
      }
    }
    else
      sidebar_folder_name = m->path + maildir_is_prefix * (maildirlen + 1);

    if (m->desc)
    {
      sidebar_folder_name = m->desc;
    }
    else if (maildir_is_prefix && SidebarFolderIndent)
    {
      int lastsep = 0;
      const char *tmp_folder_name = m->path + maildirlen + 1;
      int tmplen = (int) mutt_str_strlen(tmp_folder_name) - 1;
      for (int i = 0; i < tmplen; i++)
      {
        if (SidebarDelimChars && strchr(SidebarDelimChars, tmp_folder_name[i]))
        {
          sidebar_folder_depth++;
          lastsep = i + 1;
        }
      }
      if (sidebar_folder_depth > 0)
      {
        if (SidebarShortPath)
          tmp_folder_name += lastsep; /* basename */
        int sfn_len = mutt_str_strlen(tmp_folder_name) +
                      sidebar_folder_depth * mutt_str_strlen(SidebarIndentString) + 1;
        sidebar_folder_name = mutt_mem_malloc(sfn_len);
        sidebar_folder_name[0] = 0;
        for (int i = 0; i < sidebar_folder_depth; i++)
          mutt_str_strcat(sidebar_folder_name, sfn_len, NONULL(SidebarIndentString));
        mutt_str_strcat(sidebar_folder_name, sfn_len, tmp_folder_name);
      }
    }
    char str[STRING];
    make_sidebar_entry(str, sizeof(str), w, sidebar_folder_name, entry);
    printw("%s", str);
    if (sidebar_folder_depth > 0)
      FREE(&sidebar_folder_name);
    row++;
  }

  fill_empty_space(row, num_rows - row, div_width, w);
}

/**
 * mutt_sb_draw - Completely redraw the sidebar
 *
 * Completely refresh the sidebar region.  First draw the divider; then, for
 * each Mailbox, call make_sidebar_entry; finally blank out any remaining space.
 */
void mutt_sb_draw(void)
{
  if (!SidebarVisible)
    return;

#ifdef USE_SLANG_CURSES
  int x = SLsmg_get_column();
  int y = SLsmg_get_row();
#else
  int x = getcurx(stdscr);
  int y = getcury(stdscr);
#endif

  int num_rows = MuttSidebarWindow->rows;
  int num_cols = MuttSidebarWindow->cols;

  int div_width = draw_divider(num_rows, num_cols);

  if (!Entries)
  {
    struct MailboxNode *np = NULL;
    STAILQ_FOREACH(np, &AllMailboxes, entries)
    {
      mutt_sb_notify_mailbox(np->mailbox, true);
    }
  }

  if (!prepare_sidebar(num_rows))
  {
    fill_empty_space(0, num_rows, div_width, num_cols - div_width);
    return;
  }

  draw_sidebar(num_rows, num_cols, div_width);
  move(y, x);
}

/**
 * mutt_sb_change_mailbox - Change the selected mailbox
 * @param op Operation code
 *
 * Change the selected mailbox, e.g. "Next mailbox", "Previous Mailbox
 * with new mail". The operations are listed in opcodes.h.
 *
 * If the operation is successful, HilMailbox will be set to the new mailbox.
 * This function only *selects* the mailbox, doesn't *open* it.
 *
 * Allowed values are: OP_SIDEBAR_NEXT, OP_SIDEBAR_NEXT_NEW,
 * OP_SIDEBAR_PAGE_DOWN, OP_SIDEBAR_PAGE_UP, OP_SIDEBAR_PREV,
 * OP_SIDEBAR_PREV_NEW.
 */
void mutt_sb_change_mailbox(int op)
{
  if (!SidebarVisible)
    return;

  if (HilIndex < 0) /* It'll get reset on the next draw */
    return;

  switch (op)
  {
    case OP_SIDEBAR_NEXT:
      if (!select_next())
        return;
      break;
    case OP_SIDEBAR_NEXT_NEW:
      if (!select_next_new())
        return;
      break;
    case OP_SIDEBAR_PAGE_DOWN:
      if (!select_page_down())
        return;
      break;
    case OP_SIDEBAR_PAGE_UP:
      if (!select_page_up())
        return;
      break;
    case OP_SIDEBAR_PREV:
      if (!select_prev())
        return;
      break;
    case OP_SIDEBAR_PREV_NEW:
      if (!select_prev_new())
        return;
      break;
    default:
      return;
  }
  mutt_menu_set_current_redraw(REDRAW_SIDEBAR);
}

/**
 * mutt_sb_get_highlight - Get the Mailbox that's highlighted in the sidebar
 * @retval ptr Mailbox
 */
struct Mailbox *mutt_sb_get_highlight(void)
{
  if (!SidebarVisible)
    return NULL;

  if (!EntryCount || (HilIndex < 0))
    return NULL;

  return Entries[HilIndex]->mailbox;
}

/**
 * mutt_sb_set_open_mailbox - Set the OpnMailbox based on the global Context
 *
 * Search through the list of mailboxes.  If a Mailbox has a matching path, set
 * OpnMailbox to it.
 */
void mutt_sb_set_open_mailbox(void)
{
  OpnIndex = -1;

  if (!Context)
    return;

  for (int entry = 0; entry < EntryCount; entry++)
  {
    if (mutt_str_strcmp(Entries[entry]->mailbox->realpath, Context->mailbox->realpath) == 0)
    {
      OpnIndex = entry;
      HilIndex = entry;
      break;
    }
  }
}

/**
 * mutt_sb_notify_mailbox - The state of a Mailbox is about to change
 * @param m       Folder
 * @param created True if folder created, false if deleted
 *
 * We receive a notification:
 * - After a new Mailbox has been created
 * - Before a Mailbox is deleted
 *
 * Before a deletion, check that our pointers won't be invalidated.
 */
void mutt_sb_notify_mailbox(struct Mailbox *m, bool created)
{
  if (!m)
    return;

  /* Any new/deleted mailboxes will cause a refresh.  As long as
   * they're valid, our pointers will be updated in prepare_sidebar() */

  if (created)
  {
    if (EntryCount >= EntryLen)
    {
      EntryLen += 10;
      mutt_mem_realloc(&Entries, EntryLen * sizeof(struct SbEntry *));
    }
    Entries[EntryCount] = mutt_mem_calloc(1, sizeof(struct SbEntry));
    Entries[EntryCount]->mailbox = m;

    if (TopIndex < 0)
      TopIndex = EntryCount;
    if (BotIndex < 0)
      BotIndex = EntryCount;
    if ((OpnIndex < 0) && Context &&
        (mutt_str_strcmp(m->realpath, Context->mailbox->realpath) == 0))
      OpnIndex = EntryCount;

    EntryCount++;
  }
  else
  {
    int del_index;
    for (del_index = 0; del_index < EntryCount; del_index++)
      if (Entries[del_index]->mailbox == m)
        break;
    if (del_index == EntryCount)
      return;
    FREE(&Entries[del_index]);
    EntryCount--;

    if (TopIndex > del_index || TopIndex == EntryCount)
      TopIndex--;
    if (OpnIndex == del_index)
      OpnIndex = -1;
    else if (OpnIndex > del_index)
      OpnIndex--;
    if (HilIndex > del_index || HilIndex == EntryCount)
      HilIndex--;
    if (BotIndex > del_index || BotIndex == EntryCount)
      BotIndex--;

    for (; del_index < EntryCount; del_index++)
      Entries[del_index] = Entries[del_index + 1];
  }

  mutt_menu_set_current_redraw(REDRAW_SIDEBAR);
}
