/*------------------------------------------------------------------------
--  Copyright (C) 2011-2012 Christopher Brochtrup
--
--  This file is part of eplkup.
--
--  eplkup is free software: you can redistribute it and/or modify
--  it under the terms of the GNU General Public License as published by
--  the Free Software Foundation, either version 3 of the License, or
--  (at your option) any later version.
--
--  eplkup is distributed in the hope that it will be useful,
--  but WITHOUT ANY WARRANTY; without even the implied warranty of
--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--  GNU General Public License for more details.
--
--  You should have received a copy of the GNU General Public License
--  along with eplkup.  If not, see <http://www.gnu.org/licenses/>.
--
------------------------------------------------------------------------*/


#include <wtypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <eb/eb.h>
#include <eb/error.h>
#include <eb/text.h>
#include <eb/font.h>

#include "eplkup_utils.h"
#include "eplkup_hook_handler.h"
#include "eplkup_data.h"
#include "eplkup_gaiji.h"


static void parse_command_line(void);
static void usage(int exit_code, char *msg);


/* Hooks - Used to process certain constructs as they come up (such as gaiji) */
static EB_Hookset hookset;
static EB_Hook hook_begin_subscript    = { EB_HOOK_BEGIN_SUBSCRIPT   , hook_set_begin_subscript   };
static EB_Hook hook_begin_superscript  = { EB_HOOK_BEGIN_SUPERSCRIPT , hook_set_begin_superscript };
static EB_Hook hook_end_subscript      = { EB_HOOK_END_SUBSCRIPT     , hook_set_end_subscript     };
static EB_Hook hook_end_superscript    = { EB_HOOK_END_SUPERSCRIPT   , hook_set_end_superscript   };
static EB_Hook hook_narrow_font        = { EB_HOOK_NARROW_FONT       , hook_set_narrow_font       };
static EB_Hook hook_wide_font          = { EB_HOOK_WIDE_FONT         , hook_set_wide_font         };
static EB_Hook hook_begin_keyword      = { EB_HOOK_BEGIN_KEYWORD     , hook_set_begin_keyword     };
static EB_Hook hook_end_keyword        = { EB_HOOK_END_KEYWORD       , hook_set_end_keyword       };
static EB_Hook hook_begin_emphasis     = { EB_HOOK_BEGIN_EMPHASIS    , hook_set_begin_emphasis    };
static EB_Hook hook_end_emphasis       = { EB_HOOK_END_EMPHASIS      , hook_set_end_emphasis      };
static EB_Hook hook_begin_reference    = { EB_HOOK_BEGIN_REFERENCE   , hook_set_begin_reference   };
static EB_Hook hook_end_reference      = { EB_HOOK_END_REFERENCE     , hook_set_end_reference     };

/* Hook enables */
static int set_emphasis_hook    = 0; /* 1 = User wants to set the bold/emphasis hook */
static int set_keyword_hook     = 0; /* 1 = User wants to set the keyword hook */
static int set_reference_hook   = 0; /* 1 = User wants to set the link/reference hook */
static int set_subscript_hook   = 0; /* 1 = User wants to set the subscript hook */
static int set_superscript_hook = 0; /* 1 = User wants to set the superscript hook */

static char conv_buf[MAXLEN_CONV + 1];      /* Storage for encoding conversion */
static char final_buf[MAXLEN_CONV + 1];     /* Text that will be output to file */
static char text[MAXLEN_TEXT + 1];          /* Entry text */
static char heading[MAXLEN_HEADING + 1];    /* Entry heading */
static char title[EB_MAX_TITLE_LENGTH + 1]; /* Subbook title */


/*------------------------------------------------------------------------
-- Name: WinMain
--
------------------------------------------------------------------------*/
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  FILE *in_file = NULL;
  FILE *out_file = NULL;

  EB_Error_Code error_code;
  EB_Book book;
  EB_Subbook_Code subbook_list[EB_MAX_SUBBOOKS];
  EB_Hit hits[MAX_HITS];

  int subbook_count;
  int hit_count;

  int heading_length;
  int text_length;
  
  int i;

  char lookup_word_utf8[MAXLEN_LOOKUP_WORD + 1];
  char lookup_word_eucjp[MAXLEN_LOOKUP_WORD + 1];
  char *status_conv = NULL;

  /* Parse the command line arguments */
  parse_command_line();

  /* Blank the output file */
  out_file = fopen(out_path, "w");
  fclose(out_file);
  out_file = NULL;

  /* Init EB Lib */
  eb_initialize_library();
  eb_initialize_book(&book);
  eb_initialize_hookset(&hookset);

  /* Set hooks */
  eb_set_hook(&hookset, &hook_narrow_font);
  eb_set_hook(&hookset, &hook_wide_font);

  if(set_emphasis_hook)
  {
    eb_set_hook(&hookset, &hook_begin_emphasis);
    eb_set_hook(&hookset, &hook_end_emphasis);
  }

  if(set_keyword_hook)
  {
    eb_set_hook(&hookset, &hook_begin_keyword);
    eb_set_hook(&hookset, &hook_end_keyword);
  }

  if(set_reference_hook)
  {
    eb_set_hook(&hookset, &hook_begin_reference);
    eb_set_hook(&hookset, &hook_end_reference);
  }

  if(set_subscript_hook)
  {
    eb_set_hook(&hookset, &hook_begin_subscript);
    eb_set_hook(&hookset, &hook_end_subscript);
  }

  if(set_superscript_hook)
  {
    eb_set_hook(&hookset, &hook_begin_superscript);
    eb_set_hook(&hookset, &hook_end_superscript);
  }

  /* Open the EPWING book */
  error_code = eb_bind(&book, book_path);

  if(error_code != EB_SUCCESS)
  {
    fprintf(stderr, "Error: Failed to bind the book, %s: %s\n", eb_error_message(error_code), book_path);
    goto die;
  }

  /* Get the subbook list */
  error_code = eb_subbook_list(&book, subbook_list, &subbook_count);

  if(error_code != EB_SUCCESS)
  {
    fprintf(stderr, "Error: Failed to get the subbbook list, %s\n", eb_error_message(error_code));
    goto die;
  }

  /* Get the subbook */
  error_code = eb_set_subbook(&book, subbook_list[subbook_index]);

  if(error_code != EB_SUCCESS)
  {
    fprintf(stderr, "Error: Failed to set the current subbook, %s\n", eb_error_message(error_code));
    goto die;
  }

  /* If the user want to print the title of the subbook to the output file instead of performing a search */
  if(print_title)
  {
    /* Get the title of the subbook */
    error_code = eb_subbook_title2(&book, subbook_index, title);

    if(error_code != EB_SUCCESS)
    {
      fprintf(stderr, "Error: Failed to get the title: %s\n", eb_error_message(error_code));
      goto die;
    }

    /* Convert title from EUC-JP to UTF-8 */
    status_conv = convert_encoding(conv_buf, MAXLEN_CONV, title, EB_MAX_TITLE_LENGTH, "UTF-8", "EUC-JP");

    if(status_conv == NULL)
    {
      fprintf(stderr, "Error: Something went wrong when trying to encode the title\n");
      goto die;
    }

    out_file = fopen(out_path, "w");

    /* Output the text to file (in UTF-8) */
    fwrite(conv_buf, 1, strlen(conv_buf), out_file);

    fclose(out_file);

    eb_finalize_book(&book);
    eb_finalize_library();
    eb_finalize_hookset(&hookset);

    exit(0);
  }

  /* Get the subbook directory (the name only, not the full path) */
  error_code = eb_subbook_directory(&book, subbook_directory);

  if(error_code != EB_SUCCESS)
  {
    fprintf(stderr, "Error: Failed to get the subbook directory: %s\n", eb_error_message(error_code));
    goto die;
  }

  /* Set the font */
  if (eb_set_font(&book, EB_FONT_16) < 0) 
  {
    fprintf(stderr, "Error: Failed to set the font size: %s\n", eb_error_message(error_code));
    goto die;
  }

  /* Get the word to lookup */
  in_file = fopen(in_path, "r");

  if(in_file == NULL)
  {
    fprintf(stderr, "Error: Could not open input file: \"%s\"", in_path);
    goto die;
  }

  if(fgets(lookup_word_utf8, MAXLEN_LOOKUP_WORD, in_file) == NULL)
  {
    fclose(in_file);
    fprintf(stderr, "Error: Could not read word from input file: \"%s\"", in_path);
    goto die;
  }

  fclose(in_file);

  /* Remove the final '\n' */
  if(lookup_word_utf8[strlen(lookup_word_utf8) - 1] == '\n')
  {
    lookup_word_utf8[strlen(lookup_word_utf8) - 1] = '\0';
  }

  /* Convert the lookup word from UTF-8 to EUC-JP */
  status_conv = convert_encoding(lookup_word_eucjp, MAXLEN_LOOKUP_WORD, lookup_word_utf8, strlen(lookup_word_utf8), "EUC-JP", "UTF-8");

  if(status_conv == NULL)
  {
    fprintf(stderr, "Error: Something went wront when trying to encode the lookup word\n");
    goto die;
  }

  /* Perform an exact search of the lookup word */
  error_code = eb_search_exactword(&book, lookup_word_eucjp);

  if(error_code != EB_SUCCESS)
  {
    fprintf(stderr, "Error: Failed to search for the word, %s: %s\n", eb_error_message(error_code), lookup_word_eucjp);
    goto die;
  }

  while(1)
  {
    /* Get the list of hits */
    error_code = eb_hit_list(&book, MAX_HITS, hits, &hit_count);

    if(error_code != EB_SUCCESS)
    {
      fprintf(stderr, "Error: Failed to get hit entries, %s\n", eb_error_message(error_code));
      goto die;
    }

    /* Are we done? */
    if(hit_count == 0)
    {
      break;
    }

    /* Create the output file */
    out_file = fopen(out_path, "w");

    /* Output only the number of hits? */
    if(show_hit_count)
    {
      fprintf(out_file, "{HITS: %d}\n", hit_count);
    }

    /* Determine the max number of hits to output */
    hit_count = MIN(hit_count, max_hits_to_output);

    /* For each search hit, print the hit information to the output file */
    for(i = 0; i < hit_count; i++)
    {
      /* Did the user specify a particular hit index to output? */
      if(hit_to_output >= 0)
      {
        i = hit_to_output;
      }

      /* Output the hit number */
      if(print_hit_number && (hit_count > 1) && (hit_to_output == -1))
      {
        fprintf(out_file, "{ENTRY: %d}\n", i);
      }

      /* Print the heading of the hit to file */
      if(print_heading)
      {
        /* Seek to the heading */
        error_code = eb_seek_text(&book, &(hits[i].heading));

        if(error_code != EB_SUCCESS)
        {
          fprintf(stderr, "Error: Failed to seek the subbook, %s\n", eb_error_message(error_code));
          goto die;
        }

        /* Read the heading */
        error_code = eb_read_heading(&book, NULL, &hookset, NULL, MAXLEN_HEADING, heading, &heading_length);

        if(error_code != EB_SUCCESS)
        {
          fprintf(stderr, "Error: Failed to read the subbook, %s\n", eb_error_message(error_code));
          goto die;
        }

        /* Convert from EUC-JP to UTF-8 */
        status_conv = convert_encoding(conv_buf, MAXLEN_CONV, heading, heading_length, "UTF-8", "EUC-JP");

        if(status_conv == NULL)
        {
          fprintf(stderr, "Error: Something went wrong when trying to encode the the heading\n");
          goto die;
        }

        /* Replace gaiji that have UTF-8 equivalents */
        replace_gaiji_with_utf8(final_buf, conv_buf);

        /* Output the header to file (in UTF-8) */
        fprintf(out_file, "%s\n", conv_buf);
      }

      /* Print the text of the hit to file */
      if(print_text)
      {
        /* Seek to the text */
        error_code = eb_seek_text(&book, &(hits[i].text));

        if(error_code != EB_SUCCESS)
        {
          fprintf(stderr, "Error: Failed to seek the subbook, %s\n", eb_error_message(error_code));
          goto die;
        }

        /* Read the text*/
        error_code = eb_read_text(&book, NULL, &hookset, NULL, MAXLEN_TEXT, text, &text_length);

        if(error_code != EB_SUCCESS)
        {
          fprintf(stderr, "Error: Failed to read the subbook, %s\n", eb_error_message(error_code));
          goto die;
        }
      }

      /* Convert from EUC-JP to UTF-8 */
      status_conv = convert_encoding(conv_buf, MAXLEN_CONV, text, text_length, "UTF-8", "EUC-JP");

      if(status_conv == NULL)
      {
        fprintf(stderr, "Error: Something went wrong when trying to encode the the text\n");
        goto die;
      }

      /* Replace gaiji that have UTF-8 equivalents */
      replace_gaiji_with_utf8(final_buf, conv_buf);

      /* Output the text to file (in UTF-8) */
      fwrite(final_buf, 1, strlen(final_buf), out_file);

      /* Since the user specifed a hit index, don't display the other hits */
      if(hit_to_output >= 0)
      {
        break;
      }
    }

    fclose(out_file);
  }

  /* Cleanup */
  eb_finalize_book(&book);
  eb_finalize_library();
  eb_finalize_hookset(&hookset);
  exit(0);

/* Something went wrong, cleanup */
die:

  if(out_file != NULL)
  {
    fclose(out_file);
  }

  eb_finalize_book(&book);
  eb_finalize_library();
  eb_finalize_hookset(&hookset);
  exit(1);

} /* WinMain */


/*------------------------------------------------------------------------
-- Name: parse_command_line
--
-- Description:
--   Parse the command line arguments.
--
-- Parameters:
--   None.
--
-- Returns:
--   None.
--
------------------------------------------------------------------------*/
void parse_command_line(void)
{
  int i;
  FILE *fp = NULL;
  int argc = 0;
  LPWSTR *argv;
  char cur_arg[MAXLEN_ARG + 1] = "";
  char *status_conv = NULL;
  argv = CommandLineToArgvW(GetCommandLineW(), &argc);

  if(argv == NULL)
  {
    fprintf(stderr, "Error: Could not determine arguments!\n");
    exit(1);
  }

  /* For each argument */
  for(i = 1; i < argc; i++)
  {
    /* Convert arg to char array */
    wcstombs(cur_arg, argv[i], MAXLEN_ARG);

    if(strcmp(cur_arg, "--emphasis") == 0)
    {
      set_emphasis_hook = 1;
    }
    else if(strcmp(cur_arg, "--gaiji") == 0)
    {
      i++;
      wcstombs(cur_arg, argv[i], MAXLEN_ARG);
      gaiji_option = atoi(cur_arg);

      if((gaiji_option < GAIJI_OPTION_DEFAULT) || (gaiji_option > GAIJI_OPTION_HTML_IMG))
      {
        usage(1, "Bad --gaiji option!\n");
      }
    }
    else if(strcmp(cur_arg, "--help") == 0)
    {
      usage(0, "");
    }
    else if(strcmp(cur_arg, "--hit") == 0)
    {
      i++;
      wcstombs(cur_arg, argv[i], MAXLEN_ARG);
      hit_to_output = atoi(cur_arg);

      if(hit_to_output < 0)
      {
        usage(1, "Bad --hit option!\n");
      }
    }
    else if(strcmp(cur_arg, "--hit-num") == 0)
    {
      print_hit_number = 1;
    }
    else if(strcmp(cur_arg, "--html-sub") == 0)
    {
      set_subscript_hook = 1;
    }
    else if(strcmp(cur_arg, "--html-sup") == 0)
    {
      set_superscript_hook = 1;
    }
    else if(strcmp(cur_arg, "--keyword") == 0)
    {
      set_keyword_hook = 1;
    }
    else if(strcmp(cur_arg, "--link") == 0)
    {
      set_reference_hook = 1;
    }
    else if(strcmp(cur_arg, "--max-hits") == 0)
    {
      i++;
      wcstombs(cur_arg, argv[i], MAXLEN_ARG);
      max_hits_to_output = atoi(cur_arg);
      
      if((max_hits_to_output < 1) || (max_hits_to_output > MAX_HITS))
      {
        usage(1, "Bad --max-hits option!\n");
      }
    }
    else if(strcmp(cur_arg, "--no-header") == 0)
    {
      print_heading = 0;
    }
    else if(strcmp(cur_arg, "--no-text") == 0)
    {
      print_text = 0;
    }
    else if(strcmp(cur_arg, "--show-count") == 0)
    {
      show_hit_count = 1;
    }
    else if(strcmp(cur_arg, "--subbook") == 0)
    {
      i++;
      wcstombs(cur_arg, argv[i], MAXLEN_ARG);
      subbook_index = atoi(cur_arg);
     
      if(subbook_index < 0)
      {
        usage(1, "Bad --subbook option!\n");
      }
    }
    else if(strcmp(cur_arg, "--title") == 0)
    {
      print_title = 1;
    }
    else if(strcmp(cur_arg, "--ver") == 0)
    {
      usage(0, "eplkup version 1.2.1 by Christopher Brochtrup.\n");
    }
    else
    {
      if((argc - i) != 3)
      {
        usage(1, "Error: Incorrect number of arguments!\n");
      }

      /* Get the book path */
      strncpy_len(book_path, cur_arg, MAXLEN_PATH);

      /* Get the input file */
      i++;
      wcstombs(cur_arg, argv[i], MAXLEN_ARG);
      strncpy_len(in_path, cur_arg, MAXLEN_PATH);

      /* Get the output file */
      i++;
      wcstombs(cur_arg, argv[i], MAXLEN_ARG);
      strncpy_len(out_path, cur_arg, MAXLEN_PATH);
    }
  }

} /* parse_command_line */


/*------------------------------------------------------------------------
-- Name: usage
--
-- Description:
--   Print usage information.
--
-- Parameters:
--   (IN) exist_code  - The exit code to return to the shell.
--   (IN) msg         - An additional message to print before the usage.
--                      Leave blank if desired.
--
-- Returns:
--   None.
--
------------------------------------------------------------------------*/
void usage(int exit_code, char *msg)
{
  if(strlen(msg) > 0)
  {
    fprintf(stderr, "%s\n\n", msg);
  }

  printf("Usage: eplkup [--emphasis] [--gaiji] [--help] [--hit #] [--hit-num] [--html-sub] \\\n");
  printf("              [--html-sup] [--keyword] [--link] [--no-header] [--no-text] [--max-hits #] \\\n");
  printf("              [--show-count] [--subbook #] [--title] [--ver] <book-path> <input-file> <output-file>\n");
  printf("\n");
  printf("Performs an exact search on the provided word in the provided EPWING book.\n");
  printf("\n");
  printf("Required:\n");
  printf("  book-path    - Directory that contains the EPWING \"CATALOG\" or \"CATALOGS\" file.\n");
  printf("  input-file   - File that contains the word to lookup (in UTF-8 without BOM).\n");
  printf("  output-file  - File that will contain the lookup text (in UTF-8 without BOM).\n");
  printf("\n");
  printf("Optional:\n");
  printf("  --emphasis   - Place HTML <em></em> tags around bold/emphasized text.\n");
  printf("  --gaiji      - 0 = Replace gaiji with no UTF-8 equivalents with a '?' (default).\n");
  printf("                 1 = Replace gaiji with no UTF-8 equivalents with HTML image tags containing\n");
  printf("                     embedded base64 image data.\n");
  printf("  --help       - Show help.\n");
  printf("  --hit        - Specify which hit to output (starting at 0). If not specified, all hits will be output.\n");
  printf("  --hit-num    - Output the number of the hit above the hit output (if multiple hits). Ex: {ENTRY: 3}.\n");
  printf("  --html-sub   - Put HTML <sub></sub> tags around subscript text.\n");
  printf("  --html-sup   - Put HTML <sup></sup> tags around supercript text.\n");
  printf("  --keyword    - Put <KEYWORD></KEYWORD> tags around the keyword.\n");
  printf("  --link       - Put <LINK></LINK> tags around links/references.\n");
  printf("  --max-hits   - Specify the number of hits to output when --hit is not speicifed. Default is %d.\n", MAX_HITS);
  printf("  --no-header  - Don't print the headers.\n");
  printf("  --no-text    - Don't print the text.\n");
  printf("  --show-count - Output the number of lookup hits in the first line of the output file. Ex. {HITS: 6}\n");
  printf("  --subbook    - The subbook to use in the EPWING book. Default is 0.\n");
  printf("  --title      - Get the title of the subbook.\n");
  printf("  --ver        - Show version.\n");

  printf("\n");

  exit(exit_code);

} /* usage */
